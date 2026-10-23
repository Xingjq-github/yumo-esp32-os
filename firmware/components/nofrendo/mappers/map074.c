/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
** Library General Public License for more details. To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
** Waixing MMC3 variants 74 and 245. Separate from the legacy mapper 4
** path so its game-specific timing remains unchanged. Register wiring
** follows Mesen2 MMC3_ChrRam / MMC3_245. No ROM copies or allocations.
*/
#include "nes/nes.h"

static struct {
    uint8 banks[8], command, mirror, ram_control;
    uint8 counter, latch, reload, enabled, asserted;
} wx;

/* The engine owns cartridge memory; no pointer survives a game in this state. */
static rom_t *cartridge(void) { return nes_getptr()->cart; }

static void sync_prg(void)
{
    rom_t *cart = cartridge();
    const bool large = cart->mapper_number == 245;
    const unsigned outer = large ? ((wx.banks[0] & 2) << 5) : 0;
    const int last = large && cart->prg_rom_banks >= 64 ? (63 | outer) : cart->prg_rom_banks - 1;
    const unsigned selected = (wx.command & 0x40) ? 0xC000 : 0x8000;
    mmc_bankrom(8, selected, (wx.banks[6] & 0x3F) | outer);
    mmc_bankrom(8, 0xA000, (wx.banks[7] & 0x3F) | outer);
    mmc_bankrom(8, selected ^ 0x4000, last - 1);
    mmc_bankrom(8, 0xE000, last);
}

static void sync_chr(void)
{
    rom_t *cart = cartridge();
    const unsigned invert = (wx.command & 0x80) ? 4 : 0;
    const uint8 banks[8] = {
        wx.banks[0] & 0xFE, wx.banks[0] | 1,
        wx.banks[1] & 0xFE, wx.banks[1] | 1,
        wx.banks[2], wx.banks[3], wx.banks[4], wx.banks[5]
    };
    uint16 readonly = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned page = i ^ invert;
        unsigned bank = banks[i];
        uint8 *base = cart->chr_rom_banks ? CHR_ROM : CHR_RAM;
        if (cart->mapper_number == 74 && (bank == 8 || bank == 9)) {
            /* Only these two banks select the board's 2KB CHR-RAM. */
            base = CHR_RAM;
            bank -= 8;
        } else if (cart->mapper_number == 245 && !cart->chr_rom_banks) {
            /* CHR bank lines feed PRG selection; RAM is fixed within each
             * 4KB half, with the MMC3 CHR inversion bit still connected. */
            bank = i;
        }
        mmc_bankchr(1, page * 1024, bank, base);
        if (base == CHR_ROM) readonly |= 1u << page;
    }
    ppu_setreadonly(readonly);
}

static void sync_mirror(void)
{
    ppu_setmirroring(cartridge()->fourscreen ? PPU_MIRROR_FOUR :
        (wx.mirror & 1) ? PPU_MIRROR_HORI : PPU_MIRROR_VERT);
}

static uint8 ram_read(uint32 address)
{
    rom_t *cart = cartridge();
    return (wx.ram_control & 0x80) && cart->prg_ram ? cart->prg_ram[address & 0x1FFF] : 0xFF;
}

static void map_write(uint32 address, uint8 value)
{
    if (address < 0x8000) {
        rom_t *cart = cartridge();
        if ((wx.ram_control & 0xC0) == 0x80 && cart->prg_ram)
            cart->prg_ram[address & 0x1FFF] = value;
        return;
    }
    switch (address & 0xE001) {
    case 0x8000: {
        const uint8 changed = wx.command ^ value;
        wx.command = value;
        if (changed & 0x40) sync_prg();
        if (changed & 0x80) sync_chr();
        break;
    }
    case 0x8001: {
        const unsigned reg = wx.command & 7;
        wx.banks[reg] = reg < 2 ? value & 0xFE : value;
        if (reg < 6) sync_chr();
        if (reg >= 6 || (reg == 0 && cartridge()->mapper_number == 245)) sync_prg();
        break;
    }
    case 0xA000: wx.mirror = value; sync_mirror(); break;
    case 0xA001: wx.ram_control = value; break;
    case 0xC000: wx.latch = value; break;
    case 0xC001: wx.counter = 0; wx.reload = true; break;
    case 0xE000:
        wx.enabled = wx.asserted = false;
        nes6502_setmapperirq(false);
        break;
    case 0xE001: wx.enabled = true; break;
    }
}

static void map_hblank(nes_t *nes)
{
    /* The scanline renderer does not expose individual PPU A12 edges.
     * Count rendered lines (including pre-render), never post-render/vblank.
     * This still runs when the display drops a frame for performance. */
    if ((nes->scanline >= 240 && nes->scanline != nes->scanlines_per_frame - 1) || !ppu_enabled()) return;
    if (wx.counter == 0 || wx.reload) wx.counter = wx.latch;
    else --wx.counter;
    wx.reload = false;
    if (wx.enabled && wx.counter == 0) {
        wx.asserted = true;
        nes6502_setmapperirq(true);
    }
}

static void map_getstate(uint8 *state) { memcpy(state, &wx, sizeof(wx)); }
static void map_setstate(uint8 *state)
{
    memcpy(&wx, state, sizeof(wx));
    sync_prg(); sync_chr(); sync_mirror();
    nes6502_setmapperirq(wx.asserted && wx.enabled);
}

static void map_init(rom_t *cart)
{
    static const uint8 initial[8] = {0, 2, 4, 5, 6, 7, 0, 1};
    memset(&wx, 0, sizeof(wx));
    memcpy(wx.banks, initial, sizeof(initial));
    wx.ram_control = 0x80;
    wx.mirror = cart->mirroring == PPU_MIRROR_HORI;
    nes6502_setmapperirq(false);
    sync_prg(); sync_chr(); sync_mirror();
}

static void map_checkpoint(checkpoint_io_t *io)
{
    CHECKPOINT_FIELD(io, wx);
}

mapintf_t map74_intf = {
    .checkpoint = map_checkpoint,
    .number = 74, .name = "Waixing MMC3 / CHR-RAM",
    .init = map_init, .hblank = map_hblank,
    .get_state = map_getstate, .set_state = map_setstate,
    .mem_read = {{0x6000, 0x7FFF, ram_read}},
    .mem_write = {{0x6000, 0xFFFF, map_write}},
};

mapintf_t map245_intf = {
    .checkpoint = map_checkpoint,
    .number = 245, .name = "Waixing MMC3 / large PRG",
    .init = map_init, .hblank = map_hblank,
    .get_state = map_getstate, .set_state = map_setstate,
    .mem_read = {{0x6000, 0x7FFF, ram_read}},
    .mem_write = {{0x6000, 0xFFFF, map_write}},
};
