/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** map024.c: Konami VRC6 mapper interface
**
*/

#include "nes/nes.h"

// VRC6a (mapper 24) register wiring. The original scanline-only mapper has
// been replaced by instruction-clock IRQs and bounded, timestamped audio.
// Hardware behavior cross-checked against NESdev VRC6/VRC IRQ and Mesen2.
typedef struct {
    uint8 control,low,high,phase,accumulator;
    unsigned timer;
} voice_t;
static struct {
    rom_t *cart;
    uint8 banking,chr[8],prg16,prg8;
    struct { uint8 latch,counter,control;int prescaler;bool asserted; } irq;
    voice_t voice[3];
    uint8 audio_control;
    unsigned audio_pending,sample_phase,frame_cycles,samples_per_frame;
    int32 dc;
    int16 samples[1024];
    unsigned read,write,count;
} vrc;

static unsigned period(const voice_t *v) {
    const unsigned shift=(vrc.audio_control&4)?8:(vrc.audio_control&2)?4:0;
    return ((((unsigned)(v->high&15)<<8)|v->low)>>shift)+1;
}
static void advance_audio(void) {
    unsigned cycles=vrc.audio_pending;vrc.audio_pending=0;
    if(!cycles || (vrc.audio_control&1)) return;
    for(unsigned ch=0;ch<3;++ch) {
        voice_t *v=&vrc.voice[ch];
        if(!(v->high&0x80)) continue;
        if(cycles<v->timer) {v->timer-=cycles;continue;}
        const unsigned left=cycles-v->timer,p=period(v);
        unsigned ticks=1+left/p;v->timer=p-left%p;
        if(ch<2) {v->phase=(v->phase+ticks)&15;continue;}
        // Complete saw periods have no history. At most 27 steps are needed,
        // even with frequency shift 8 and a timer period of one CPU cycle.
        if(ticks>=28) ticks=14+ticks%14;
        while(ticks--) {
            if(++v->phase==14) {v->phase=0;v->accumulator=0;}
            else if(!(v->phase&1)) v->accumulator+=v->control&63;
        }
    }
}
static int sample_audio(void) {
    unsigned level=0;
    for(unsigned i=0;i<2;++i) {
        const voice_t *v=&vrc.voice[i];
        if((v->high&0x80) && ((v->control&0x80) || v->phase<=((v->control>>4)&7))) level+=v->control&15;
    }
    if(vrc.voice[2].high&0x80) level+=vrc.voice[2].accumulator>>3;
    const int output=(int)level*128;
    vrc.dc+=((output*256)-vrc.dc)/1024; // Remove expansion chip DC before mixing.
    return output-vrc.dc/256;
}
static void map_clock(unsigned cycles) {
    if(!cycles) return;
    if(vrc.irq.control&2) {
        unsigned clocks=cycles;
        if(!(vrc.irq.control&4)) {
            vrc.irq.prescaler-=(int)cycles*3;clocks=0;
            while(vrc.irq.prescaler<=0) {vrc.irq.prescaler+=341;++clocks;}
        }
        while(clocks--) {
            if(vrc.irq.counter==255) {
                vrc.irq.counter=vrc.irq.latch;vrc.irq.asserted=true;
                nes6502_setmapperirq(true);
            } else ++vrc.irq.counter;
        }
    }
    vrc.audio_pending+=cycles;
    vrc.sample_phase+=cycles*vrc.samples_per_frame;
    if(vrc.sample_phase>=vrc.frame_cycles) {
        advance_audio();
        while(vrc.sample_phase>=vrc.frame_cycles) {
            vrc.sample_phase-=vrc.frame_cycles;
            if(vrc.count==1024) {vrc.read=(vrc.read+1)&1023;--vrc.count;}
            vrc.samples[vrc.write]=(int16)sample_audio();vrc.write=(vrc.write+1)&1023;++vrc.count;
        }
    }
}
static int audio_process(void) {
    if(!vrc.count) return 0;
    const int value=vrc.samples[vrc.read];vrc.read=(vrc.read+1)&1023;--vrc.count;return value;
}
static void audio_reset(void) {
    memset(vrc.voice,0,sizeof(vrc.voice));
    for(unsigned i=0;i<3;++i) vrc.voice[i].timer=1;
    vrc.audio_control=0;vrc.audio_pending=0;vrc.sample_phase=0;vrc.dc=0;
    vrc.read=vrc.write=vrc.count=0;
}
static const apuext_t expansion={audio_reset,audio_process};

static void map_banks(void) {
    const unsigned mode=vrc.banking&3;
    for(unsigned i=0;i<8;++i) {
        const bool pair=mode==1 || (mode>=2 && i>=4);
        const unsigned reg=mode==1?i/2:pair?4+(i-4)/2:i;
        unsigned bank=vrc.chr[reg];
        if(pair && (vrc.banking&0x20)) bank=(bank&0xfe)|(i&1);
        mmc_bankchr(1,i*1024,bank,CHR_ANY);
    }
    static const uint8 nt_register[8][4]={
        {6,6,7,7},{4,5,6,7},{6,7,6,7},{6,7,6,7},
        {6,7,6,7},{4,5,6,7},{6,6,7,7},{6,6,7,7}
    };
    // Special bit-5 combinations select fixed CIRAM mirroring, or paired
    // CHR-ROM nametables. Other combinations use the bank register low bit.
    const unsigned select=vrc.banking&0x2f;
    const int special=select==0x20 || select==0x27?0:
        select==0x23 || select==0x24?1:select==0x28 || select==0x2f?2:
        select==0x2b || select==0x2c?3:-1;
    static const uint8 ciram[4][4]={{0,1,0,1},{0,0,1,1},{0,0,0,0},{1,1,1,1}};
    const bool rom_nt=(vrc.banking&0x10) && vrc.cart->chr_rom_banks;
    for(unsigned i=0;i<4;++i) {
        unsigned bank=vrc.chr[nt_register[vrc.banking&7][i]];
        if(special>=0) {
            if(rom_nt) {
                const unsigned reg=(special==0 || special==2)?6+i/2:6+(i&1);
                bank=(vrc.chr[reg]&0xfe)|ciram[special][i];
            } else bank=ciram[special][i];
        }
        if(rom_nt) {
            const unsigned banks=vrc.cart->chr_rom_banks*8;
            ppu_setpage(8+i,vrc.cart->chr_rom+(bank%banks)*1024);
        } else ppu_setnametable(i,bank&1);
    }
    ppu_setreadonly((vrc.cart->chr_rom_banks?0xff:0)|(rom_nt?0xff00:0));
    if((vrc.banking&0x80) && vrc.cart->prg_ram_banks) mmc_bankprg(8,0x6000,0,PRG_RAM);
    else for(unsigned i=0;i<4;++i) mem_setpage((0x6000>>MEM_PAGESHIFT)+i,MEM_PAGE_NOT_MAPPED);
}
static uint8 ram_read(uint32 address) {
    return (vrc.banking&0x80) && vrc.cart->prg_ram_banks?vrc.cart->prg_ram[address&0x1fff]:0xff;
}
static void map_write(uint32 address,uint8 value) {
    if(address<0x8000) {
        if((vrc.banking&0x80) && vrc.cart->prg_ram_banks) vrc.cart->prg_ram[address&0x1fff]=value;
        return;
    }
    const unsigned reg=address&0xf003;
    if(reg==0x9003) {advance_audio();vrc.audio_control=value&7;return;}
    if(reg>=0x9000 && reg<=0xb002 && (reg&3)<3) {
        advance_audio();voice_t *v=&vrc.voice[(reg>>12)-9];
        if((reg&3)==0) v->control=value;
        else if((reg&3)==1) v->low=value;
        else {v->high=value;if(!(value&0x80)) {v->phase=0;v->accumulator=0;}}
        return;
    }
    if((reg&0xf000)==0x8000) {vrc.prg16=value&15;mmc_bankprg(16,0x8000,vrc.prg16,PRG_ROM);}
    else if((reg&0xf000)==0xc000) {vrc.prg8=value&31;mmc_bankprg(8,0xc000,vrc.prg8,PRG_ROM);}
    else if(reg==0xb003) {vrc.banking=value;map_banks();}
    else if((reg&0xf000)==0xd000 || (reg&0xf000)==0xe000) {
        vrc.chr[(reg&3)+((reg&0xf000)==0xe000?4:0)]=value;map_banks();
    } else if(reg==0xf000) vrc.irq.latch=value;
    else if(reg==0xf001) {
        vrc.irq.control=value&7;
        if(value&2) {vrc.irq.counter=vrc.irq.latch;vrc.irq.prescaler=341;}
        vrc.irq.asserted=false;nes6502_setmapperirq(false);
    } else if(reg==0xf002) {
        vrc.irq.control=(vrc.irq.control&5)|((vrc.irq.control&1)<<1);
        vrc.irq.asserted=false;nes6502_setmapperirq(false);
    }
}
static void map_init(rom_t *cart) {
    memset(&vrc,0,sizeof(vrc));vrc.cart=cart;
    nes_t *nes=nes_getptr();
    vrc.frame_cycles=nes->cycles_per_scanline*nes->scanlines_per_frame;
    vrc.samples_per_frame=nes->apu->samples_per_frame;
    if(!vrc.frame_cycles) vrc.frame_cycles=29868;
    if(!vrc.samples_per_frame) vrc.samples_per_frame=367;
    audio_reset();vrc.prg8=cart->prg_rom_banks>1?cart->prg_rom_banks-2:0;
    mmc_bankprg(16,0x8000,0,PRG_ROM);mmc_bankprg(8,0xc000,vrc.prg8,PRG_ROM);
    mmc_bankprg(8,0xe000,-1,PRG_ROM);map_banks();
    apu_setext(&expansion);nes6502_setcyclehook(map_clock);nes6502_setmapperirq(false);
}
static void map_checkpoint(checkpoint_io_t *io)
{
    CHECKPOINT_FIELD(io, vrc.banking);
    CHECKPOINT_FIELD(io, vrc.chr);
    CHECKPOINT_FIELD(io, vrc.prg16);
    CHECKPOINT_FIELD(io, vrc.prg8);
    CHECKPOINT_FIELD(io, vrc.irq.latch);
    CHECKPOINT_FIELD(io, vrc.irq.counter);
    CHECKPOINT_FIELD(io, vrc.irq.control);
    CHECKPOINT_FIELD(io, vrc.irq.prescaler);
    CHECKPOINT_FIELD(io, vrc.irq.asserted);
    CHECKPOINT_FIELD(io, vrc.voice);
    CHECKPOINT_FIELD(io, vrc.audio_control);
    CHECKPOINT_FIELD(io, vrc.audio_pending);
    CHECKPOINT_FIELD(io, vrc.sample_phase);
    CHECKPOINT_FIELD(io, vrc.frame_cycles);
    CHECKPOINT_FIELD(io, vrc.samples_per_frame);
    CHECKPOINT_FIELD(io, vrc.dc);
    CHECKPOINT_FIELD(io, vrc.samples);
    CHECKPOINT_FIELD(io, vrc.read);
    CHECKPOINT_FIELD(io, vrc.write);
    CHECKPOINT_FIELD(io, vrc.count);
}

mapintf_t map24_intf={
    .checkpoint = map_checkpoint,
    .number=24,.name="Konami VRC6a",.init=map_init,
    .mem_read={{0x6000,0x7fff,ram_read},LAST_MEMORY_HANDLER},
    .mem_write={{0x6000,0xffff,map_write},LAST_MEMORY_HANDLER},
};

