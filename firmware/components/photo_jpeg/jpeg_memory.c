// Per-decode RAM accounting and optional worker-owned virtual-array storage.
#define JPEG_INTERNALS
#include "jpeg_port.h"
#include "jmemsys.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
typedef union { size_t size; long double alignment; void *pointer; } Allocation;
size_t photo_jpeg_available(void) {
#ifdef ESP_PLATFORM
    const size_t free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return free>PHOTO_JPEG_RESERVE?free-PHOTO_JPEG_RESERVE:0;
#else
    return PHOTO_JPEG_MAX_CODEC;
#endif
}
void *jpeg_get_small(j_common_ptr jpeg,size_t bytes) {
    photo_jpeg_reader *r=jpeg->client_data;
    if(!r || bytes>r->limit || sizeof(Allocation)>r->limit-bytes ||
       r->used>r->limit-bytes-sizeof(Allocation) || bytes+sizeof(Allocation)>photo_jpeg_available()) {
        if(r) r->result=PHOTO_JPEG_MEMORY;
        return NULL;
    }
#ifdef ESP_PLATFORM
    Allocation *p=heap_caps_malloc(sizeof(*p)+bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    Allocation *p=malloc(sizeof(*p)+bytes);
#endif
    if(!p) {r->result=PHOTO_JPEG_MEMORY;return NULL;}
    p->size=sizeof(*p)+bytes;r->used+=p->size;return p+1;
}
void jpeg_free_small(j_common_ptr jpeg,void *object,size_t bytes) {
    (void)bytes;if(!object) return;
    photo_jpeg_reader *r=jpeg->client_data;Allocation *p=(Allocation *)object-1;
    r->used-=p->size;
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}
void *jpeg_get_large(j_common_ptr jpeg,size_t bytes) {return jpeg_get_small(jpeg,bytes);}
void jpeg_free_large(j_common_ptr jpeg,void *object,size_t bytes) {jpeg_free_small(jpeg,object,bytes);}
size_t jpeg_mem_available(j_common_ptr jpeg,size_t min,size_t max,size_t allocated) {
    (void)min;(void)max;(void)allocated;
    photo_jpeg_reader *r=jpeg->client_data;
    size_t available=r->limit-r->used;
    // Leave room for row pointers, colour conversion, crop/upsampling and IDCT.
    return available>256*1024?available-256*1024:0;
}
typedef struct { unsigned slot;size_t length; } Store;
_Static_assert(sizeof(Store)<=TEMP_NAME_LENGTH,"backing descriptor capacity");
static Store store_info(backing_store_ptr info) {
    Store s;memcpy(&s,info->temp_name,sizeof(s));return s;
}
static void transfer(j_common_ptr jpeg,backing_store_ptr info,void *buffer,long offset,long bytes,int writing) {
    photo_jpeg_reader *r=jpeg->client_data;Store s=store_info(info);
    if(r->cancel && r->cancel(r->context)) {r->result=PHOTO_JPEG_CANCELLED;ERREXIT(jpeg,JERR_FILE_READ);}
    if(offset<0 || bytes<0 || (size_t)offset>s.length || (size_t)bytes>s.length-(size_t)offset) {
        r->result=PHOTO_JPEG_BACKING_LIMIT;ERREXIT(jpeg,JERR_FILE_READ);
    }
    int ok=writing?r->backing->write(r->backing->context,s.slot,(size_t)offset,buffer,(size_t)bytes):
        r->backing->read(r->backing->context,s.slot,(size_t)offset,buffer,(size_t)bytes);
    if(!ok) {r->result=PHOTO_JPEG_IO;ERREXIT(jpeg,JERR_FILE_READ);}
}
static void read_store(j_common_ptr j,backing_store_ptr s,void *p,long o,long n) {transfer(j,s,p,o,n,0);}
static void write_store(j_common_ptr j,backing_store_ptr s,void *p,long o,long n) {transfer(j,s,p,o,n,1);}
static void close_store(j_common_ptr j,backing_store_ptr info) {
    photo_jpeg_reader *r=j->client_data;Store s=store_info(info);
    // Destruction must finish even after cancellation or an I/O error.
    if(!r->backing->close(r->backing->context,s.slot)) r->result=PHOTO_JPEG_IO;
    r->backing_bytes-=s.length;
}
void jpeg_open_backing_store(j_common_ptr jpeg,backing_store_ptr info,long bytes) {
    photo_jpeg_reader *r=jpeg->client_data;
    if(!r->backing || !r->backing->open || !r->backing->read || !r->backing->write || !r->backing->close) {
        r->result=PHOTO_JPEG_MEMORY;ERREXIT(jpeg,JERR_NO_BACKING_STORE);
    }
    if(bytes<=0 || (size_t)bytes>PHOTO_JPEG_MAX_BACKING-r->backing_bytes) {
        r->result=PHOTO_JPEG_BACKING_LIMIT;ERREXIT(jpeg,JERR_NO_BACKING_STORE);
    }
    Store s={0,(size_t)bytes};
    if(!r->backing->open(r->backing->context,s.length,&s.slot)) {r->result=PHOTO_JPEG_IO;ERREXIT(jpeg,JERR_NO_BACKING_STORE);}
    r->backing_bytes+=s.length;
    memcpy(info->temp_name,&s,sizeof(s));info->temp_file=NULL;
    info->read_backing_store=read_store;info->write_backing_store=write_store;info->close_backing_store=close_store;
}
long jpeg_mem_init(j_common_ptr jpeg) {(void)jpeg;return PHOTO_JPEG_MAX_CODEC;}
void jpeg_mem_term(j_common_ptr jpeg) {(void)jpeg;}
