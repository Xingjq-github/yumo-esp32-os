#include <stdlib.h>
#include <stdint.h>
#include <string.h>
// Windows x64 setjmp saves XMM registers and requires 16-byte heap alignment.
typedef union {size_t size;long double alignment;void *pointer;char padding[16];} Block;
static size_t live,bytes,peak,allocations;
static long fail_after=-1;
void probe_begin(long n) {fail_after=n;peak=bytes;allocations=0;}
size_t probe_live(void) {return live;}
size_t probe_peak(void) {return peak;}
size_t probe_allocations(void) {return allocations;}
void *probe_malloc(size_t size) {
    if(fail_after==0) return NULL;
    if(fail_after>0) --fail_after;
    if(size>SIZE_MAX-sizeof(Block)) return NULL;
    Block *p=malloc(sizeof(*p)+size);if(!p) return NULL;
    p->size=size;++live;bytes+=size;++allocations;if(bytes>peak) peak=bytes;return p+1;
}
void probe_free(void *memory) {
    if(!memory) return;Block *p=(Block *)memory-1;--live;bytes-=p->size;free(p);
}
void *probe_calloc(size_t n,size_t size) {
    if(size && n>SIZE_MAX/size) return NULL;
    void *p=probe_malloc(n*size);if(p) memset(p,0,n*size);return p;
}
