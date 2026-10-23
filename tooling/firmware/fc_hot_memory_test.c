#include "nes/nes.h"
_Static_assert(sizeof(mem_t)<=4096,"RAM/page tables must fit the hot allocation cap");
_Static_assert(sizeof(ppu_t)<=4096,"PPU context must fit the hot allocation cap");
_Static_assert(sizeof(apu_t)<=4096,"APU context must fit the hot allocation cap");
_Static_assert(sizeof(mem_t)+sizeof(ppu_t)+sizeof(apu_t)+4096<=12*1024,"Hot allocation budget exceeded");
