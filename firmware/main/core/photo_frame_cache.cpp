#include "photo_frame_cache.hpp"
#include "apps/photo_pixels.hpp"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace core::photo::frames {
namespace {
constexpr uint32_t Tile=128,Rows=8,StripeBytes=Tile*Rows*2,TileBytes=Tile*Tile*2,RecordBytes=TileBytes+8,HeaderBytes=128;
constexpr uint64_t Basis=14695981039346656037ull,MaxLevel=160ull*1024*1024,MaxTotal=256ull*1024*1024,KeepFree=16ull*1024*1024;
constexpr unsigned MaxEntries=96,Caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
constexpr char Magic[24]="YUMO_PHOTO_TILES_V1\r\n";
uint64_t hash(const void *memory,size_t n,uint64_t h=Basis) {
    const auto *p=static_cast<const uint8_t *>(memory);
    while(n--) {h^=*p++;h*=1099511628211ull;}return h;
}
uint64_t get64(const uint8_t *p) {uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(8*i);return n;}
uint32_t get32(const uint8_t *p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=uint8_t(n>>(8*i));}
void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
uint64_t levelBytes(uint32_t w,uint32_t h) {return HeaderBytes+uint64_t((w+Tile-1)/Tile)*((h+Tile-1)/Tile)*RecordBytes;}
void header(uint8_t *out,const Source &s,const photo_view &v,bool committed) {
    std::memset(out,0,HeaderBytes);std::memcpy(out,Magic,sizeof(Magic));put32(out+24,1);put32(out+28,committed);
    put64(out+32,s.length);put64(out+40,uint64_t(s.modified));put64(out+48,hash(s.path.data(),s.path.size()));
    put32(out+56,s.width);put32(out+60,s.height);put32(out+64,(s.png?1u:0u)|(s.progressive?2u:0u));
    put32(out+68,v.scaled_width);put32(out+72,v.scaled_height);put32(out+76,Tile);
    put64(out+80,s.fingerprint);put64(out+96,levelBytes(v.scaled_width,v.scaled_height));
    // Pan is absent from the key: every position at this scale shares one file.
    uint8_t key[80];std::memcpy(key,out,sizeof(key));put32(key+28,1);
    put64(out+112,hash(key,sizeof(key)));put64(out+120,hash(out,120));
}
void filePath(char *out,size_t size,const Source &s,const photo_view &v) {
    uint8_t h[HeaderBytes];header(h,s,v,true);
    std::snprintf(out,size,"%s/%016llx.ytc",sd::PhotoFrames,(unsigned long long)get64(h+112));
}
bool io(FILE *f,void *data,size_t n,bool writing,const Access &a) {
    auto *p=static_cast<uint8_t *>(data);
    while(n) {
        if(a.stopping())return false;
        size_t count=std::min<size_t>(4096,n);
        if((writing?std::fwrite(p,1,count,f):std::fread(p,1,count,f))!=count)return false;
        p+=count;n-=count;
    }
    return true;
}
bool validHeader(FILE *f,uint8_t *h,const Access &a,bool temporary=false) {
    struct stat st{};
    if(fstat(fileno(f),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<static_cast<off_t>(HeaderBytes) ||
       !io(f,h,HeaderBytes,false,a) || std::memcmp(h,Magic,sizeof(Magic)) || get32(h+24)!=1 ||
       get64(h+120)!=hash(h,120) || get32(h+76)!=Tile)return false;
    const uint32_t w=get32(h+68),height=get32(h+72);
    const uint64_t size=levelBytes(w,height);
    if(!w || !height || w>16384 || height>16384 || size>MaxLevel || get64(h+96)!=size)return false;
    return temporary?get32(h+28)<=1 && uint64_t(st.st_size)<=size:
        get32(h+28)==1 && uint64_t(st.st_size)==size;
}
bool matches(const uint8_t *h,const Source &s,const photo_view &v,bool content) {
    uint8_t expected[HeaderBytes];header(expected,s,v,true);
    return !std::memcmp(h,expected,80) && get64(h+112)==get64(expected+112) &&
        (!content || get64(h+80)==s.fingerprint);
}
bool enough(size_t bytes,size_t reserve) {
    const size_t free=heap_caps_get_free_size(Caps);
    return free>reserve && bytes<=free-reserve && bytes<=heap_caps_get_largest_free_block(Caps);
}
bool named(const char *n) {
    if(std::strlen(n)!=20 || std::strcmp(n+16,".ytc"))return false;
    for(unsigned i=0;i<16;++i)if(!((n[i]>='0'&&n[i]<='9')||(n[i]>='a'&&n[i]<='f')))return false;
    return true;
}
bool removeOwned(const char *path,const Access &a,bool temporary) {
    FILE *f=std::fopen(path,"rb");if(!f)return errno==ENOENT;
    uint8_t h[HeaderBytes];bool owned=validHeader(f,h,a,temporary);bool closed=std::fclose(f)==0;
    return owned && closed && !a.stopping() && std::remove(path)==0;
}
struct Candidate {char name[21];uint64_t bytes;int64_t modified;};
bool makeRoom(uint64_t required,uint64_t extra,const Access &a) {
    auto candidates=std::unique_ptr<Candidate[]>(new(std::nothrow) Candidate[MaxEntries+1]);
    if(!candidates)return false;
    DIR *dir=::opendir(sd::PhotoFrames);if(!dir)return false;
    unsigned count=0,seen=0;uint64_t used=0;bool ok=true;
    while(auto *entry=::readdir(dir)) {
        if(++seen>512 || a.stopping()){ok=false;break;}
        if(!named(entry->d_name))continue;
        char path[96];std::snprintf(path,sizeof(path),"%s/%s",sd::PhotoFrames,entry->d_name);
        FILE *f=std::fopen(path,"rb");if(!f){ok=false;break;}
        uint8_t h[HeaderBytes];struct stat st{};
        bool ours=validHeader(f,h,a) && fstat(fileno(f),&st)==0;
        if(std::fclose(f)!=0){ok=false;break;}
        if(!ours)continue; // Foreign or malformed entries are never evicted.
        if(count==MaxEntries+1){ok=false;break;}
        auto &c=candidates[count++];std::memcpy(c.name,entry->d_name,21);c.bytes=st.st_size;c.modified=st.st_mtime;used+=c.bytes;
    }
    if(::closedir(dir)!=0)ok=false;
    if(!ok)return false;
    uint64_t total=0,free=0;
    if(esp_vfs_fat_info(sd::Root,&total,&free)!=ESP_OK)return false;
    std::sort(candidates.get(),candidates.get()+count,[](const Candidate &x,const Candidate &y){return x.modified<y.modified;});
    unsigned removed=0;
    while(used+required>MaxTotal || count-removed>=MaxEntries || free<required+extra+KeepFree) {
        if(removed==count || a.stopping())return false;
        const auto &c=candidates[removed++];char path[96];std::snprintf(path,sizeof(path),"%s/%s",sd::PhotoFrames,c.name);
        if(!removeOwned(path,a,false))return false;
        used-=c.bytes;free+=c.bytes;
    }
    return esp_vfs_fat_info(sd::Root,&total,&free)==ESP_OK && free>=required+extra+KeepFree;
}
}
bool contains(const Source &s,const photo_view &v,const Access &a) {
    if(!a.card || a.stopping())return false;
    char path[96];filePath(path,sizeof(path),s,v);FILE *f=std::fopen(path,"rb");if(!f)return false;
    uint8_t h[HeaderBytes];bool ok=validHeader(f,h,a) && matches(h,s,v,false);
    if(std::fclose(f)!=0)ok=false;
    return ok;
}
uint8_t *load(const Source &s,const photo_view &v,const Access &a) {
    if(!a.card || a.stopping() || !photo_view_valid(&v,s.width,s.height))return nullptr;
    char path[96];filePath(path,sizeof(path),s,v);FILE *f=std::fopen(path,"rb");if(!f)return nullptr;
    uint8_t h[HeaderBytes];bool ok=validHeader(f,h,a) && matches(h,s,v,true);
    const size_t bytes=size_t(v.width)*v.height*2;
    uint8_t *pixels=nullptr,*tile=nullptr;
    if(ok && enough(bytes+RecordBytes,384*1024)) {
        pixels=static_cast<uint8_t *>(heap_caps_malloc(bytes,Caps));tile=static_cast<uint8_t *>(heap_caps_malloc(RecordBytes,Caps));
    }
    ok=ok && pixels && tile;
    const unsigned cols=(v.scaled_width+Tile-1)/Tile;
    for(unsigned ty=v.y/Tile;ok && ty<=(v.y+v.height-1)/Tile;++ty) {
        for(unsigned tx=v.x/Tile;ok && tx<=(v.x+v.width-1)/Tile;++tx) {
            const uint64_t at=HeaderBytes+uint64_t(ty*cols+tx)*RecordBytes;
            ok=std::fseek(f,long(at),SEEK_SET)==0 && io(f,tile,RecordBytes,false,a) && get64(tile)==hash(tile+8,TileBytes);
            if(!ok)break;
            const unsigned x0=std::max(v.x,tx*Tile),x1=std::min(v.x+v.width,(tx+1)*Tile);
            const unsigned y0=std::max(v.y,ty*Tile),y1=std::min(v.y+v.height,(ty+1)*Tile);
            for(unsigned y=y0;y<y1;++y)std::memcpy(pixels+((y-v.y)*v.width+x0-v.x)*2,
                tile+8+((y-ty*Tile)*Tile+x0-tx*Tile)*2,(x1-x0)*2);
        }
    }
    if(std::fclose(f)!=0)ok=false;
    heap_caps_free(tile);if(!ok){heap_caps_free(pixels);pixels=nullptr;}return pixels;
}
Writer::Writer(const Source &s,const photo_view &v,const Access &a):access_(a),view_(v) {
    columns_=(v.scaled_width+Tile-1)/Tile;
    const size_t bytes=size_t(columns_)*StripeBytes+size_t(v.scaled_width)*4;
    const size_t reserve=384*1024+(s.progressive?2*1024*1024:512*1024)+size_t(v.width)*v.height*3;
    if(!a.card || a.stopping() || !photo_view_valid(&v,s.width,s.height) || levelBytes(v.scaled_width,v.scaled_height)>MaxLevel ||
       !enough(bytes,reserve) || !a.claim(a.owner))return;
    if(!sd::ensure(sd::PhotoFrames))return;
    filePath(path_,sizeof(path_),s,v);std::snprintf(temporary_,sizeof(temporary_),"%s/building.tmp",sd::PhotoFrames);
    if(!removeOwned(temporary_,a,true) || !removeOwned(path_,a,true) ||
       !makeRoom(levelBytes(v.scaled_width,v.scaled_height),s.progressive?128ull*1024*1024:0,a))return;
    memory_=static_cast<uint8_t *>(heap_caps_malloc(bytes,Caps));if(!memory_)return;
    band_=memory_;sink_.row=band_+size_t(columns_)*StripeBytes;sink_.valid=sink_.row+v.scaled_width*3;
    sink_.context=this;sink_.write=[](void *p,uint32_t y,unsigned pass){static_cast<Writer *>(p)->row(y,pass);};
    int fd=::open(temporary_,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return;
    owned_=true;file_=::fdopen(fd,"w+b");if(!file_){::close(fd);return;}
    uint8_t h[HeaderBytes];header(h,s,v,false);
    good_=io(file_,h,HeaderBytes,true,a) && std::fflush(file_)==0;
}
Writer::~Writer() {
    if(file_)std::fclose(file_);
    // Worker still owns SD admission, including on cancellation and sleep.
    if(owned_)std::remove(temporary_);
    heap_caps_free(memory_);
}
bool Writer::stripe(bool writing,uint32_t y,unsigned pass) {
    if(access_.stopping())return false;
    if(writing && y%Tile==0)for(unsigned c=0;c<columns_;++c)hashes_[c]=Basis;
    for(unsigned c=0;c<columns_;++c) {
        const uint64_t at=HeaderBytes+uint64_t((y/Tile)*columns_+c)*RecordBytes;
        auto *p=band_+c*StripeBytes;
        if(std::fseek(file_,long(at+8+(y%Tile)*Tile*2),SEEK_SET)!=0 || !io(file_,p,StripeBytes,writing,access_))return false;
        if(writing && pass+1==sink_.passes) {
            hashes_[c]=hash(p,StripeBytes,hashes_[c]);
            if(y%Tile==Tile-Rows) {
                uint8_t sum[8];put64(sum,hashes_[c]);
                if(std::fseek(file_,long(at),SEEK_SET)!=0 || !io(file_,sum,sizeof(sum),true,access_))return false;
            }
        }
    }
    return true;
}
void Writer::row(uint32_t y,unsigned pass) {
    if(!good_)return;
    if(y!=nextY_ || pass!=pass_ || access_.stopping()){fail();return;}
    if(y%Rows==0) {
        bandY_=y;
        if(pass==0)std::memset(band_,0,size_t(columns_)*StripeBytes);
        else if(!stripe(false,y,pass)){fail();return;}
    }
    for(unsigned x=0;x<view_.scaled_width;++x) {
        if(sink_.passes>1 && !sink_.valid[x])continue;
        const auto *rgb=sink_.row+x*3;
        uint16_t color=apps::photo_pixels::rgb565(rgb[0],rgb[1],rgb[2],x,y);
        auto *p=band_+(x/Tile)*StripeBytes+((y%Rows)*Tile+x%Tile)*2;p[0]=uint8_t(color);p[1]=uint8_t(color>>8);
    }
    ++nextY_;
    if(y%Rows==Rows-1 || nextY_==view_.scaled_height) {
        if(!stripe(true,bandY_,pass)){fail();return;}
        if(nextY_==view_.scaled_height) {
            // Every edge tile is padded, making tile checksums independent of pan.
            std::memset(band_,0,size_t(columns_)*StripeBytes);
            for(unsigned at=bandY_+Rows;at%Tile;at+=Rows)if(!stripe(true,at,pass)){fail();return;}
            nextY_=0;++pass_;complete_=pass_==sink_.passes;
        }
    }
}
bool Writer::commit(const Source &s) {
    if(!good_ || !complete_ || access_.stopping())return false;
    uint8_t h[HeaderBytes];header(h,s,view_,true);
    bool ok=std::fseek(file_,0,SEEK_SET)==0 && io(file_,h,sizeof(h),true,access_) && std::fflush(file_)==0 && ::fsync(fileno(file_))==0;
    if(std::fclose(file_)!=0)ok=false;
    file_=nullptr;
    if(ok && !access_.stopping() && std::rename(temporary_,path_)==0){owned_=false;return true;}
    return false;
}
}
