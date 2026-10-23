// Native execution of the production CPU and mapper; only bus/audio sinks are
// substituted. No copyrighted ROM is needed for bank/IRQ/audio regression.
#include "nes/cpu.c"
#include "mappers/map024.c"
#undef malloc
#undef calloc
#include <stdio.h>

static nes_t host;
static rom_t cart;
static apu_t sound;
static uint8 prg[32*8192],chr[64*1024],ram[8192],nt[2048],zero[2048],dummy[2048];
static uint8 *pages[32],*ppu_pages[16];
static uint16 readonly;
static const apuext_t *ext;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
// Checkpoints are exercised by fc_checkpoint_runtime against the full engine.
// This focused bus/IRQ/audio harness must never invoke checkpoint IO.
void checkpoint_field(checkpoint_io_t *io,void *field,size_t size,const char *name) {
    (void)io;(void)field;(void)size;(void)name;CHECK(0);
}
nes_t *nes_getptr(void) {return &host;}
void *fc_alloc(size_t n) {return malloc(n);}
void *fc_calloc(size_t n,size_t s) {return calloc(n,s);}
void mem_setpage(uint32 page,uint8 *ptr) {pages[page]=(ptr?ptr:dummy)-page*2048;}
uint8 mem_getbyte(uint32 address) {
    if(address>=0x6000 && address<0x8000) return ram_read(address);
    return pages[address>>11][address];
}
uint32 mem_getword(uint32 address) {return mem_getbyte(address)|((uint32)mem_getbyte((address+1)&65535)<<8);}
void mem_putbyte(uint32 address,uint8 value) {
    if(address>=0x6000) map_write(address,value);else pages[address>>11][address]=value;
}
void mmc_bankprg(unsigned size,unsigned address,int bank,uint8 *base) {
    const unsigned banks=(base==PRG_ROM?cart.prg_rom_banks:cart.prg_ram_banks)/(size/8);
    if(bank<0) bank+=banks;bank%=banks;
    uint8 *p=(base==PRG_ROM?prg:ram)+bank*size*1024;
    for(unsigned i=0;i<size/2;++i) mem_setpage((address>>11)+i,p+i*2048);
}
void mmc_bankchr(unsigned size,unsigned address,int bank,uint8 *base) {
    (void)base;for(unsigned i=0;i<size;++i) ppu_pages[(address>>10)+i]=chr+((bank*size+i)%64)*1024;
}
void ppu_setpage(uint32 page,uint8 *p) {ppu_pages[page]=p;if(page>=8) ppu_pages[page^4]=p;}
void ppu_setnametable(uint8 i,uint8 table) {ppu_setpage(8+i,nt+table*1024);}
void ppu_setreadonly(uint16 mask) {readonly=mask;}
void apu_setext(const apuext_t *e) {ext=e;if(e && e->reset) e->reset();}

static void reset_host(void) {
    memset(&host,0,sizeof(host));memset(&cart,0,sizeof(cart));memset(&sound,0,sizeof(sound));
    memset(zero,0,sizeof(zero));memset(dummy,255,sizeof(dummy));
    for(unsigned i=0;i<32;++i) memset(prg+i*8192,i,8192);
    for(unsigned i=0;i<64;++i) memset(chr+i*1024,i,1024);
    for(unsigned i=0;i<32;++i) mem_setpage(i,i<4?zero:NULL);
    cart.prg_rom=prg;cart.prg_rom_banks=32;cart.prg_ram=ram;cart.prg_ram_banks=1;
    cart.chr_rom=chr;cart.chr_rom_banks=8;
    sound.samples_per_frame=367;host.apu=&sound;host.cart=&cart;
    host.cycles_per_scanline=114;host.scanlines_per_frame=262;
    host.cpu=nes6502_init(pages);map_init(&cart);
}
static void banking(void) {
    reset_host();CHECK(mem_getbyte(0xe000)==31);
    for(unsigned bank=0;bank<16;++bank) {
        map_write(0x8ffd,bank);CHECK(mem_getbyte(0x8000)==bank*2);CHECK(mem_getbyte(0xa000)==bank*2+1);
    }
    map_write(0xcfff,7);CHECK(mem_getbyte(0xc000)==7);CHECK(mem_getbyte(0xe000)==31);
    map_write(0xb003,0);ram[7]=0;map_write(0x6007,99);CHECK(ram[7]==0 && ram_read(0x6007)==255);
    map_write(0xb003,0x80);map_write(0x6007,99);CHECK(ram_read(0x6007)==99);
    for(unsigned i=0;i<8;++i) map_write((i<4?0xd000:0xe000)+(i&3),10+i);
    for(unsigned i=0;i<8;++i) CHECK(*ppu_pages[i]==10+i);
    map_write(0xb003,0xa1); // Four even-aligned 2K pairs.
    for(unsigned i=0;i<8;++i) CHECK(*ppu_pages[i]==((10+i/2)&0xfe)+(i&1));
    map_write(0xb003,0xa2);CHECK(*ppu_pages[0]==10 && *ppu_pages[3]==13);
    CHECK(*ppu_pages[4]==14 && *ppu_pages[5]==15 && *ppu_pages[6]==14 && *ppu_pages[7]==15);
    static const uint8 mirror[4][4]={{0,1,0,1},{0,0,1,1},{0,0,0,0},{1,1,1,1}};
    for(unsigned i=0;i<4;++i) {
        map_write(0xb003,0xa0+4*i);
        for(unsigned p=0;p<4;++p) CHECK(ppu_pages[8+p]==nt+1024*mirror[i][p]);
    }
    map_write(0xb003,0xb0);CHECK(readonly==65535);
    CHECK(*ppu_pages[8]==16 && *ppu_pages[9]==17 && *ppu_pages[10]==16 && *ppu_pages[11]==17);
    for(unsigned p=8;p<12;++p) CHECK(ppu_pages[p]==ppu_pages[p+4]);
    map_write(0xb003,0xa0);CHECK(readonly==255);
}
static void interrupts(void) {
    reset_host();map_write(0xf000,0xfe);map_write(0xf001,7);
    map_clock(1);CHECK(!cpu.mapper_irq);map_clock(1);CHECK(cpu.mapper_irq && vrc.irq.counter==0xfe);
    cpu.int_pending=true;const uint8 flags=cpu.p_reg;map_write(0xf002,0);
    CHECK(!cpu.mapper_irq && cpu.int_pending && cpu.p_reg==flags && (vrc.irq.control&2));
    map_clock(2);CHECK(cpu.mapper_irq);map_write(0xf001,6);map_clock(2);CHECK(cpu.mapper_irq);
    map_write(0xf002,0);map_clock(500);CHECK(!cpu.mapper_irq && !(vrc.irq.control&2));
    map_write(0xf000,0xff);map_write(0xf001,3);
    map_clock(113);CHECK(!cpu.mapper_irq);map_clock(1);CHECK(cpu.mapper_irq);
    map_write(0xf002,0);map_clock(113);CHECK(!cpu.mapper_irq);map_clock(1);CHECK(cpu.mapper_irq);
    map_write(0xf002,0);map_clock(113);CHECK(cpu.mapper_irq); // 114,114,113 cycle cadence.
    // Exercise real CPU instructions, IRQ stack/vector, burn and level release.
    reset_host();memset(prg,0xea,sizeof(prg));
    prg[sizeof(prg)-4]=0;prg[sizeof(prg)-3]=0x80;
    prg[sizeof(prg)-2]=0;prg[sizeof(prg)-1]=0xe0;
    nes6502_reset();cpu.burn_cycles=0;cpu.p_reg=R_FLAG;cpu.s_reg=0xff;
    map_write(0xf000,0xff);map_write(0xf001,6);
    CHECK(nes6502_execute(2)==2 && cpu.pc_reg==0x8001 && cpu.mapper_irq);
    CHECK(nes6502_execute(7)==7 && cpu.pc_reg==0xe000 && cpu.s_reg==0xfc);
    CHECK(cpu.mapper_irq && (cpu.p_reg&I_FLAG));map_write(0xf002,0);CHECK(!cpu.mapper_irq);
    map_write(0xf001,6);nes6502_burn(4);nes6502_execute(4);CHECK(cpu.mapper_irq);
}
static void audio(void) {
    reset_host();CHECK(ext && cycle_hook && !vrc.count);
    map_write(0x9000,0x8f);map_write(0x9001,100);map_write(0x9002,0x80);
    map_clock(82);CHECK(vrc.count==1 && audio_process()>0);
    map_write(0x9002,0);CHECK(vrc.voice[0].phase==0);
    map_write(0x9000,0x0f);map_write(0x9001,1);map_write(0x9002,0x80);
    vrc.voice[0].timer=1;vrc.audio_pending=31;advance_audio();CHECK(vrc.voice[0].phase==0);
    map_write(0x9003,1);vrc.audio_pending=100;advance_audio();CHECK(vrc.voice[0].phase==0);
    map_write(0x9003,4);CHECK(period(&vrc.voice[0])==1);
    map_write(0x9003,0);map_write(0xb000,8);map_write(0xb001,0);map_write(0xb002,0x80);
    vrc.audio_pending=12;advance_audio();CHECK(vrc.voice[2].accumulator==48 && vrc.voice[2].phase==12);
    vrc.audio_pending=2;advance_audio();CHECK(vrc.voice[2].accumulator==0 && vrc.voice[2].phase==0);
    map_write(0xb002,0);CHECK(vrc.voice[2].accumulator==0);
    for(unsigned i=0;i<5000;++i) map_clock(114);CHECK(vrc.count<=1024);
    reset_host();CHECK(!vrc.audio_control && !vrc.count && !vrc.irq.asserted && !cpu.mapper_irq);
}
int main(void) {banking();interrupts();audio();printf("VRC6 native checks: %u passed\n",checks);return 0;}
