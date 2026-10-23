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
** nes/rom.c: NES file loader (ROM, FDS, NSF, SAV, etc)
**
*/


/* JoyCode port: bounded iNES loader, no database header corrections. */
#include "nes.h"
#include "ines.h"
static rom_t rom;
rom_t *rom_loadmem(uint8 *data,size_t size) {
    rom_free();
    ines_info_t info={0};
    if(ines_parse(data,size,&info)!=INES_OK) return NULL;
    const size_t offset=info.offset,prg=info.prg,chr=info.chr;
    rom.type=ROM_TYPE_INES;rom.data_ptr=data;rom.data_len=size;
    rom.mapper_number=info.mapper;
    rom.system=info.pal?SYS_NES_PAL:SYS_NES_NTSC;
    rom.vertical=(data[6]&ROM_FLAG_VERTICAL)!=0;rom.fourscreen=(data[6]&ROM_FLAG_FOURSCREEN)!=0;
    rom.battery=(data[6]&ROM_FLAG_BATTERY)!=0;
    rom.mirroring=rom.fourscreen?PPU_MIRROR_FOUR:rom.vertical?PPU_MIRROR_VERT:PPU_MIRROR_HORI;
    rom.prg_rom_banks=prg/ROM_PRG_BANK_SIZE;rom.chr_rom_banks=chr/ROM_CHR_BANK_SIZE;
    rom.prg_ram_banks=1;rom.chr_ram_banks=1;
    rom.prg_rom=data+offset;rom.chr_rom=chr?data+offset+prg:NULL;
    rom.trainer=(data[6]&ROM_FLAG_TRAINER)?data+16:NULL;
    rom.prg_ram=calloc(1,ROM_PRG_BANK_SIZE);rom.chr_ram=calloc(1,ROM_CHR_BANK_SIZE);
    if(!rom.prg_ram || !rom.chr_ram) {rom_free();return NULL;}
    return &rom;
}
rom_t *rom_loadfile(const char *filename) {
    (void)filename;return NULL; // App owns chunked SD reads and cancellation.
}
void rom_free(void) {
    if(rom.free_data_ptr) free(rom.data_ptr);
    free(rom.prg_ram);free(rom.chr_ram);free(rom.filename);
    memset(&rom,0,sizeof(rom));
}
void rom_savesram(const char *filename) {(void)filename;}
void rom_loadsram(const char *filename) {(void)filename;}
