/* Run the production NES engine with synthetic, freely generated cartridges.
 * Only allocation is instrumented; CPU, PPU, bus, loader and mappers are real. */
#include "mappers/mappers.h"
#include "nes/ines.h"
#undef malloc
#undef calloc
#undef free
void free(void *ptr);

static unsigned checks, allocations, peak, live;
static size_t live_bytes, peak_bytes;
static struct { uint8 *ptr; size_t size; } tracked[64];
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void *fc_alloc(size_t size)
{
    uint8 *p = malloc(size + 32); CHECK(p);
    memset(p, 0xA6, 16); memset(p + 16 + size, 0xA6, 16);
    for (unsigned i = 0; i < 64; ++i) if (!tracked[i].ptr) {
        tracked[i].ptr = p + 16; tracked[i].size = size;
        ++allocations; ++live; live_bytes += size;
        if (live > peak) peak = live;
        if (live_bytes > peak_bytes) peak_bytes = live_bytes;
        return p + 16;
    }
    CHECK(0); return NULL;
}
void *fc_calloc(size_t count, size_t size) { void *p = fc_alloc(count * size); memset(p, 0, count * size); return p; }
void *fc_calloc_hot(size_t count, size_t size) { return fc_calloc(count, size); }
void probe_free(void *ptr)
{
    if (!ptr) return;
    for (unsigned i = 0; i < 64; ++i) if (tracked[i].ptr == ptr) {
        uint8 *p = ptr; const size_t size = tracked[i].size;
        for (unsigned n = 0; n < 16; ++n) { CHECK(p[(int)n - 16] == 0xA6); CHECK(p[size + n] == 0xA6); }
        tracked[i].ptr = NULL; --live; live_bytes -= size; free(p - 16); return;
    }
    CHECK(0);
}
uint16_t fc_color(unsigned index) { return index; }

static uint8 image[2 * 1024 * 1024 + 528], video[NES_SCREEN_PITCH * 240];
static size_t image_size;
static nes_t *host;
static unsigned rom_hash;
static unsigned digest(void) { unsigned h = 2166136261u; for (size_t i = 0; i < image_size; ++i) h = (h ^ image[i]) * 16777619u; return h; }
static void start(unsigned mapper, unsigned prg, unsigned chr, bool four, bool nes2)
{
    CHECK(!live);
    image_size = 16 + prg * 8192 + chr * 1024;
    memset(image, 0, image_size);
    memcpy(image, "NES\032", 4); image[4] = prg / 2; image[5] = chr / 8;
    image[6] = ((mapper & 15) << 4) | (four ? 8 : 0);
    image[7] = (mapper & 0xF0) | (nes2 ? 8 : 0);
    if (nes2) { image[10] = 7; image[11] = mapper == 74 && chr ? 5 : chr ? 0 : 7; }
    for (unsigned bank = 0; bank < prg; ++bank) {
        uint8 *p = image + 16 + bank * 8192;
        memset(p, bank, 8192);
        p[0] = 0x4C; p[1] = 0; p[2] = 0xE0; // JMP $E000
        p[8186] = 0; p[8187] = 0xE0; p[8188] = 0; p[8189] = 0xE0;
        p[8190] = 0; p[8191] = 0xE1; // IRQ $E100
    }
    for (unsigned bank = 0; bank < chr; ++bank) memset(image + 16 + prg * 8192 + bank * 1024, bank, 1024);
    rom_hash = digest();
    host = nes_init(SYS_DETECT, 22050, false, NULL); CHECK(host);
    rom_t *cart = rom_loadmem(image, image_size); CHECK(cart);
    CHECK(nes_insertcart(cart) == 0); CHECK(host->mapper->number == mapper);
    CHECK(!host->cpu->mapper_irq);
}
static void finish(void)
{
    nes_shutdown(); CHECK(!live && !live_bytes); CHECK(digest() == rom_hash);
    CHECK(!host->cpu->mapper_irq);
}
static void reg(unsigned r, uint8 value, uint8 mode) { mem_putbyte(0x8000, r | mode); mem_putbyte(0x8001, value); }
static unsigned prg(unsigned slot) { return mem_getbyte(0x8100 + slot * 8192); }
static unsigned chr(unsigned page) { return *ppu_getpage(page); }
static void vwrite(unsigned address, uint8 value)
{
    ppu_write(PPU_CTRL1, 0);
    (void)ppu_read(PPU_STAT);
    ppu_write(PPU_VADDR, address >> 8); ppu_write(PPU_VADDR, address & 255); ppu_write(PPU_VDATA, value);
}
static void parser(void)
{
    ines_info_t info;
    for (unsigned m = 0; m < 4096; ++m) {
        uint8 h[16] = {'N','E','S',0x1A,1,1};
        h[6] = (m & 15) << 4; h[7] = (m & 0xF0) | 8; h[8] = m >> 8;
        bool present = false; for (unsigned i = 0; mappers[i]; ++i) if (mappers[i]->number == m) present = true;
        ines_error_t error = ines_parse(h, 24592, &info);
        if ((error == INES_OK) != present) fprintf(stderr,"mapper=%u error=%u parsed=%u registered=%u\n",m,error,info.mapper,present);
        CHECK((error == INES_OK) == present);
    }
    uint8 h[16] = {'N','E','S',0x1A,32,32,0xA0,0x48,0,0,7,5};
    size_t size = 16 + 512 * 1024 + 256 * 1024;
    CHECK(ines_parse(h, size, &info) == INES_OK && info.mapper == 74);
    h[11] = 6; CHECK(ines_parse(h, size, &info) == INES_RAM);
    h[11] = 5; h[8] = 0x10; CHECK(ines_parse(h, size, &info) == INES_SUBMAPPER);
    h[8] = 0; CHECK(ines_parse(h, size - 1, &info) == INES_TRUNCATED);
    h[6] = 0x40; h[7] = 8; CHECK(ines_parse(h, size, &info) == INES_RAM); // Mapper4 unchanged.
    h[6] = 0x50; h[7] = 0xF8; h[5] = 0; h[4] = 64; h[11] = 7;
    CHECK(ines_parse(h, 16 + 1024 * 1024, &info) == INES_OK && info.mapper == 245);
    h[11] = 8; CHECK(ines_parse(h, 16 + 1024 * 1024, &info) == INES_RAM);
}
static void mapper74(void)
{
    start(74, 64, 256, false, true);
    CHECK(prg(0) == 0 && prg(1) == 1 && prg(2) == 62 && prg(3) == 63);
    unsigned before = allocations;
    for (unsigned value = 0; value < 256; ++value) {
        reg(6, value, 0); reg(7, 255 - value, 0);
        CHECK(prg(0) == (value & 63) && prg(1) == ((255 - value) & 63));
        mem_putbyte(0x8000, 0x46);
        CHECK(prg(0) == 62 && prg(2) == (value & 63));
        mem_putbyte(0x8000, 6); CHECK(prg(0) == (value & 63) && prg(2) == 62);
    }
    reg(0, 9, 0); // R0 is even-aligned and maps the two RAM banks.
    vwrite(0, 0xA5); vwrite(0x400, 0x5A);
    CHECK(chr(0) == 0xA5 && chr(1) == 0x5A);
    reg(2, 8, 0); reg(3, 9, 0);
    CHECK(ppu_getpage(4) == ppu_getpage(0) && ppu_getpage(5) == ppu_getpage(1));
    vwrite(0x1000, 0x3C); CHECK(chr(0) == 0x3C);
    reg(0, 0, 0); CHECK(chr(0) == 0 && chr(1) == 1);
    vwrite(0, 0xEE); CHECK(chr(0) == 0); // Real PPU $2007 must not corrupt ROM.
    reg(0, 8, 0); CHECK(chr(0) == 0x3C && chr(1) == 0x5A);
    for (unsigned mode = 0; mode < 2; ++mode) for (unsigned bank = 0; bank < 256; ++bank) {
        reg(5, bank, mode ? 0x80 : 0);
        unsigned page = 7 ^ (mode ? 4 : 0);
        uint8 *expected = bank == 8 || bank == 9 ? host->cart->chr_ram + (bank - 8) * 1024 : host->cart->chr_rom + bank * 1024;
        CHECK(ppu_getpage(page) == expected);
        if (bank != 8 && bank != 9) { vwrite(page * 1024, bank ^ 255); CHECK(chr(page) == bank); }
    }
    CHECK(allocations == before && host->cart->chr_rom_banks == 32 && host->cart->chr_ram_banks == 1);
    finish();
}
static void mapper245(void)
{
    for (unsigned count = 16; count <= 128; count *= 2) {
        start(245, count, 0, false, count == 128);
        for (unsigned i = 0; i < 8; ++i) vwrite(i * 1024, 0x30 + i);
        unsigned before = allocations;
        for (unsigned outer = 0; outer < 2; ++outer) for (unsigned mode = 0; mode < 4; ++mode) {
            uint8 command = mode << 6;
            reg(0, outer * 2, command);
            unsigned last = count >= 64 ? (63 | outer * 64) % count : count - 1;
            for (unsigned bank = 0; bank < 256; ++bank) {
                reg(6, bank, command); reg(7, 255 - bank, command);
                CHECK(prg((mode & 1) ? 2 : 0) == (((bank & 63) | outer * 64) % count));
                CHECK(prg(1) == ((((255 - bank) & 63) | outer * 64) % count));
                CHECK(prg((mode & 1) ? 0 : 2) == last - 1 && prg(3) == last);
            }
            // Other CHR registers must not change the PRG outer bit or RAM layout.
            for (unsigned r = 1; r < 6; ++r) reg(r, 255 - outer * 2, command);
            CHECK(prg(3) == last);
            for (unsigned i = 0; i < 8; ++i) CHECK(chr(i) == 0x30 + (i ^ ((mode & 2) ? 4 : 0)));
        }
        CHECK(allocations == before); finish();
    }
    // A ROM-equipped board retains ordinary MMC3 CHR banking and protection.
    start(245, 128, 32, false, false); reg(2, 21, 0); CHECK(chr(4) == 21);
    vwrite(0x1000, 0xEE); CHECK(chr(4) == 21); finish();
}
static void shared(unsigned mapper)
{
    start(mapper, 64, mapper == 74 ? 256 : 0, false, false);
    mem_putbyte(0x6001, 0x55); CHECK(mem_getbyte(0x6001) == 0x55);
    mem_putbyte(0xA001, 0xC0); mem_putbyte(0x6001, 0xAA); CHECK(mem_getbyte(0x6001) == 0x55);
    mem_putbyte(0xA001, 0); mem_putbyte(0x6001, 0xAA); CHECK(mem_getbyte(0x6001) == 255);
    mem_putbyte(0xA001, 0x80); CHECK(mem_getbyte(0x6001) == 0x55);
    mem_putbyte(0xA000, 0); CHECK(ppu_getpage(8) == ppu_getpage(10) && ppu_getpage(8) != ppu_getpage(9));
    mem_putbyte(0xA000, 1); CHECK(ppu_getpage(8) == ppu_getpage(9) && ppu_getpage(8) != ppu_getpage(10));
    ppu_write(PPU_CTRL1, 0x18); host->scanline = 10;
    mem_putbyte(0xC000, 2); mem_putbyte(0xC001, 0); mem_putbyte(0xE001, 0);
    host->mapper->hblank(host); CHECK(!host->cpu->mapper_irq);
    host->mapper->hblank(host); CHECK(!host->cpu->mapper_irq);
    host->mapper->hblank(host); CHECK(host->cpu->mapper_irq);
    host->cpu->int_pending = true; mem_putbyte(0xE000, 0);
    CHECK(!host->cpu->mapper_irq && host->cpu->int_pending); host->cpu->int_pending = false;
    mem_putbyte(0xC000, 0); mem_putbyte(0xC001, 0); mem_putbyte(0xE001, 0);
    for (unsigned line = 240; line < 261; ++line) { host->scanline = line; host->mapper->hblank(host); CHECK(!host->cpu->mapper_irq); }
    host->scanline = 261; host->mapper->hblank(host); CHECK(host->cpu->mapper_irq);
    // IRQ is a persistent CPU line; the real 6502 enters the IRQ vector.
    host->cpu->p_reg = R_FLAG; host->cpu->s_reg = 0xFF; host->cpu->burn_cycles = 0;
    CHECK(nes6502_execute(7) == 7 && host->cpu->pc_reg == 0xE100 && host->cpu->s_reg == 0xFC);
    CHECK(host->cpu->mapper_irq);
    mem_putbyte(0xE000, 0); CHECK(!host->cpu->mapper_irq);
    ppu_write(PPU_CTRL1, 0); mem_putbyte(0xE001, 0); host->scanline = 30;
    host->mapper->hblank(host); CHECK(!host->cpu->mapper_irq);
    ppu_write(PPU_CTRL1, 0x18); host->mapper->hblank(host); CHECK(host->cpu->mapper_irq);
    // State restoration must restore mappings, mirroring, RAM control and IRQ.
    uint8 state[512]; uint8 *saved_prg[4], *saved_chr[8];
    reg(0, 10, 0xC0); reg(6, 17, 0xC0); reg(2, 9, 0xC0);
    for (unsigned i = 0; i < 4; ++i) saved_prg[i] = mem_getpage(16 + i * 4);
    for (unsigned i = 0; i < 8; ++i) saved_chr[i] = ppu_getpage(i);
    host->mapper->get_state(state); nes_reset(false); CHECK(!host->cpu->mapper_irq);
    host->mapper->set_state(state); CHECK(host->cpu->mapper_irq);
    for (unsigned i = 0; i < 4; ++i) CHECK(mem_getpage(16 + i * 4) == saved_prg[i]);
    for (unsigned i = 0; i < 8; ++i) CHECK(ppu_getpage(i) == saved_chr[i]);
    CHECK(mem_getbyte(0x6001) == 0x55);
    nes_reset(true); CHECK(!host->cpu->mapper_irq && mem_getbyte(0x6001) == 0);
    unsigned before = allocations; nes_setvidbuf(video); ppu_write(PPU_CTRL1, 0x18);
    // Drawn and skipped frames both execute the production scanline path.
    for (unsigned frame = 0; frame < 120; ++frame) nes_emulate(frame & 1);
    CHECK(allocations == before && !host->cpu->jammed);
    finish();
    start(mapper, 64, mapper == 74 ? 256 : 0, true, false);
    mem_putbyte(0xA000, 1); CHECK(ppu_getpage(8) != ppu_getpage(9) && ppu_getpage(8) != ppu_getpage(10) && ppu_getpage(8) != ppu_getpage(11));
    finish();
}
int main(void)
{
    parser(); mapper74(); mapper245(); shared(74); shared(245);
    // Repeatedly switch between new and established mapper implementations.
    const unsigned sequence[] = {74, 245, 4, 24, 0};
    for (unsigned round = 0; round < 20; ++round) for (unsigned i = 0; i < 5; ++i) {
        unsigned m = sequence[i]; start(m, 64, m == 245 || m == 0 ? 0 : 32, false, false);
        if (m == 74 || m == 245) { ppu_write(PPU_CTRL1, 0x18); host->scanline = 20; mem_putbyte(0xE001, 0); host->mapper->hblank(host); CHECK(host->cpu->mapper_irq); }
        finish();
    }
    printf("MMC3 native checks: %u passed; allocations=%u; peak_live=%u; peak_bytes=%zu; remaining=%u\n", checks, allocations, peak, peak_bytes, live);
    return 0;
}
