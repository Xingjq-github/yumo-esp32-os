#include "photo_loader.hpp"
#include "photo_frame_cache.hpp"
#include "photo_png.h"
#include "photo_jpeg.h"
#include "photo_pixels.hpp"
#include "power_mgr.hpp"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace core::photo {
namespace {
std::atomic<bool> active{false};
constexpr size_t MaxFileBytes=16*1024*1024;
constexpr size_t CacheBytes=256*1024,ReserveBytes=384*1024,CodecRoom=512*1024;
constexpr uint32_t Caps=MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
using namespace apps;
bool stopping(const Job &job) {return job.cancelled.load() || sys::Power::sleepPending();}
bool canAllocate(size_t bytes,size_t reserve=ReserveBytes) {
    const size_t free=heap_caps_get_free_size(Caps);
    return free>reserve && bytes<=free-reserve && bytes<=heap_caps_get_largest_free_block(Caps);
}
struct Context {Job &job;int64_t started=esp_timer_get_time(),lastYield=0,limit=90000000;};
int cancelled(void *context) {
    auto &c=*static_cast<Context *>(context);
    if(stopping(c.job)) return 1;
    if(sd::busy()) {c.job.error="SD卡正在退出，请返回后重试";return 1;}
    const auto now=esp_timer_get_time();
    if(now-c.started>c.limit) {c.job.error="图片解码超时，请缩小后重试";return 1;}
    if(now-c.lastYield>=4000) {vTaskDelay(1);c.lastYield=now;}
    return stopping(c.job)?1:0;
}
const char *jpegError(int result) {
    return result==PHOTO_JPEG_LIMIT?"JPEG尺寸超限：边长16384，普通64MP／渐进32MP":
        result==PHOTO_JPEG_MEMORY?"JPEG解码工作内存不足，请稍后重试":
        result==PHOTO_JPEG_BACKING_LIMIT?"渐进JPEG中间数据超过128MiB，请转为普通JPEG":
        result==PHOTO_JPEG_FORMAT?"不支持此JPEG编码：需要8位RGB或灰度":
        result==PHOTO_JPEG_IO?"JPEG临时缓存读写失败，请检查SD卡":"JPEG数据不完整或已损坏";
}
const char *pngError(int result) {
    return result==PHOTO_PNG_LIMIT?"PNG尺寸超限：边长16384、总像素64MP":
        result==PHOTO_PNG_MEMORY?"PNG解码工作内存不足，请稍后重试":"PNG数据不完整或编码无效";
}
// Lifetime stays entirely in C++: callbacks return before a codec can longjmp.
// Close FILE before the SD Session on success, errors, cancellation and throws.
struct Input {
    Context &context;
    sd::Session card;
    FILE *file=nullptr;
    bool fresh;
    size_t offset=0;
    uint64_t hash=14695981039346656037ull;
    struct Temp {FILE *file=nullptr;size_t length=0;bool owned=false;char path[96]{};} temps[3];
    bool prepared=false,mutation=false;
    size_t reserved=0;
    uint64_t traffic=0;
    static constexpr char magic[32]="YUMO_PHOTO_CACHE_V1\r\n";
    explicit Input(Context &c):context(c),card(c.job.sdAccess),fresh(!c.job.source) {}
    ~Input() {close();}
    bool close() {
        if(!card) return true; // Explicit close already released the lease.
        bool ok=true;
        for(unsigned i=0;i<3;++i) if(!closeTemp(i)) ok=false;
        if(file) {const bool closed=std::fclose(file)==0;file=nullptr;if(!closed) {ok=false;failure("SD卡关闭失败，请返回刷新");}}
        card.release();return ok;
    }
    bool failure(const char *message) {
        card.reportError(errno?errno:EIO);
        if(!context.job.error) context.job.error=message;
        return false;
    }
    bool closeTemp(unsigned slot) {
        if(slot>=3) return false;
        auto &t=temps[slot];bool ok=true;
        if(t.file) {ok=std::fclose(t.file)==0;t.file=nullptr;}
        if(t.owned) {
            if(std::remove(t.path)==0 || errno==ENOENT) {t.owned=false;reserved-=t.length;t.length=0;}
            else ok=false;
        }
        return ok?true:failure("图片临时缓存清理失败，请检查SD卡");
    }
    bool prepareTemps() {
        if(prepared) return true;
        if(!card) {context.job.error="渐进JPEG需要可写SD卡作为临时缓存";return false;}
        if(!claimMutation()) {context.job.error="SD卡正在使用，无法创建图片缓存，请稍后重试";return false;}
        if(!sd::ensure(sd::PhotoCache)) return failure("无法创建图片缓存目录，请检查SD卡");
        // Only the three named, magic-tagged files belong to this decoder.
        // This recovers interrupted sessions without touching unrelated files.
        for(unsigned i=0;i<3;++i) {
            auto &t=temps[i];std::snprintf(t.path,sizeof(t.path),"%s/coeff-%u.tmp",sd::PhotoCache,i);
            FILE *old=std::fopen(t.path,"rb");
            if(!old) {if(errno==ENOENT)continue;return failure("无法读取旧图片缓存");}
            struct stat st{};char tag[sizeof(magic)]{};
            bool owned=fstat(fileno(old),&st)==0 && S_ISREG(st.st_mode) && st.st_size>=long(sizeof(magic)) &&
                uint64_t(st.st_size)<=uint64_t(PHOTO_JPEG_MAX_BACKING)+sizeof(magic) &&
                std::fread(tag,1,sizeof(tag),old)==sizeof(tag) && !std::memcmp(tag,magic,sizeof(tag));
            const bool closed=std::fclose(old)==0;
            if(!closed) return failure("旧图片缓存关闭失败");
            if(!owned) {context.job.error="图片缓存目录含未知文件，请检查.yumo-photo-cache-v1";return false;}
            if(std::remove(t.path)!=0) return failure("旧图片缓存清理失败");
        }
        prepared=true;return true;
    }
    bool claimMutation() {if(!mutation)mutation=card.claimMutation();return mutation;}
    bool openTemp(size_t bytes,unsigned *slot) {
        if(cancelled(&context)) return false;
        if(bytes>PHOTO_JPEG_MAX_BACKING-reserved) {context.job.error="图片临时缓存超过128MiB";return false;}
        if(!prepareTemps()) return false;
        uint64_t total=0,free=0;
        if(esp_vfs_fat_info(sd::Root,&total,&free)!=ESP_OK) return failure("无法查询SD卡剩余空间");
        if(free<uint64_t(reserved)+bytes+8*1024*1024) {context.job.error="SD卡空间不足，图片缓存需额外保留8MiB";return false;}
        for(unsigned i=0;i<3;++i) if(!temps[i].owned) {
            auto &t=temps[i];
            int fd=::open(t.path,O_CREAT|O_EXCL|O_RDWR,0600);
            if(fd<0) return failure("无法创建图片临时文件，请检查SD卡");
            t.owned=true;t.length=bytes;reserved+=bytes;
            t.file=::fdopen(fd,"w+b");
            if(!t.file) {::close(fd);closeTemp(i);return failure("图片临时文件打开失败");}
            if(std::fwrite(magic,1,sizeof(magic),t.file)!=sizeof(magic) || std::fflush(t.file)!=0) {
                failure("图片缓存写入失败，可能空间不足");closeTemp(i);return false;
            }
            *slot=i;return true;
        }
        context.job.error="图片临时缓存数量超限";return false;
    }
    bool tempIo(unsigned slot,size_t at,uint8_t *out,const uint8_t *in,size_t bytes) {
        if(slot>=3 || !temps[slot].file || at>temps[slot].length || bytes>temps[slot].length-at) {
            context.job.error="图片临时缓存范围异常";return false;
        }
        if(traffic+bytes>2ull*1024*1024*1024) {context.job.error="渐进JPEG处理量过大，请转为普通JPEG";return false;}
        traffic+=bytes;
        if(cancelled(&context)) return false;
        FILE *f=temps[slot].file;
        if(std::fseek(f,long(at+sizeof(magic)),SEEK_SET)!=0) return failure("图片缓存定位失败");
        while(bytes) {
            if(cancelled(&context)) return false;
            size_t n=std::min<size_t>(4096,bytes);
            if(in) {
                if(std::fwrite(in,1,n,f)!=n) return failure("图片缓存写入失败，可能空间不足");
                in+=n;
            } else {
                if(std::fread(out,1,n,f)!=n) return failure("图片缓存读取失败");
                out+=n;
            }
            bytes-=n;
        }
        return true;
    }
    photo_jpeg_backing backing() {
        return {this,
            [](void *p,size_t n,unsigned *s)->int{return static_cast<Input *>(p)->openTemp(n,s);},
            [](void *p,unsigned s,size_t o,uint8_t *b,size_t n)->int{return static_cast<Input *>(p)->tempIo(s,o,b,nullptr,n);},
            [](void *p,unsigned s,size_t o,const uint8_t *b,size_t n)->int{return static_cast<Input *>(p)->tempIo(s,o,nullptr,b,n);},
            [](void *p,unsigned s)->int{return static_cast<Input *>(p)->closeTemp(s);}};
    }
    bool open() {
        auto &job=context.job;
        if(!card) {job.error="SD卡不可用，请返回刷新";return false;}
        if(cancelled(&context)) return false;
        const auto &path=job.source?job.source->path:job.path;
        const auto directory=path.substr(0,path.find_last_of('/'));
        if(path.rfind("/sdcard/",0)!=0 || path.find("/../")!=std::string::npos ||
           path.find("/./")!=std::string::npos || path.find('\\')!=std::string::npos ||
           (!sd::child(path,directory.c_str(),".jpg") && !sd::child(path,directory.c_str(),".jpeg") &&
            !sd::child(path,directory.c_str(),".png"))) {job.error="图片路径无效";return false;}
        if(job.source && job.source->generation!=card.generation()) {
            job.error="SD卡已变化，请重新打开图片";return false;
        }
        file=std::fopen(path.c_str(),"rb");
        if(!file) return failure("图片读取失败，请返回刷新");
        struct stat st{};
        if(fstat(fileno(file),&st)!=0) return failure("无法读取图片大小");
        if(!S_ISREG(st.st_mode) || st.st_size<=0 || size_t(st.st_size)>MaxFileBytes) {
            job.error="图片为空或超过16MiB";return false;
        }
        if(job.source) {
            if(job.source->length!=size_t(st.st_size) || job.source->modified!=int64_t(st.st_mtime)) {
                job.error="图片文件已变化，请重新打开";return false;
            }
        } else {
            auto source=std::make_shared<Source>();source->path=path;
            source->length=st.st_size;source->modified=st.st_mtime;source->generation=card.generation();
            job.source=std::move(source);
        }
        return true;
    }
    bool rewind() {
        if(cancelled(&context)) return false;
        if(std::fseek(file,0,SEEK_SET)!=0) return failure("图片定位失败，请返回刷新");
        offset=0;hash=14695981039346656037ull;return true;
    }
    bool read(uint8_t *out,size_t bytes) {
        if(bytes>context.job.source->length-offset) {errno=EIO;return failure("图片数据不完整");}
        while(bytes) {
            if(cancelled(&context)) return false;
            const size_t count=std::min<size_t>(4096,bytes);
            if(std::fread(out,1,count,file)!=count) {errno=EIO;return failure("图片读取中断，请检查SD卡");}
            for(size_t i=0;i<count;++i) {hash^=out[i];hash*=1099511628211ull;}
            offset+=count;out+=count;bytes-=count;
        }
        return true;
    }
    static int callback(void *context,uint8_t *out,size_t bytes) {
        return static_cast<Input *>(context)->read(out,bytes)?1:0;
    }
    bool finish() {
        // Include trailing metadata in file identity, with bounded memory/I/O.
        uint8_t tail[4096];auto &s=*context.job.source;
        while(offset<s.length) if(!read(tail,std::min(sizeof(tail),s.length-offset))) return false;
        struct stat st{};
        if(fstat(fileno(file),&st)!=0) return failure("图片读取状态异常");
        if(size_t(st.st_size)!=s.length || int64_t(st.st_mtime)!=s.modified || (!fresh && hash!=s.fingerprint)) {
            context.job.error="图片文件已变化，请重新打开";return false;
        }
        if(fresh) s.fingerprint=hash;
        return true;
    }
};
bool readSource(Job &job,Context &context,std::unique_ptr<Input> &input) {
    if(job.source && job.source->data) return true;
    if(cancelled(&context)) return false;
    input=std::make_unique<Input>(context);
    if(!input->open()) return false;
    auto &s=*job.source;
    if(input->fresh && s.length<=CacheBytes && canAllocate(s.length,ReserveBytes+2*CodecRoom)) {
        s.data=static_cast<uint8_t *>(heap_caps_malloc(s.length,Caps));
        if(s.data) {
            if(!input->read(s.data,s.length) || !input->finish() || !input->close()) return false;
            input.reset(); // Cached input releases SD before either decoder starts.
        }
    }
    return true;
}
int readPng(Job &job,Context &context,Input *input,uint8_t *pixels,uint16_t *w,uint16_t *h,photo_sink *sink=nullptr) {
    auto &s=*job.source;
    if(sink)return photo_png_read_tiles(s.data,Input::callback,input,s.length,pixels,&job.view,w,h,cancelled,&context,sink);
    return photo_png_read_view(s.data,Input::callback,input,s.length,pixels,pixels?&job.view:nullptr,w,h,cancelled,&context);
}
int readJpeg(Job &job,Context &context,Input *input,uint8_t *pixels,uint16_t *w,uint16_t *h,int *progressive,photo_sink *sink=nullptr) {
    auto &s=*job.source;
    photo_jpeg_backing backing=input?input->backing():photo_jpeg_backing{};
    if(sink)return photo_jpeg_read_tiles(s.data,Input::callback,input,s.length,pixels,&job.view,
                                        w,h,progressive,cancelled,&context,input?&backing:nullptr,sink);
    return photo_jpeg_read_view(s.data,Input::callback,input,s.length,pixels,pixels?&job.view:nullptr,
                               w,h,progressive,cancelled,&context,input?&backing:nullptr);
}
bool parseHeader(Job &job,Context &context,Input *input) {
    auto &s=*job.source;
    if(s.width && s.height) return true;
    static constexpr uint8_t signature[]={137,80,78,71,13,10,26,10};
    uint8_t prefix[8]{};
    if(s.data) std::memcpy(prefix,s.data,std::min(sizeof(prefix),s.length));
    else if(!input->read(prefix,std::min(sizeof(prefix),s.length)) || !input->rewind()) return false;
    s.png=s.length>=8 && !memcmp(prefix,signature,8);
    if(s.png) {
        const int result=readPng(job,context,input,nullptr,&s.width,&s.height);
        if(result!=1) {
            if(!job.error) job.error=pngError(result);
            return false;
        }
    } else {
        if(s.length<2 || prefix[0]!=0xff || prefix[1]!=0xd8) {job.error="文件实际格式不是JPEG或PNG，请检查后缀";return false;}
        int progressive=0;
        const int header=readJpeg(job,context,input,nullptr,&s.width,&s.height,&progressive);
        if(header!=PHOTO_JPEG_OK) {if(!job.error) job.error=jpegError(header);s.width=s.height=0;return false;}
        s.progressive=progressive!=0;
    }
    if(!s.width || !s.height || s.width>16384 || s.height>16384) {job.error="图片边长不能超过16384像素";return false;}
    return !input || input->rewind();
}
bool decodeJpeg(Job &job) {
    auto &s=*job.source;
    if(!s.data || s.width<8 || s.height<8) return false;
    jpeg_dec_config_t config=DEFAULT_JPEG_DEC_CONFIG();
    config.output_type=JPEG_PIXEL_FORMAT_RGB888;
    auto alignDim=[](uint16_t target,uint16_t orig) -> uint16_t {
        const uint32_t min8=((orig+63)/64)*8;
        return std::max<uint32_t>(8,std::min<uint32_t>(std::max<uint32_t>(target/8*8,min8),orig/8*8));
    };
    if(job.width<s.width || job.height<s.height) {
        config.scale.width=alignDim(job.width,s.width);
        config.scale.height=alignDim(job.height,s.height);
    }
    jpeg_dec_handle_t decoder=nullptr;
    if(jpeg_dec_open(&config,&decoder)!=JPEG_ERR_OK) return false;
    jpeg_dec_io_t io={};jpeg_dec_header_info_t info={};
    io.inbuf=s.data;io.inbuf_len=int(s.length);
    if(jpeg_dec_parse_header(decoder,&io,&info)!=JPEG_ERR_OK) {jpeg_dec_close(decoder);return false;}
    const uint16_t width=config.scale.width?config.scale.width:info.width;
    const uint16_t height=config.scale.height?config.scale.height:info.height;
    const size_t rgbBytes=photo_pixels::rgb888Bytes(width,height);
    int outLength=0;
    if(!rgbBytes || jpeg_dec_get_outbuf_len(decoder,&outLength)!=JPEG_ERR_OK ||
       outLength<=0 || size_t(outLength)<rgbBytes || size_t(outLength)>rgbBytes+15) {
        jpeg_dec_close(decoder);return false;
    }
    const size_t extra=(job.width!=width || job.height!=height)?size_t(job.width)*job.height*2:0;
    if(!canAllocate(size_t(outLength)+15+extra,ReserveBytes+CodecRoom)) {jpeg_dec_close(decoder);return false;}
    auto *pixels=static_cast<uint8_t *>(heap_caps_malloc(size_t(outLength)+15,Caps));
    if(!pixels) {jpeg_dec_close(decoder);return false;}
    auto *decoded=reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(pixels)+15)&~uintptr_t(15));
    io.outbuf=decoded;io.out_size=0;
    const bool ok=!stopping(job) && jpeg_dec_process(decoder,&io)==JPEG_ERR_OK &&
        io.out_size>0 && size_t(io.out_size)==rgbBytes;
    jpeg_dec_close(decoder);
    if(!ok || stopping(job)) {heap_caps_free(pixels);return false;}
    // Finish fitting even when JPEG's native minimum scale is only 1/8.
    // Also correct its multiple-of-eight rounding without changing aspect ratio.
    const size_t imageBytes=size_t(job.width)*job.height*2;
    if(job.width!=width || job.height!=height) {
        job.pixels=static_cast<uint8_t *>(heap_caps_malloc(imageBytes,Caps));
        if(!job.pixels) {heap_caps_free(pixels);return false;}
        for(unsigned y=0;y<job.height;++y) {
            if((y&31)==0) {vTaskDelay(1);if(stopping(job)) {heap_caps_free(pixels);return false;}}
            const size_t sy=uint64_t(2*y+1)*height/(2*job.height);
            for(unsigned x=0;x<job.width;++x) {
                const size_t sx=uint64_t(2*x+1)*width/(2*job.width);
                const auto *p=decoded+(sy*width+sx)*3;
                const auto color=photo_pixels::rgb565(p[0],p[1],p[2],x,y);
                const size_t i=(size_t(y)*job.width+x)*2;
                job.pixels[i]=uint8_t(color);job.pixels[i+1]=uint8_t(color>>8);
            }
        }
        heap_caps_free(pixels);
    } else {
        if(!photo_pixels::rgb888To565InPlace(decoded,width,height,rgbBytes)) {heap_caps_free(pixels);return false;}
        memmove(pixels,decoded,imageBytes);
        if(auto *smaller=static_cast<uint8_t *>(heap_caps_realloc(pixels,imageBytes,Caps))) pixels=smaller;
        job.pixels=pixels;
    }
    return true;
}
void execute(Job &job) {
    Context context{job};std::unique_ptr<Input> input;
    if(stopping(job) || !readSource(job,context,input) || cancelled(&context) || !parseHeader(job,context,input.get())) return;
    auto &s=*job.source;
    if(s.progressive) {
        context.limit=180000000;
    }
    if(!input) input=std::make_unique<Input>(context); // Optional cache and progressive backing share one lease.
    job.fitWidth=photo_fit_width(s.width,s.height);
    job.view=photo_view_make(s.width,s.height,job.targetWidth,job.panX,job.panY);
    job.width=job.view.width;job.height=job.view.height;
    const size_t rgbBytes=photo_pixels::rgb888Bytes(job.width,job.height);
    if(!rgbBytes) {job.error="缩放受限，请选择较小倍率";return;}
    const frames::Access cache{input->card,input.get(),[](void *p){return static_cast<Input *>(p)->claimMutation();},&context,cancelled};
    bool ok=false;
    if(frames::contains(s,job.view,cache)) {
        // Reopening verifies all source bytes once. Subsequent pans in this
        // immutable Source session use generation/size/mtime checked by open().
        if(input->fresh && input->file && (!input->finish() || !input->rewind()))return;
        job.pixels=frames::load(s,job.view,cache);ok=job.pixels!=nullptr;job.cacheHit=ok;
    }
    std::unique_ptr<frames::Writer> writer;
    if(!ok && !cancelled(&context))writer.reset(new(std::nothrow) frames::Writer(s,job.view,cache));
    photo_sink *sink=writer?writer->sink():nullptr;
    if(!ok && !sink && s.data && !s.png && !s.progressive && !job.view.x && !job.view.y &&
       job.view.scaled_width==job.width && job.view.scaled_height==job.height) ok=decodeJpeg(job);
    if(!ok && !cancelled(&context)) {
        heap_caps_free(job.pixels);job.pixels=nullptr;
        // Free heap already excludes the old displayed frame. Reserve codec
        // work and system memory before allocating, including fragmentation.
        if(!canAllocate(rgbBytes,ReserveBytes+CodecRoom)) {
            job.error=job.targetWidth?"内存受限，无法继续放大":"图片解码内存不足";return;
        }
        job.pixels=static_cast<uint8_t *>(heap_caps_malloc(rgbBytes,Caps));
        if(job.pixels) {
            uint16_t w=0,h=0;
            if(!s.png) {
                int progressive=0;
                const int result=readJpeg(job,context,input.get(),job.pixels,&w,&h,&progressive,sink);
                ok=result==PHOTO_JPEG_OK;
                if(!ok && !job.error) job.error=jpegError(result);
                if(ok && (progressive!=0)!=s.progressive) {ok=false;job.error="图片编码已变化，请重新打开";}
            } else {
                const int result=readPng(job,context,input.get(),job.pixels,&w,&h,sink);
                ok=result==PHOTO_PNG_OK;
                if(!ok && !job.error) job.error=pngError(result);
            }
            if(ok && (w!=s.width || h!=s.height)) {ok=false;job.error="图片尺寸已变化，请重新打开";}
            ok=ok && !cancelled(&context) && photo_pixels::rgb888To565InPlace(job.pixels,job.width,job.height,rgbBytes,job.view.x,job.view.y);
            if(ok) {
                if(auto *smaller=static_cast<uint8_t *>(heap_caps_realloc(job.pixels,size_t(job.width)*job.height*2,Caps))) job.pixels=smaller;
            }
        }
    }
    if(ok && !job.cacheHit && input && input->file) ok=input->finish();
    if(ok && writer)writer->commit(s); // Optional cache failure never rejects a decoded frame.
    writer.reset(); // Close every cache FILE before releasing SD admission.
    if(input && !input->close()) ok=false;
    if(!ok || cancelled(&context)) {
        heap_caps_free(job.pixels);job.pixels=nullptr;
        if(!job.error) job.error="图片无法解码或内存不足，请缩小后重试";
    }
}
void worker(void *argument) {
    {
        std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(argument));
        auto job=std::move(*owner);owner.reset();
        {
            sys::Power::WorkGuard work;
            try {execute(*job);} catch(const std::bad_alloc &) {job->error="图片内存不足";}
        }
        job->done.store(true,std::memory_order_release);
    }
    active.store(false,std::memory_order_release);
    vTaskDeleteWithCaps(nullptr);
}
}
Source::~Source() {heap_caps_free(data);}
Job::~Job() {heap_caps_free(pixels);}
bool idle() {return !active.load(std::memory_order_acquire);}
std::shared_ptr<Job> start(const std::string &path,std::shared_ptr<Source> source,uint16_t targetWidth,int32_t panX,int32_t panY) {
    if(sys::Power::sleepPending()) return {};
    bool expected=false;
    if(!active.compare_exchange_strong(expected,true)) return {};
    std::shared_ptr<Job> job;
    try {
        job=std::make_shared<Job>();job->path=path;job->source=std::move(source);
        job->sdAccess=sd::requestAccess();job->targetWidth=targetWidth;job->panX=panX;job->panY=panY;
        auto *owner=new std::shared_ptr<Job>(job);
        if(xTaskCreateWithCaps(worker,"photo_decode",8192,owner,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS) return job;
        delete owner;
    } catch(const std::bad_alloc &) {}
    active.store(false,std::memory_order_release);
    return {};
}
}
