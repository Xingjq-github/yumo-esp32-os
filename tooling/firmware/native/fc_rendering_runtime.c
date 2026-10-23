/* Real Nofrendo PPU with generated CHR/OAM, no commercial ROM or hardware. */
#define main mapper_regressions_main
#include "mmc3_variants_runtime.c"
#undef main

static void scene(void)
{
    start(0, 4, 0, false, false);
    nes_setvidbuf(video);
    memset(host->cart->chr_ram, 0, 8192);
    memset(host->ppu->nametab, 0, 4096);
    memset(host->ppu->oam, 255, 256);
    for (unsigned i = 0; i < 32; ++i)
        host->ppu->palette[i] = i % 4 ? i : 0x80;
    const uint8 one[8] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0};
    const uint8 letter[8] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0};
    memcpy(host->cart->chr_ram + 16, one, 8);
    memcpy(host->cart->chr_ram + 32, letter, 8);
    memset(host->cart->chr_ram + 48, 255, 8);
    ppu_write(PPU_CTRL0, 0);
    ppu_write(PPU_CTRL1, 0x1E);
}

static void sprite(unsigned n, unsigned x, unsigned tile, unsigned attr)
{
    host->ppu->oam[n*4] = 79;
    host->ppu->oam[n*4+1] = tile;
    host->ppu->oam[n*4+2] = attr;
    host->ppu->oam[n*4+3] = x;
}

static void render(bool draw)
{
    for (unsigned y = 0; y < 240; ++y) {
        ppu_renderline(video, y, draw);
        ppu_endline();
    }
    ppu_renderline(video, host->scanlines_per_frame - 1, false);
}

static unsigned glyph_pixels(void)
{
    unsigned count = 0;
    for (unsigned y = 80; y < 88; ++y)
        for (unsigned x = 80; x < 96; ++x)
            count += (NES_SCREEN_GETPTR(video, x, y)[0] & 63) == 17;
    return count;
}

static void layers(void)
{
    scene();
    sprite(0, 80, 3, 0); sprite(1, 80, 3, 1);
    render(true); CHECK((*NES_SCREEN_GETPTR(video,80,80) & 63) == 17);
    host->ppu->oam[2] = 0x20;
    render(true); CHECK((*NES_SCREEN_GETPTR(video,80,80) & 63) == 17);
    for (unsigned y = 0; y < 30; ++y)
        memset(host->ppu->nametab + y*32, 3, 32);
    render(true); CHECK((*NES_SCREEN_GETPTR(video,80,80) & 63) == 1);
    host->ppu->oam[2] = 0;
    render(true); CHECK((*NES_SCREEN_GETPTR(video,80,80) & 63) == 17);
    finish();
    // OAM rotation can put the useful glyph on alternating source frames.
    scene(); CHECK(ppu_getopt(PPU_LIMIT_SPRITES));
    for (unsigned n = 0; n < 8; ++n) sprite(n, n*8, 0, 0);
    sprite(8, 80, 1, 0); render(true); CHECK(glyph_pixels() == 0);
    sprite(0, 80, 1, 0); render(true); CHECK(glyph_pixels() > 10);
    finish();
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    layers();
    scene();
    FILE *out = fopen(argv[1], "wb"); CHECK(out);
    for (unsigned phase = 0; phase < 2; ++phase) {
        memset(host->ppu->oam, 255, 256);
        if (phase) { sprite(0, 80, 1, 0); sprite(1, 88, 2, 0); }
        render(true);
        CHECK((glyph_pixels() > 10) == !!phase);
        CHECK(fwrite(video, 1, sizeof(video), out) == sizeof(video));
    }
    CHECK(!fclose(out));
    finish();
    printf("{\"passed\":true,\"checks\":%u,\"fixture_frames\":2,\"outstanding_allocations\":%u}\n", checks, live);
    return 0;
}
