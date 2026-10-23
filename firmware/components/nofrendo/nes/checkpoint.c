/* Complete in-process NES checkpoint codec; file IO belongs to the caller.
 * Checksum, ROM identity, ABI/layout and every page reference are validated
 * before the first byte of live machine state is changed. */
#include "nes.h"
#include "checkpoint.h"

typedef struct {
    uint32 magic, version, length, layout, crc, rom_crc;
    uint32 mapper, system, sample_rate, stereo, endian, word_size;
    uint32 prg_rom, chr_rom, prg_ram, chr_ram;
} header_t;
typedef struct { uint32 region, offset; } page_t;
typedef struct { uint8 *base; size_t length; } region_t;

uint32_t checkpoint_crc_update(uint32_t crc, const void *data, size_t length)
{
    static const uint32 table[16] = {
        0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c
    };
    const uint8 *p = data;
    while (length--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 15];
        crc = (crc >> 4) ^ table[crc & 15];
    }
    return crc;
}
uint32_t checkpoint_crc(const void *data,size_t length) {return ~checkpoint_crc_update(~0u,data,length);}

void checkpoint_field(checkpoint_io_t *io, void *field, size_t size, const char *name)
{
    for (; *name; ++name) io->layout = (io->layout ^ (uint8)*name) * 16777619u;
    io->layout = (io->layout ^ (uint32)size) * 16777619u;
    if (!io->valid || size > io->capacity - io->position) { io->valid = false; return; }
    if (io->mode == 1) memcpy(io->bytes + io->position, field, size);
    if (io->mode == 3) memcpy(field, io->bytes + io->position, size);
    io->position += size;
}

static void regions(nes_t *n, region_t *r)
{
    r[0] = (region_t){NULL,0};
    r[1] = (region_t){n->mem->ram,MEM_RAMSIZE};
    r[2] = (region_t){n->ppu->nametab,4*PPU_PAGESIZE};
    r[3] = (region_t){n->cart->prg_rom,n->cart->prg_rom_banks*ROM_PRG_BANK_SIZE};
    r[4] = (region_t){n->cart->chr_rom,n->cart->chr_rom_banks*ROM_CHR_BANK_SIZE};
    r[5] = (region_t){n->cart->prg_ram,n->cart->prg_ram_banks*ROM_PRG_BANK_SIZE};
    r[6] = (region_t){n->cart->chr_ram,n->cart->chr_ram_banks*ROM_CHR_BANK_SIZE};
    r[7] = (region_t){n->mem->dummy,MEM_PAGESIZE};
}
static void pages(nes_t *n, checkpoint_io_t *io)
{
    region_t r[8]; regions(n,r);
    for (unsigned i=0;i<MEM_PAGECOUNT+PPU_PAGECOUNT;++i) {
        const bool cpu=i<MEM_PAGECOUNT;
        const size_t size=cpu?MEM_PAGESIZE:PPU_PAGESIZE;
        page_t page={0,0};
        if (io->mode<2) {
            uint8 *p=cpu?mem_getpage(i):ppu_getpage(i-MEM_PAGECOUNT);
            if (p) {
                for (unsigned j=1;j<8;++j) {
                    const uintptr_t offset=(uintptr_t)p-(uintptr_t)r[j].base;
                    if (r[j].base && r[j].length>=size && offset<=r[j].length-size) {
                        page.region=j; page.offset=(uint32)offset; break;
                    }
                }
                if (!page.region) io->valid=false;
            } else if (!cpu) io->valid=false;
        } else if (io->valid && sizeof(page)<=io->capacity-io->position) {
            memcpy(&page,io->bytes+io->position,sizeof(page));
            if (page.region>=8 || (!cpu && !page.region) ||
                (!page.region && page.offset) || (page.region &&
                (!r[page.region].base || r[page.region].length<size ||
                 page.offset>r[page.region].length-size))) io->valid=false;
        }
        CHECKPOINT_FIELD(io,page);
        if (io->mode==3 && io->valid) {
            uint8 *p=page.region?r[page.region].base+page.offset:NULL;
            if (cpu) mem_setpage(i,p); else ppu_setpage(i-MEM_PAGECOUNT,p);
        }
    }
}

static void machine(nes_t *n, checkpoint_io_t *io)
{
    nes6502_t *c=n->cpu;
    ppu_t *p=n->ppu;
    apu_t *a=n->apu;
    CHECKPOINT_FIELD(io,n->scanline); CHECKPOINT_FIELD(io,n->cycles);
    CHECKPOINT_FIELD(io,c->pc_reg); CHECKPOINT_FIELD(io,c->a_reg);
    CHECKPOINT_FIELD(io,c->x_reg); CHECKPOINT_FIELD(io,c->y_reg);
    CHECKPOINT_FIELD(io,c->s_reg); CHECKPOINT_FIELD(io,c->p_reg);
    CHECKPOINT_FIELD(io,c->int_pending); CHECKPOINT_FIELD(io,c->mapper_irq);
    CHECKPOINT_FIELD(io,c->jammed); CHECKPOINT_FIELD(io,c->total_cycles);
    CHECKPOINT_FIELD(io,c->burn_cycles);
    CHECKPOINT_FIELD(io,p->oam); CHECKPOINT_FIELD(io,p->palette);
    CHECKPOINT_FIELD(io,p->ctrl0); CHECKPOINT_FIELD(io,p->ctrl1);
    CHECKPOINT_FIELD(io,p->stat); CHECKPOINT_FIELD(io,p->oam_addr);
    CHECKPOINT_FIELD(io,p->nametab_base); CHECKPOINT_FIELD(io,p->latch);
    CHECKPOINT_FIELD(io,p->vdata_latch); CHECKPOINT_FIELD(io,p->tile_xofs);
    CHECKPOINT_FIELD(io,p->flipflop); CHECKPOINT_FIELD(io,p->vaddr);
    CHECKPOINT_FIELD(io,p->vaddr_latch); CHECKPOINT_FIELD(io,p->vaddr_inc);
    CHECKPOINT_FIELD(io,p->nt_map); CHECKPOINT_FIELD(io,p->obj_height);
    CHECKPOINT_FIELD(io,p->obj_base); CHECKPOINT_FIELD(io,p->bg_base);
    CHECKPOINT_FIELD(io,p->left_bg_on); CHECKPOINT_FIELD(io,p->left_obj_on);
    CHECKPOINT_FIELD(io,p->bg_on); CHECKPOINT_FIELD(io,p->obj_on);
    CHECKPOINT_FIELD(io,p->strikeflag); CHECKPOINT_FIELD(io,p->strike_cycle);
    CHECKPOINT_FIELD(io,p->scanlines); CHECKPOINT_FIELD(io,p->scanline);
    CHECKPOINT_FIELD(io,p->left_bg_counter);
    CHECKPOINT_FIELD(io,p->vram_accessible); CHECKPOINT_FIELD(io,p->vram_present);
    CHECKPOINT_FIELD(io,p->options);
    uint16 readonly=ppu_getreadonly();
    CHECKPOINT_FIELD(io,readonly);
    if (io->mode==3) ppu_setreadonly(readonly);
    CHECKPOINT_FIELD(io,a->rectangle); CHECKPOINT_FIELD(io,a->triangle);
    CHECKPOINT_FIELD(io,a->noise); CHECKPOINT_FIELD(io,a->dmc);
    CHECKPOINT_FIELD(io,a->control_reg); CHECKPOINT_FIELD(io,a->prev_sample);
    CHECKPOINT_FIELD(io,a->cycle_rate); CHECKPOINT_FIELD(io,a->fc);
    CHECKPOINT_FIELD(io,a->decay_lut); CHECKPOINT_FIELD(io,a->vbl_lut);
    CHECKPOINT_FIELD(io,a->trilength_lut); CHECKPOINT_FIELD(io,a->options);
    checkpoint_field(io,a->buffer,a->samples_per_frame*sizeof(short)*(a->stereo?2:1),"audio");
    checkpoint_field(io,n->input,2*sizeof(input_t),"ports");
    int strobe=input_getstrobe();
    CHECKPOINT_FIELD(io,strobe);
    if (io->mode==3) input_setstrobe(strobe);
    CHECKPOINT_FIELD(io,n->mem->ram);
    checkpoint_field(io,n->mem->dummy,MEM_PAGESIZE,"dummy");
    checkpoint_field(io,p->nametab,4*PPU_PAGESIZE,"nametab");
    checkpoint_field(io,n->cart->prg_ram,n->cart->prg_ram_banks*ROM_PRG_BANK_SIZE,"prg_ram");
    checkpoint_field(io,n->cart->chr_ram,n->cart->chr_ram_banks*ROM_CHR_BANK_SIZE,"chr_ram");
    checkpoint_field(io,n->vidbuf,NES_SCREEN_PITCH*NES_SCREEN_HEIGHT,"video");
    if (n->mapper->checkpoint) n->mapper->checkpoint(io);
    pages(n,io);
}
static bool available(nes_t *n) { return n && n->cart && n->mapper && n->vidbuf; }
static checkpoint_io_t walk(nes_t *n, void *data, size_t length, int mode)
{
    checkpoint_io_t io={data,sizeof(header_t),length,2166136261u,mode,true};
    machine(n,&io);
    return io;
}
size_t checkpoint_size(nes_t *n)
{
    if (!available(n)) return 0;
    checkpoint_io_t io=walk(n,NULL,SIZE_MAX,0);
    return io.valid?io.position:0;
}
static header_t identity(nes_t *n)
{
    uint32 endian=1;
    header_t h={0};
    h.magic=0x534D5559; h.version=1;
    h.rom_crc=checkpoint_crc(n->cart->data_ptr,n->cart->data_len);
    h.mapper=n->mapper->number; h.system=n->system;
    h.sample_rate=n->apu->sample_rate; h.stereo=n->apu->stereo;
    h.endian=*(uint8 *)&endian; h.word_size=sizeof(long);
    h.prg_rom=n->cart->prg_rom_banks; h.chr_rom=n->cart->chr_rom_banks;
    h.prg_ram=n->cart->prg_ram_banks; h.chr_ram=n->cart->chr_ram_banks;
    return h;
}
bool checkpoint_save(nes_t *n, void *data, size_t length)
{
    if (!available(n) || !data || length!=checkpoint_size(n)) return false;
    checkpoint_io_t io=walk(n,data,length,1);
    if (!io.valid || io.position!=length) return false;
    header_t h=identity(n);
    h.length=length; h.layout=io.layout;
    h.crc=checkpoint_crc((uint8 *)data+sizeof(h),length-sizeof(h));
    memcpy(data,&h,sizeof(h));
    return true;
}
bool checkpoint_prepare(nes_t *n,checkpoint_profile_t *profile)
{
    if(!profile || !available(n))return false;
    checkpoint_io_t io=walk(n,NULL,SIZE_MAX,0);
    if(!io.valid)return false;
    header_t h=identity(n);h.length=io.position;h.layout=io.layout;
    typedef char profile_header_size_check[sizeof(h)==sizeof(profile->header)?1:-1];
    (void)sizeof(profile_header_size_check);
    memcpy(profile->header,&h,sizeof(h));profile->length=io.position;return true;
}
bool checkpoint_capture(nes_t *n,const checkpoint_profile_t *profile,void *data,size_t length)
{
    if(!available(n) || !profile || !data || length!=profile->length)return false;
    checkpoint_io_t io=walk(n,data,length,1);
    header_t h;memcpy(&h,profile->header,sizeof(h));
    if(!io.valid || io.position!=length || io.layout!=h.layout)return false;
    memcpy(data,&h,sizeof(h));return true;
}
bool checkpoint_seal(void *data,size_t length)
{
    if(!data || length<sizeof(header_t))return false;
    header_t h;memcpy(&h,data,sizeof(h));
    if(h.magic!=0x534D5559 || h.version!=1 || h.length!=length)return false;
    h.crc=checkpoint_crc((uint8 *)data+sizeof(h),length-sizeof(h));
    memcpy(data,&h,sizeof(h));return true;
}
bool checkpoint_valid(nes_t *n, const void *data, size_t length)
{
    if (!available(n) || !data || length<sizeof(header_t) || length!=checkpoint_size(n)) return false;
    header_t h,want=identity(n);
    memcpy(&h,data,sizeof(h));
    checkpoint_io_t io=walk(n,(void *)data,length,2);
    want.length=length; want.layout=io.layout;
    want.crc=checkpoint_crc((const uint8 *)data+sizeof(h),length-sizeof(h));
    return io.valid && io.position==length && !memcmp(&h,&want,sizeof(h));
}
bool checkpoint_load(nes_t *n, const void *data, size_t length)
{
    if (!checkpoint_valid(n,data,length)) return false;
    /* No allocations, register writes, callbacks or other fallible work here. */
    checkpoint_io_t io=walk(n,(void *)data,length,3);
    return io.valid;
}
