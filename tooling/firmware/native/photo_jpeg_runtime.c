#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "photo_jpeg.h"
void probe_begin(long n);
size_t probe_live(void);
size_t probe_peak(void);
size_t probe_allocations(void);
static unsigned calls,cancel_at;
static int cancelled(void *p) {(void)p;return cancel_at && ++calls>=cancel_at;}
int main(int argc,char **argv) {
    if(argc!=7) return 2;
    FILE *file=fopen(argv[1],"rb");if(!file) return 3;
    fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);
    uint8_t *input=malloc(length);if(!input) return 4;
    if(fread(input,1,length,file)!=(size_t)length) return 5;fclose(file);
    unsigned ow=(unsigned)atoi(argv[3]),oh=(unsigned)atoi(argv[4]);
    if((size_t)ow*oh>1024*1024 || ow>8192 || oh>8192) return 6;
    uint8_t *pixels=ow && oh?malloc((size_t)ow*oh*3+64):NULL;
    if(pixels) memset(pixels,0xa5,(size_t)ow*oh*3+64);
    cancel_at=(unsigned)atoi(argv[5]);
    probe_begin(atol(argv[6]));const size_t before=probe_live();
    uint16_t width=0,height=0;int progressive=0;
    int result=photo_jpeg_read(input,length,pixels,ow,oh,&width,&height,&progressive,cancelled,NULL);
    if(before!=probe_live()) {fprintf(stderr,"decoder allocation leak\n");return 7;}
    if(pixels) for(unsigned i=0;i<64;++i) if(pixels[(size_t)ow*oh*3+i]!=0xa5) return 8;
    if(result==1 && pixels) {
        file=fopen(argv[2],"wb");if(!file) return 9;
        fwrite(pixels,3,(size_t)ow*oh,file);fclose(file);
    }
    printf("{\"result\":%d,\"width\":%u,\"height\":%u,\"progressive\":%d,\"callbacks\":%u,\"peak\":%zu,\"allocations\":%zu}\n",
        result,width,height,progressive,calls,probe_peak(),probe_allocations());
    free(pixels);free(input);if(probe_live()) return 10;return 0;
}
