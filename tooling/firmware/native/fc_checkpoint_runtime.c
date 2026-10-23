/* Real production CPU/PPU/APU/all mappers + file transactions. Synthetic ROMs
 * only. Compare full state and audio/video after replay and after reallocation. */
#define main mapper_regressions_main
#include "mmc3_variants_runtime.c"
#undef main
#include "nes/checkpoint.h"
#include "core/fc_save_store.h"
#include "core/vault_sha256.h"
#include <errno.h>

static uint8 snapshot[FC_SAVE_MAX], reference[FC_SAVE_MAX], scratch[FC_SAVE_MAX];
static int cancel_at=-1, calls;
static bool proceed_test(void *ctx) { (void)ctx; return cancel_at<0 || ++calls<cancel_at; }
static bool validate_test(void *ctx,const void *p,size_t size) { (void)ctx;return checkpoint_valid(host,p,size); }
static bool capture_test(void *ctx,void *p,size_t size) { (void)ctx;return checkpoint_save(host,p,size); }
static unsigned latch_calls;
static void count_latch(uint32 addr,uint8 value){(void)addr;(void)value;++latch_calls;}
static void skipped_scanlines(void)
{
    for(unsigned solid=0;solid<2;++solid) {
        start(0,4,0,false,false);nes_setvidbuf(video);
        memset(host->ppu->oam,255,256);
        memset(host->cart->chr_ram,0,8192);
        memset(host->cart->chr_ram+16,solid?255:0,8);
        memset(host->cart->chr_ram+32,255,8);
        memset(host->ppu->nametab,1,4096);
        for(unsigned i=0;i<32;++i)host->ppu->palette[i]=(i%4)?i:0x80;
        host->ppu->oam[0]=9;host->ppu->oam[1]=2;host->ppu->oam[2]=0;host->ppu->oam[3]=40;
        ppu_write(PPU_CTRL0,0);ppu_write(PPU_CTRL1,0x1E);
        const size_t size=checkpoint_size(host);CHECK(checkpoint_save(host,snapshot,size));
        ppu_renderline(video,10,true);CHECK(host->ppu->strikeflag==!!solid);
        memset(video,0,sizeof(video));CHECK(checkpoint_save(host,reference,size));
        CHECK(checkpoint_load(host,snapshot,size));ppu_renderline(video,10,false);
        CHECK(host->ppu->strikeflag==!!solid);
        memset(video,0,sizeof(video));CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(scratch,reference,size));
        finish();
    }
    // More than one sprite and background FD/FE fetches must reach MMC2/MMC4,
    // even when display delivery is skipped. Status must match the drawn line.
    for(unsigned mapper=9;mapper<=10;++mapper) {
        start(mapper,16,32,false,false);nes_setvidbuf(video);
        memset(host->ppu->nametab,0xFD,4096);memset(host->ppu->oam,255,256);
        for(unsigned i=0;i<12;++i){host->ppu->oam[i*4]=9;host->ppu->oam[i*4+1]=0xFE;host->ppu->oam[i*4+2]=0;host->ppu->oam[i*4+3]=i*16;}
        ppu_write(PPU_CTRL0,0);ppu_write(PPU_CTRL1,0x1E);const size_t size=checkpoint_size(host);
        CHECK(checkpoint_save(host,snapshot,size));
        ppu_renderline(video,10,true);memset(video,0,sizeof(video));CHECK(checkpoint_save(host,reference,size));
        CHECK(checkpoint_load(host,snapshot,size));ppu_renderline(video,10,false);memset(video,0,sizeof(video));
        CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(scratch,reference,size));
        CHECK(checkpoint_load(host,snapshot,size));ppu_setlatchfunc(count_latch);latch_calls=0;
        ppu_renderline(video,10,true);unsigned drawn=latch_calls;CHECK(drawn>=40);
        CHECK(checkpoint_load(host,snapshot,size));latch_calls=0;ppu_renderline(video,10,false);CHECK(latch_calls==drawn);
        finish();
    }
}
static void exercise(unsigned mapper)
{
    start(mapper,128,mapper==245?0:256,false,false);
    nes_setvidbuf(video);
    ppu_write(PPU_CTRL1,0x18);
    static const unsigned addresses[]={0x8000,0x8001,0xA000,0xA001,0xB000,0xB001,
        0xC000,0xC001,0xD000,0xD001,0xE000,0xE001,0xF000,0xF001,0xF002,0xF003};
    for(unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);++i) mem_putbyte(addresses[i],(i*3+1)&15);
    // Keep audio oscillators, serial input reads and mapper registers live.
    apu_write(0x4015,15);apu_write(0x4000,0xBF);apu_write(0x4002,24);apu_write(0x4003,8);
    if(mapper==24) { mem_putbyte(0x9000,0x7F);mem_putbyte(0x9001,12);mem_putbyte(0x9002,0x80); }
    for(unsigned f=0;f<4;++f) nes_emulate(true);
    input_update(0,0x91);input_write(0x4016,1);input_write(0x4016,0);(void)input_read(0x4016);
    host->cpu->p_reg|=I_FLAG;host->cpu->int_pending=true;nes6502_setmapperirq(true);
    const size_t size=checkpoint_size(host);CHECK(size>80000 && size<FC_SAVE_MAX);
    CHECK(checkpoint_save(host,snapshot,size));
    checkpoint_profile_t profile;
    CHECK(checkpoint_prepare(host,&profile));CHECK(profile.length==size);
    CHECK(checkpoint_capture(host,&profile,scratch,size));
    CHECK(checkpoint_seal(scratch,size));CHECK(!memcmp(scratch,snapshot,size));
    // Sealing a detached frame after emulation advances must not touch live NES.
    CHECK(checkpoint_capture(host,&profile,scratch,size));
    // Advancing must be bit-identical, including image and audio waveform.
    for(unsigned f=0;f<9;++f) { input_update(0,f*7);nes_emulate(f&1); }
    CHECK(checkpoint_save(host,reference,size));
    CHECK(checkpoint_seal(scratch,size));CHECK(!memcmp(scratch,snapshot,size));
    CHECK(checkpoint_load(host,snapshot,size));
    for(unsigned f=0;f<9;++f) { input_update(0,f*7);nes_emulate(f&1); }
    CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(reference,scratch,size));
    // Damaged header/payload, wrong size and wrong ROM never modify live state.
    for(size_t offset=0;offset<size;offset+=997) {
        snapshot[offset]^=0x80;CHECK(!checkpoint_load(host,snapshot,size));snapshot[offset]^=0x80;
    }
    CHECK(!checkpoint_load(host,snapshot,size-1));
    // A forged checksum still cannot introduce an out-of-range bank pointer.
    memcpy(scratch,snapshot,size);scratch[size-8]=255;
    uint32 crc=checkpoint_crc(scratch+64,size-64);memcpy(scratch+16,&crc,4);
    CHECK(!checkpoint_load(host,scratch,size));
    image[16+121]^=1;CHECK(!checkpoint_load(host,snapshot,size));image[16+121]^=1;
    CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(reference,scratch,size));
    // Reset/restart reestablishes handlers but checkpoint must restore all banks,
    // IRQ state, RAM, MMC2 latches, VRC6 audio and input serial position itself.
    nes_reset(true);nes_setvidbuf(video);CHECK(checkpoint_load(host,snapshot,size));
    CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(snapshot,scratch,size));
    finish();
    uint8 *moved=malloc(image_size);CHECK(moved);memcpy(moved,image,image_size);
    host=nes_init(SYS_DETECT,22050,false,NULL);CHECK(host);
    rom_t *cart=rom_loadmem(moved,image_size);CHECK(cart);CHECK(!nes_insertcart(cart));nes_setvidbuf(video);
    CHECK(checkpoint_load(host,snapshot,size));
    for(unsigned f=0;f<9;++f) { input_update(0,f*7);nes_emulate(f&1); }
    CHECK(checkpoint_save(host,scratch,size));CHECK(!memcmp(reference,scratch,size));
    finish();
    free(moved);
}
static void transactions(const char *directory)
{
    start(24,128,256,false,false);nes_setvidbuf(video);nes_emulate(true);
    fc_save_store_t store={0};store.directory=directory;store.buffer=scratch;store.length=checkpoint_size(host);
    store.proceed=proceed_test;store.validate=validate_test;store.capture=capture_test;
    for(unsigned i=0;i<32;++i) store.rom[i]=i;
    fc_save_slot_t slots[FC_SAVE_SLOTS],saved,newer,loaded;
    // Legacy manual 4/5 files must be ignored and never used as automatic banks.
    char legacy[2][256],automatic[256];
    CHECK(fc_save_path(&store,FC_SAVE_AUTO_SLOT,0,automatic,sizeof(automatic)));
    char *suffix=strstr(automatic,"-auto-a.sav");CHECK(suffix);
    for(unsigned i=0;i<2;++i) {
        snprintf(legacy[i],sizeof(legacy[i]),"%.*s-%u-a.sav",(int)(suffix-automatic),automatic,i+4);
        FILE *old=fopen(legacy[i],"wb");CHECK(old);CHECK(fwrite("legacy",1,6,old)==6);CHECK(!fclose(old));
    }
    CHECK(fc_save_scan(&store,slots));
    for(unsigned i=0;i<FC_SAVE_SLOTS;++i) {
        CHECK(!slots[i].exists && !slots[i].valid);
        CHECK(fc_save_write(&store,i,&slots[i],1700000000+i,1000+i,60,&saved));
        slots[i]=saved;CHECK(saved.valid && saved.revision==1);
    }
    CHECK(checkpoint_save(host,snapshot,store.length));
    saved=slots[2];nes_emulate(true);
    CHECK(fc_save_write(&store,2,&saved,1700000060,2000,60,&newer));
    CHECK(newer.revision==2 && newer.bank!=saved.bank);
    CHECK(!fc_save_write(&store,2,&saved,1,1,60,&loaded) && store.error==ESTALE);
    CHECK(!fc_save_read(&store,2,&saved,&loaded) && store.error==ESTALE);
    CHECK(fc_save_read(&store,2,&newer,&loaded));CHECK(loaded.frames==2000);
    // Truncation of the new generation falls back to the old, valid generation.
    char path[256];CHECK(fc_save_path(&store,2,newer.bank,path,sizeof(path)));
    FILE *file=fopen(path,"wb");CHECK(file);CHECK(fwrite("partial",1,7,file)==7);CHECK(!fclose(file));
    CHECK(fc_save_scan(&store,slots));CHECK(fc_save_same(&saved,&slots[2]));
    CHECK(fc_save_read(&store,2,&saved,&loaded));CHECK(!memcmp(snapshot,scratch,store.length));
    // Fail at every bounded IO/yield step: old generation must survive each cut.
    unsigned interruptions=0;
    for(int cut=1;cut<110;++cut) {
        cancel_at=cut;calls=0;
        bool done=fc_save_write(&store,2,&saved,1700000060,2000,60,&loaded);
        cancel_at=-1;
        CHECK(fc_save_scan(&store,slots));CHECK(slots[2].valid);
        CHECK(fc_save_read(&store,2,&slots[2],&loaded));
        CHECK(loaded.revision==saved.revision || loaded.revision==saved.revision+1);
        if(loaded.revision==saved.revision+1) saved=loaded;
        if(!done) ++interruptions;
    }
    CHECK(interruptions>30);
    // Automatic updates/readback/cancellation use the same transaction safety,
    // but must leave every manual slot and retired file unchanged.
    fc_save_slot_t manual[FC_SAVE_MANUAL_SLOTS];
    memcpy(manual,slots,sizeof(manual));
    CHECK(fc_save_inspect_slot(&store,FC_SAVE_AUTO_SLOT,&saved));
    CHECK(saved.valid && saved.revision==1);
    for(int cut=1;cut<110;++cut) {
        cancel_at=cut;calls=0;
        fc_save_write(&store,FC_SAVE_AUTO_SLOT,&saved,1700000120,3600,60,&loaded);
        cancel_at=-1;
        CHECK(fc_save_inspect_slot(&store,FC_SAVE_AUTO_SLOT,&newer));CHECK(newer.valid);
        CHECK(newer.revision==saved.revision || newer.revision==saved.revision+1);
        CHECK(fc_save_read(&store,FC_SAVE_AUTO_SLOT,&newer,&loaded));
        saved=newer;
    }
    CHECK(fc_save_scan(&store,slots));
    for(unsigned i=0;i<FC_SAVE_MANUAL_SLOTS;++i)CHECK(fc_save_same(&manual[i],&slots[i]));
    // Production background path uses only 4 KiB scratch and immutable state.
    CHECK(checkpoint_save(host,snapshot,store.length));
    const uint32_t detached_crc=checkpoint_crc(snapshot,store.length);
    uint8_t io_scratch[4096];
    fc_save_store_t stream=store;stream.buffer=io_scratch;stream.validate=NULL;stream.capture=NULL;
    unsigned stream_interruptions=0;
    for(int cut=1;cut<150;++cut){
        cancel_at=cut;calls=0;
        if(!fc_save_write_snapshot(&stream,snapshot,1700000180,7200,60,&loaded))++stream_interruptions;
        cancel_at=-1;
        CHECK(checkpoint_crc(snapshot,store.length)==detached_crc);
        CHECK(fc_save_inspect_slot(&store,FC_SAVE_AUTO_SLOT,&newer));CHECK(newer.valid);
        CHECK(newer.revision==saved.revision||newer.revision==saved.revision+1);
        CHECK(fc_save_read(&store,FC_SAVE_AUTO_SLOT,&newer,&loaded));
        saved=newer;
    }
    CHECK(stream_interruptions>30);
    CHECK(fc_save_write_snapshot(&stream,snapshot,1700000240,10800,60,&loaded));
    CHECK(loaded.revision==saved.revision+1&&loaded.frames==10800);
    CHECK(fc_save_read(&store,FC_SAVE_AUTO_SLOT,&loaded,&newer));CHECK(!memcmp(snapshot,scratch,store.length));
    CHECK(fc_save_scan(&store,slots));
    for(unsigned i=0;i<FC_SAVE_MANUAL_SLOTS;++i)CHECK(fc_save_same(&manual[i],&slots[i]));
    for(unsigned i=0;i<2;++i) {
        char data[8]={0};FILE *old=fopen(legacy[i],"rb");CHECK(old);
        CHECK(fread(data,1,sizeof(data),old)==6 && !memcmp(data,"legacy",6));CHECK(!fclose(old));
    }
    CHECK(!fc_save_path(&store,FC_SAVE_SLOTS,0,automatic,sizeof(automatic)));
    // A different game's identity cannot read these files.
    store.rom[0]^=1;CHECK(fc_save_scan(&store,slots));
    for(unsigned i=0;i<FC_SAVE_SLOTS;++i) CHECK(!slots[i].exists);
    store.rom[0]^=1;
    finish();
}
static void cold_file(const char *directory,unsigned mapper,bool writing)
{
    start(mapper,128,mapper==245?0:256,false,false);nes_setvidbuf(video);
    fc_save_store_t s={0};s.directory=directory;s.buffer=scratch;s.length=checkpoint_size(host);
    s.validate=validate_test;s.capture=capture_test;s.proceed=proceed_test;
    vault_sha256_state hash;vault_sha256_init(&hash);
    for(size_t pos=0;pos<image_size;) {size_t n=image_size-pos;if(n>4096)n=4096;CHECK(!vault_sha256_process(&hash,image+pos,n));pos+=n;}
    CHECK(!vault_sha256_done(&hash,s.rom));
    fc_save_slot_t slots[FC_SAVE_SLOTS],result;CHECK(fc_save_scan(&s,slots));
    if(writing) {
        for(unsigned i=0;i<FC_SAVE_SLOTS;++i)CHECK(!slots[i].exists);
        for(unsigned i=0;i<3;++i)nes_emulate(true);
        host->mem->ram[123]=0xA7;host->mem->ram[124]=mapper;
        for(unsigned i=0;i<FC_SAVE_MANUAL_SLOTS;++i)CHECK(fc_save_write(&s,i,&slots[i],1700000000,4000+i,60,&result));
        CHECK(checkpoint_save(host,snapshot,s.length));uint8 io[4096];s.buffer=io;
        CHECK(fc_save_write_snapshot(&s,snapshot,1700000060,4500,60,&result));
    } else {
        for(unsigned i=0;i<FC_SAVE_SLOTS;++i) {
            CHECK(slots[i].exists&&slots[i].valid);
            CHECK(fc_save_read(&s,i,&slots[i],&result));CHECK(checkpoint_load(host,scratch,s.length));
            CHECK(host->mem->ram[123]==0xA7&&host->mem->ram[124]==(uint8)mapper);
            CHECK(result.frames==(i==FC_SAVE_AUTO_SLOT?4500:4000+i));
        }
    }
    finish();
}
int main(int argc,char **argv)
{
    CHECK(argc==2||argc==3);
    if(argc==3) {
        for(unsigned i=0;mappers[i];++i)cold_file(argv[1],mappers[i]->number,!strcmp(argv[2],"write"));
        printf("%u cold-process %s checks passed, 35 mapper identities and all four slots\n",checks,argv[2]);return 0;
    }
    skipped_scanlines();
    for(unsigned i=0;mappers[i];++i) { printf("checkpoint mapper %d\n",mappers[i]->number);fflush(stdout);exercise(mappers[i]->number); }
    transactions(argv[1]);
    printf("Checkpoint checks: %u passed; all 35 mappers replay/restart, 3 manual + independent auto, legacy isolation, stale/partial/cancel; allocations=%u remaining=%u\n",checks,allocations,live);
    return 0;
}
