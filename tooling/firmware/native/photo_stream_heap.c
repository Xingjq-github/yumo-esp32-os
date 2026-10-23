// Bounded PSRAM substitute. Used by actual loader and codec allocations.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef union { struct {size_t size;uint64_t magic;} h;max_align_t align; } Block;
static size_t used,peak,live,attempts,largest=SIZE_MAX,capacity=2800*1024,fail_at=SIZE_MAX;
static const uint64_t magic=0x70686f746f31364dULL;
static void check(Block *b) {
    if(b->h.magic!=magic) abort();
    const unsigned char *tail=(const unsigned char *)(b+1)+b->h.size;
    for(unsigned i=0;i<16;++i) if(tail[i]!=0xa5) abort();
}
void probe_config(size_t cap,size_t block,long fail) {
    capacity=cap;largest=block?block:SIZE_MAX;attempts=0;peak=used;
    fail_at=fail<0?SIZE_MAX:(size_t)fail;
}
size_t probe_live(void){return live;}
size_t probe_used(void){return used;}
size_t probe_peak(void){return peak;}
size_t probe_attempts(void){return attempts;}
void *probe_malloc(size_t n) {
    if(attempts++==fail_at || n>largest || used>capacity || n>capacity-used) {errno=ENOMEM;return NULL;}
    Block *b=malloc(sizeof(*b)+n+16);if(!b) return NULL;
    b->h.size=n;b->h.magic=magic;memset((unsigned char *)(b+1)+n,0xa5,16);
    used+=n;++live;if(used>peak)peak=used;return b+1;
}
void probe_free(void *p) {
    if(!p)return;Block *b=(Block *)p-1;check(b);
    used-=b->h.size;--live;b->h.magic=0;free(b);
}
void *probe_calloc(size_t n,size_t size) {
    if(size && n>SIZE_MAX/size)return NULL;
    void *p=probe_malloc(n*size);if(p)memset(p,0,n*size);return p;
}
void *probe_realloc(void *p,size_t n) {
    if(!p)return probe_malloc(n);if(!n){probe_free(p);return NULL;}
    Block *b=(Block *)p-1;check(b);size_t old=b->h.size;
    if(attempts++==fail_at || (n>old && (n>largest || used>capacity || n-old>capacity-used)))return NULL;
    b=realloc(b,sizeof(*b)+n+16);if(!b)return NULL;
    b->h.size=n;used=used-old+n;if(used>peak)peak=used;
    memset((unsigned char *)(b+1)+n,0xa5,16);return b+1;
}
void *heap_caps_malloc(size_t n,unsigned caps){(void)caps;return probe_malloc(n);}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps){(void)caps;return probe_calloc(n,size);}
void *heap_caps_realloc(void *p,size_t n,unsigned caps){(void)caps;return probe_realloc(p,n);}
void heap_caps_free(void *p){probe_free(p);}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return used<capacity?capacity-used:0;}
size_t heap_caps_get_largest_free_block(unsigned caps){size_t free=heap_caps_get_free_size(caps);return free<largest?free:largest;}
