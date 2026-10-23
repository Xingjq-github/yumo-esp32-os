#include "fc_save_store.h"
#include "nes/checkpoint.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#define fileno _fileno
#else
#include <unistd.h>
#endif

enum { HEADER=96 };
static uint64_t get(const uint8_t *p,unsigned size) {
    uint64_t v=0; for(unsigned i=0;i<size;++i) v|=(uint64_t)p[i]<<(8*i); return v;
}
static void put(uint8_t *p,uint64_t v,unsigned size) {
    for(unsigned i=0;i<size;++i) p[i]=(uint8_t)(v>>(8*i));
}
static bool fail(fc_save_store_t *s,int error) { s->error=error?error:EIO; return false; }
static bool proceed(fc_save_store_t *s) {
    return !s->proceed || s->proceed(s->context) || fail(s,ECANCELED);
}
static bool usable(fc_save_store_t *s,unsigned slot) {
    s->error=0;
    return (s->directory && s->buffer && s->length && s->length<=FC_SAVE_MAX && slot<FC_SAVE_SLOTS) || fail(s,EINVAL);
}
bool fc_save_path(const fc_save_store_t *s,unsigned slot,unsigned bank,char *path,size_t size) {
    if(!s || !s->directory || slot>=FC_SAVE_SLOTS || bank>1) return false;
    const char hex[]="0123456789abcdef";char id[65];
    for(unsigned i=0;i<32;++i) { id[i*2]=hex[s->rom[i]>>4]; id[i*2+1]=hex[s->rom[i]&15]; }
    id[64]=0;
    // Manual 1-3 keep their original names. Auto must never inherit old slot 4.
    int count=slot==FC_SAVE_AUTO_SLOT?
        snprintf(path,size,"%s/ym-%s-auto-%c.sav",s->directory,id,'a'+bank):
        snprintf(path,size,"%s/ym-%s-%u-%c.sav",s->directory,id,slot+1,'a'+bank);
    return count>0 && (size_t)count<size;
}
static bool close_file(fc_save_store_t *s,FILE *file,bool ok) {
    if(fclose(file)!=0) return fail(s,errno);
    return ok;
}
/* Invalid/truncated files remain visible as occupied slots; actual IO errors
 * abort the operation so a temporarily unreadable save is never overwritten. */
static bool inspect(fc_save_store_t *s,unsigned slot,unsigned bank,fc_save_slot_t *out) {
    memset(out,0,sizeof(*out));out->bank=bank;
    char path[256];
    if(!fc_save_path(s,slot,bank,path,sizeof(path))) return fail(s,ENAMETOOLONG);
    if(!proceed(s)) return false;
    FILE *file=fopen(path,"rb");
    if(!file) return errno==ENOENT || fail(s,errno);
    out->exists=true;
    uint8_t h[HEADER];
    size_t got=fread(h,1,sizeof(h),file);
    if(ferror(file)) return close_file(s,file,fail(s,EIO));
    if(got!=sizeof(h) || memcmp(h,"YUMOFC1",8) || get(h+8,4)!=1 ||
       get(h+12,4)!=HEADER || get(h+16,4)!=s->length || get(h+20,4)!=slot ||
       !get(h+24,8) || memcmp(h+56,s->rom,32) || get(h+88,4) ||
       get(h+92,4)!=checkpoint_crc(h,92) || (get(h+48,4)!=50 && get(h+48,4)!=60))
        return close_file(s,file,true);
    for(size_t pos=0;pos<s->length;) {
        if(!proceed(s)) return close_file(s,file,false);
        size_t count=s->length-pos; if(count>4096) count=4096;
        got=fread(s->buffer+pos,1,count,file);
        if(ferror(file)) return close_file(s,file,fail(s,EIO));
        if(got!=count) return close_file(s,file,true);
        pos+=count;
    }
    int trailing=fgetc(file);
    if(ferror(file)) return close_file(s,file,fail(s,EIO));
    if(!close_file(s,file,true)) return false;
    if(trailing!=EOF || get(h+52,4)!=checkpoint_crc(s->buffer,s->length) ||
       (s->validate && !s->validate(s->context,s->buffer,s->length))) return true;
    out->valid=true;out->revision=get(h+24,8);out->timestamp=get(h+32,8);
    out->frames=get(h+40,8);out->rate=get(h+48,4);out->crc=get(h+52,4);
    return true;
}
static bool latest(fc_save_store_t *s,unsigned slot,fc_save_slot_t *out) {
    fc_save_slot_t a,b;
    if(!inspect(s,slot,0,&a) || !inspect(s,slot,1,&b)) return false;
    *out=(b.valid && (!a.valid || b.revision>a.revision))?b:a;
    out->exists=a.exists || b.exists;
    return true;
}
bool fc_save_same(const fc_save_slot_t *a,const fc_save_slot_t *b) {
    return a->exists==b->exists && a->valid==b->valid && a->bank==b->bank &&
           a->revision==b->revision && a->crc==b->crc && a->timestamp==b->timestamp &&
           a->frames==b->frames && a->rate==b->rate;
}
bool fc_save_scan(fc_save_store_t *s,fc_save_slot_t slots[FC_SAVE_SLOTS]) {
    if(!usable(s,0)) return false;
    for(unsigned i=0;i<FC_SAVE_SLOTS;++i) if(!latest(s,i,&slots[i])) return false;
    return true;
}
bool fc_save_inspect_slot(fc_save_store_t *s,unsigned slot,fc_save_slot_t *out) {
    return usable(s,slot) && latest(s,slot,out);
}
bool fc_save_read(fc_save_store_t *s,unsigned slot,const fc_save_slot_t *expected,fc_save_slot_t *result) {
    if(!usable(s,slot)) return false;
    fc_save_slot_t found;
    if(!latest(s,slot,&found)) return false;
    if(!fc_save_same(&found,expected)) return fail(s,ESTALE);
    if(!found.valid) return fail(s,EBADMSG);
    if(!inspect(s,slot,found.bank,result)) return false;
    if(!fc_save_same(result,&found)) return fail(s,ESTALE);
    return proceed(s);
}
bool fc_save_write(fc_save_store_t *s,unsigned slot,const fc_save_slot_t *expected,
                   uint64_t timestamp,uint64_t frames,uint32_t rate,fc_save_slot_t *result) {
    if(!usable(s,slot) || (rate!=50 && rate!=60)) return fail(s,EINVAL);
    fc_save_slot_t found;
    if(!latest(s,slot,&found)) return false;
    if(!fc_save_same(&found,expected)) return fail(s,ESTALE);
    if(!s->capture || !s->capture(s->context,s->buffer,s->length)) return fail(s,EBADMSG);
    if(expected->revision==UINT64_MAX) return fail(s,EOVERFLOW);
    const unsigned bank=expected->valid?(expected->bank^1):0;
    uint8_t h[HEADER]={0}; memcpy(h,"YUMOFC1",8);
    put(h+8,1,4);put(h+12,HEADER,4);put(h+16,s->length,4);put(h+20,slot,4);
    put(h+24,expected->revision+1,8);put(h+32,timestamp,8);put(h+40,frames,8);
    put(h+48,rate,4);put(h+52,checkpoint_crc(s->buffer,s->length),4);
    memcpy(h+56,s->rom,32);put(h+92,checkpoint_crc(h,92),4);
    char path[256];
    if(!fc_save_path(s,slot,bank,path,sizeof(path))) return fail(s,ENAMETOOLONG);
    if(!proceed(s)) return false;
    FILE *file=fopen(path,"wb");
    if(!file) return fail(s,errno);
    if(fwrite(h,1,sizeof(h),file)!=sizeof(h)) return close_file(s,file,fail(s,errno));
    for(size_t pos=0;pos<s->length;) {
        if(!proceed(s)) return close_file(s,file,false);
        size_t count=s->length-pos;if(count>4096) count=4096;
        if(fwrite(s->buffer+pos,1,count,file)!=count) return close_file(s,file,fail(s,errno));
        pos+=count;
    }
    if(fflush(file)!=0 || fsync(fileno(file))!=0) return close_file(s,file,fail(s,errno));
    if(!close_file(s,file,true) || !inspect(s,slot,bank,result)) return false;
    if(!result->valid || result->revision!=expected->revision+1 || result->crc!=get(h+52,4)) return fail(s,EIO);
    return true;
}

/* Streaming counterpart for detached snapshots. Never reuses the immutable
 * snapshot as read scratch, and never dereferences a live CPU/PPU/mapper. */
static bool inspect_snapshot(fc_save_store_t *s,const uint8_t *snapshot,unsigned bank,fc_save_slot_t *out) {
    const unsigned slot=FC_SAVE_AUTO_SLOT;
    memset(out,0,sizeof(*out));out->bank=bank;
    char path[256];if(!fc_save_path(s,slot,bank,path,sizeof(path)))return fail(s,ENAMETOOLONG);
    if(!proceed(s))return false;
    FILE *file=fopen(path,"rb");
    if(!file)return errno==ENOENT || fail(s,errno);
    out->exists=true;uint8_t h[HEADER],inner[64];
    size_t got=fread(h,1,sizeof(h),file);
    if(ferror(file))return close_file(s,file,fail(s,EIO));
    if(got!=sizeof(h) || memcmp(h,"YUMOFC1",8) || get(h+8,4)!=1 || get(h+12,4)!=HEADER ||
       get(h+16,4)!=s->length || get(h+20,4)!=slot || !get(h+24,8) || memcmp(h+56,s->rom,32) ||
       get(h+88,4) || get(h+92,4)!=checkpoint_crc(h,92) || (get(h+48,4)!=50 && get(h+48,4)!=60))
        return close_file(s,file,true);
    uint32_t crc=~0u,payload=~0u;
    for(size_t pos=0;pos<s->length;) {
        if(!proceed(s))return close_file(s,file,false);
        const size_t count=s->length-pos<4096?s->length-pos:4096;
        got=fread(s->buffer,1,count,file);
        if(ferror(file))return close_file(s,file,fail(s,EIO));
        if(got!=count)return close_file(s,file,true);
        if(!pos) {
            memcpy(inner,s->buffer,sizeof(inner));
            // Only the snapshot's payload checksum varies between saves.
            if(memcmp(inner,snapshot,16) || memcmp(inner+20,snapshot+20,44))return close_file(s,file,true);
        }
        crc=checkpoint_crc_update(crc,s->buffer,count);
        const size_t skip=pos?0:sizeof(inner);
        payload=checkpoint_crc_update(payload,s->buffer+skip,count-skip);
        pos+=count;
    }
    int trailing=fgetc(file);
    if(ferror(file))return close_file(s,file,fail(s,EIO));
    if(!close_file(s,file,true))return false;
    if(trailing!=EOF || get(h+52,4)!=~crc || get(inner+16,4)!=~payload)return true;
    out->valid=true;out->revision=get(h+24,8);out->timestamp=get(h+32,8);
    out->frames=get(h+40,8);out->rate=get(h+48,4);out->crc=get(h+52,4);return true;
}
bool fc_save_write_snapshot(fc_save_store_t *s,const uint8_t *snapshot,uint64_t timestamp,
                            uint64_t frames,uint32_t rate,fc_save_slot_t *result) {
    if(!usable(s,FC_SAVE_AUTO_SLOT) || !snapshot || s->length<64 || (rate!=50 && rate!=60))return fail(s,EINVAL);
    fc_save_slot_t a,b,old;
    if(!inspect_snapshot(s,snapshot,0,&a) || !inspect_snapshot(s,snapshot,1,&b))return false;
    old=(b.valid && (!a.valid || b.revision>a.revision))?b:a;
    if(old.valid && old.revision==UINT64_MAX)return fail(s,EOVERFLOW);
    const unsigned bank=old.valid?(old.bank^1):0;
    const uint64_t revision=old.valid?old.revision+1:1;
    uint8_t h[HEADER]={0};memcpy(h,"YUMOFC1",8);
    put(h+8,1,4);put(h+12,HEADER,4);put(h+16,s->length,4);put(h+20,FC_SAVE_AUTO_SLOT,4);
    put(h+24,revision,8);put(h+32,timestamp,8);put(h+40,frames,8);put(h+48,rate,4);
    put(h+52,checkpoint_crc(snapshot,s->length),4);memcpy(h+56,s->rom,32);put(h+92,checkpoint_crc(h,92),4);
    char path[256];if(!fc_save_path(s,FC_SAVE_AUTO_SLOT,bank,path,sizeof(path)))return fail(s,ENAMETOOLONG);
    if(!proceed(s))return false;
    FILE *file=fopen(path,"wb");if(!file)return fail(s,errno);
    if(fwrite(h,1,sizeof(h),file)!=sizeof(h))return close_file(s,file,fail(s,errno));
    for(size_t pos=0;pos<s->length;) {
        if(!proceed(s))return close_file(s,file,false);
        const size_t count=s->length-pos<4096?s->length-pos:4096;
        if(fwrite(snapshot+pos,1,count,file)!=count)return close_file(s,file,fail(s,errno));
        pos+=count;
    }
    if(fflush(file)!=0 || fsync(fileno(file))!=0)return close_file(s,file,fail(s,errno));
    if(!close_file(s,file,true) || !inspect_snapshot(s,snapshot,bank,result))return false;
    if(!result->valid || result->revision!=revision || result->crc!=get(h+52,4))return fail(s,EIO);
    return true;
}
