#include "knowledge_store.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <new>
#include <unistd.h>
#include <sys/stat.h>
namespace core::knowledge {
namespace {
std::atomic<unsigned> active{0};
std::atomic<bool> reading{false};
std::mutex saveMutex,saveIo;
std::shared_ptr<Save> pending,current;
bool saveRunning=false;
struct File {FILE *p=nullptr;explicit File(const char *path,const char *mode):p(fopen(path,mode)){}~File(){if(p)fclose(p);}bool close(){if(!p)return true;auto f=p;p=nullptr;return fclose(f)==0;}};
bool fetch(FILE *file,uint32_t offset,void *target,size_t bytes) {
    return fseek(file,long(offset),SEEK_SET)==0 && fread(target,1,bytes,file)==bytes;
}
bool stop(Job &j){return j.cancelled.load() || sys::Power::sleepPending() || sd::busy();}
bool stringAt(const uint8_t *data,unsigned total,unsigned offset,unsigned lower,unsigned limit) {
    if(offset<lower || offset>=total)return false;
    const auto end=static_cast<const uint8_t *>(memchr(data+offset,0,std::min(total-offset,limit+1)));
    return end && utf8(reinterpret_cast<const char *>(data+offset),end-data-offset);
}
void stateRecords(FILE *f,StateRecord &a,StateRecord &b) {
    if(!fetch(f,0,a.data(),a.size()))a={};
    if(!fetch(f,StateStride,b.data(),b.size()))b={};
}
void loadState(Job &j) {
    {std::lock_guard<std::mutex> lock(saveMutex);
        const auto latest=pending?pending:current;
        if(latest && latest->generation==j.library->generation){j.state=latest->state;return;}}
    std::lock_guard<std::mutex> lock(saveIo);
    File f(StatePath,"rb");if(!f.p){if(errno!=ENOENT)j.stateDamaged=true;return;}
    auto a=std::make_unique<StateRecord>(),b=std::make_unique<StateRecord>();stateRecords(f.p,*a,*b);
    const auto slot=latestState(*a,*b);if(slot<0){j.stateDamaged=true;return;}
    j.state=decodeState(slot==0?*a:*b);
}
void openLibrary(Job &j,sd::Session &card) {
    File f(Package,"rb");if(!f.p){j.error=errno;return;}
    auto lib=std::make_shared<Library>();auto &h=lib->header;
    if(!fetch(f.p,0,h.data(),h.size())){j.error=EBADMSG;return;}
    if(memcmp(h.data(),"YUMOREF1",8) || get32(h.data()+60)!=crc32(h.data(),60)){j.error=EBADMSG;return;}
    if(get32(h.data()+8)!=1){j.error=EPROTONOSUPPORT;return;}
    lib->revision=get32(h.data()+12);lib->count=get32(h.data()+16);
    lib->indexBytes=get32(h.data()+20);lib->dataBytes=get32(h.data()+24);
    struct stat st{};
    if(!lib->count || lib->count>MaxEntries || lib->indexBytes<lib->count*EntryBytes || lib->indexBytes>MaxIndex ||
       !lib->dataBytes || lib->dataBytes>MaxData || fstat(fileno(f.p),&st)!=0 ||
       uint64_t(st.st_size)!=HeaderBytes+lib->indexBytes+lib->dataBytes){j.error=EBADMSG;return;}
    lib->index=static_cast<uint8_t *>(heap_caps_malloc(lib->indexBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!lib->index){j.error=ENOMEM;return;}
    if(!fetch(f.p,HeaderBytes,lib->index,lib->indexBytes)||crc32(lib->index,lib->indexBytes)!=get32(h.data()+28)){j.error=EBADMSG;return;}
    uint32_t end=0;
    for(unsigned i=0;i<lib->count;++i) {
        auto p=lib->index+i*EntryBytes;
        for(unsigned at:{8u,12u,16u,20u,40u,44u})if(!stringAt(lib->index,lib->indexBytes,get32(p+at),lib->count*EntryBytes,at==16?1024:512)){j.error=EBADMSG;return;}
        auto e=lib->entry(i);
        if(!e.id || e.category>=6 || !e.sections || e.sections>MaxSections || !range(e.offset,e.bytes,lib->dataBytes) ||
           e.offset!=end || e.bytes<16+e.sections*SectionBytes){j.error=EBADMSG;return;}
        for(unsigned k=0;k<i;++k)if(lib->entry(k).id==e.id){j.error=EBADMSG;return;}
        end=e.offset+e.bytes;
        if(i%32==0){vTaskDelay(1);if(stop(j)){j.error=ECANCELED;return;}}
    }
    if(end!=lib->dataBytes){j.error=EBADMSG;return;}
    // Complete streaming integrity pass, 4 KiB scratch instead of an in-RAM DOM.
    std::array<uint8_t,4096> scratch{};uint32_t crc=~0u;
    for(uint32_t offset=0;offset<lib->dataBytes;) {
        if(stop(j)){j.error=ECANCELED;return;}
        unsigned n=std::min<unsigned>(scratch.size(),lib->dataBytes-offset);
        if(!fetch(f.p,HeaderBytes+lib->indexBytes+offset,scratch.data(),n)){j.error=EIO;return;}
        crc=crcStep(crc,scratch.data(),n);offset+=n;vTaskDelay(1);
    }
    if(~crc!=get32(h.data()+32)){j.error=EBADMSG;return;}
    lib->generation=card.generation();j.library=std::move(lib);loadState(j);
    auto &s=j.state;
    s.favorites.erase(std::remove_if(s.favorites.begin(),s.favorites.end(),[&j](uint64_t id){return j.library->find(id)<0;}),s.favorites.end());
    s.recent.erase(std::remove_if(s.recent.begin(),s.recent.end(),[&j](const Mark &m){return j.library->find(m.id)<0;}),s.recent.end());
}
void readBlock(Job &j,sd::Session &card) {
    auto lib=j.library;if(!lib || lib->generation!=card.generation() || j.entry>=lib->count){j.error=ESTALE;return;}
    File f(Package,"rb");if(!f.p){j.error=errno;return;}
    std::array<uint8_t,HeaderBytes> h{};
    if(!fetch(f.p,0,h.data(),h.size()) || h!=lib->header){j.error=ESTALE;return;}
    const auto e=lib->entry(j.entry);const uint32_t base=HeaderBytes+lib->indexBytes+e.offset;
    std::array<uint8_t,16> head{};
    if(!fetch(f.p,base,head.data(),head.size())||memcmp(head.data(),"YKE1",4)){j.error=EBADMSG;return;}
    unsigned ns=get16(head.data()+4),nr=get16(head.data()+6),nt=get32(head.data()+8);
    const unsigned lower=16+ns*SectionBytes+nr*8;
    if(ns!=e.sections||nr>MaxRelated||nt<lower||nt>MaxToc||nt>e.bytes){j.error=EBADMSG;return;}
    std::vector<uint8_t> toc(nt);
    if(!fetch(f.p,base,toc.data(),toc.size())){j.error=EIO;return;}
    uint32_t end=nt;
    for(unsigned i=0;i<ns;++i) {
        auto p=toc.data()+16+i*SectionBytes;Section s;
        s.id=get32(p);s.offset=get32(p+4);s.bytes=get32(p+8);s.crc=get32(p+12);s.kind=p[20];s.language=p[21];s.firstLine=get16(p+22);
        unsigned title=get32(p+16);
        if(!s.id||s.offset!=end||!s.bytes||s.bytes>MaxBlock||!range(s.offset,s.bytes,e.bytes)||s.kind>1||s.language>4||
           !stringAt(toc.data(),nt,title,lower,96)){j.error=EBADMSG;return;}
        for(const auto &old:j.article.sections)if(old.id==s.id){j.error=EBADMSG;return;}
        end=s.offset+s.bytes;s.title=reinterpret_cast<const char *>(toc.data()+title);j.article.sections.push_back(std::move(s));
    }
    if(end!=e.bytes){j.error=EBADMSG;return;}
    for(unsigned i=0;i<nr;++i){auto id=get64(toc.data()+16+ns*SectionBytes+i*8);if(lib->find(id)>=0)j.article.related.push_back(id);}
    if(j.section>=ns)j.section=0;
    if(j.whole) {
        // Only the selected article, bounded independently of the package size.
        // Read all sections on the storage worker, never from LVGL callbacks.
        size_t total=0;for(const auto &s:j.article.sections)total+=s.bytes;
        if(total>64*1024){j.error=EFBIG;return;}
        j.blocks.reserve(ns);
        for(const auto &s:j.article.sections) {
            if(stop(j)){j.error=ECANCELED;return;}
            std::string text(s.bytes,'\0');
            if(!fetch(f.p,base+s.offset,text.data(),s.bytes)){j.error=EIO;return;}
            if(crc32(reinterpret_cast<const uint8_t *>(text.data()),s.bytes)!=s.crc || !blockText(text,s.kind!=0)){j.error=EBADMSG;return;}
            j.blocks.push_back(std::move(text));vTaskDelay(1);
        }
        return;
    }
    const auto &s=j.article.sections[j.section];j.block.resize(s.bytes);
    if(stop(j)){j.error=ECANCELED;return;}
    if(!fetch(f.p,base+s.offset,j.block.data(),s.bytes)){j.error=EIO;return;}
    if(crc32(reinterpret_cast<const uint8_t *>(j.block.data()),s.bytes)!=s.crc || !blockText(j.block,s.kind!=0)){j.error=EBADMSG;j.block.clear();}
}
void reader(void *arg) {
    {
        std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(arg));auto j=std::move(*owner);owner.reset();
        try {
            sys::Power::WorkGuard work;sd::Session card(j->access);
            if(!card)j->error=card.error();else if(stop(*j))j->error=ECANCELED;
            else {if(j->operation==Operation::Open)openLibrary(*j,card);else readBlock(*j,card);card.reportError(j->error);}
        }catch(const std::bad_alloc &){j->error=ENOMEM;}
        j->done.store(true,std::memory_order_release);
    }
    reading.store(false,std::memory_order_release);--active;vTaskDeleteWithCaps(nullptr);
}
std::shared_ptr<Job> start(std::shared_ptr<Job> j) {
    if(sys::Power::sleepPending() || reading.exchange(true))return {};
    auto owner=new(std::nothrow) std::shared_ptr<Job>(j);
    ++active;
    if(owner && xTaskCreateWithCaps(reader,"knowledge_read",12288,owner,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return j;
    delete owner;--active;reading.store(false);return {};
}
void writeState(Save &j) {
    std::lock_guard<std::mutex> lock(saveIo);
    File f(StatePath,"r+b");
    if(!f.p && errno==ENOENT) {
        f.p=fopen(StatePath,"w+b");
        if(f.p){std::array<uint8_t,512> zero{};for(unsigned i=0;i<2*StateStride/zero.size();++i)if(fwrite(zero.data(),1,zero.size(),f.p)!=zero.size()){j.error=EIO;return;}}
    }
    if(!f.p){j.error=errno;return;}
    auto a=std::make_unique<StateRecord>(),b=std::make_unique<StateRecord>();stateRecords(f.p,*a,*b);
    if(ferror(f.p)){j.error=EIO;return;}
    int slot=latestState(*a,*b);const uint32_t seq=slot<0?1:get32((slot==0?*a:*b).data()+4)+1;
    // Use the unused record allocation for encoding; preserve the valid sector.
    encodeStateInto(*b,j.state,seq);clearerr(f.p);
    if(fseek(f.p,slot==0?StateStride:0,SEEK_SET)!=0||fwrite(b->data(),1,b->size(),f.p)!=b->size()||
       fflush(f.p)!=0||fsync(fileno(f.p))!=0)j.error=EIO;
    if(!f.close())j.error=EIO;
}
void saver(void *) {
    while(true) {
        std::shared_ptr<Save> j;
        {std::lock_guard<std::mutex> lock(saveMutex);if(!pending){saveRunning=false;break;}j=std::move(pending);current=j;}
        try {
            sys::Power::WorkGuard work;sd::Session card(j->access,true,true);
            if(!card)j->error=card.error();else if(card.generation()!=j->generation)j->error=ESTALE;
            else {writeState(*j);card.reportError(j->error);}
        }catch(const std::bad_alloc &){j->error=ENOMEM;}
        j->done.store(true,std::memory_order_release);
        {std::lock_guard<std::mutex> lock(saveMutex);current.reset();}
    }
    --active;vTaskDeleteWithCaps(nullptr);
}
}
Library::~Library(){heap_caps_free(index);}
Entry Library::entry(unsigned i)const {
    Entry e;if(!index||i>=count)return e;const auto p=index+i*EntryBytes;
    auto str=[this,p](unsigned n){return reinterpret_cast<const char *>(index+get32(p+n));};
    e.id=get64(p);e.title=str(8);e.group=str(12);e.search=str(16);e.version=str(20);e.shortTitle=str(40);e.source=str(44);
    e.offset=get32(p+24);e.bytes=get32(p+28);e.crc=get32(p+32);e.category=get16(p+36);e.sections=get16(p+38);return e;
}
int Library::find(uint64_t id)const {for(unsigned i=0;i<count;++i)if(entry(i).id==id)return int(i);return -1;}
bool idle(){return active.load(std::memory_order_acquire)==0;}
std::shared_ptr<Job> open() {try{auto j=std::make_shared<Job>();j->access=sd::requestAccess(true);return start(j);}catch(const std::bad_alloc &){return {};}}
std::shared_ptr<Job> read(std::shared_ptr<Library> lib,unsigned entry,unsigned section,bool whole) {
    try{auto j=std::make_shared<Job>();j->operation=Operation::Read;j->library=std::move(lib);j->entry=entry;j->section=section;j->whole=whole;j->access=sd::requestAccess();return start(j);}catch(const std::bad_alloc &){return {};}
}
std::shared_ptr<Save> save(const State &state,uint64_t generation,sd::Access access) {
    if(!generation)return {};
    try {
        auto j=std::make_shared<Save>();j->state=state;j->generation=generation;j->access=access;
        std::lock_guard<std::mutex> lock(saveMutex);
        if(pending){pending->error=ECANCELED;pending->done.store(true,std::memory_order_release);}pending=j;
        if(saveRunning)return j;
        saveRunning=true;++active;
        if(xTaskCreateWithCaps(saver,"knowledge_save",16384,nullptr,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return j;
        saveRunning=false;pending.reset();--active;
    }catch(const std::bad_alloc &){}return {};
}
const char *errorText(int e) {
    switch(e){case ENOENT:return "未找到知识包。请将 programming 目录放入SD卡根目录。";
    case ENOMEM:return "内存不足，请退出后重试。";case EPROTONOSUPPORT:return "知识包格式不兼容，请更换知识包。";
    case EFBIG:return "条目内容过长，请检查知识包。";
    case EBADMSG:return "知识包无效，请重新复制完整文件";case ESTALE:case ENODEV:return "SD卡或知识包已变化，请重新加载。";
    case ECANCELED:return "读取已取消，可返回或重试。";case EBUSY:return "SD卡正在使用，请稍后重试。";
    case ENOSPC:return "SD卡空间不足，阅读记录未保存。";default:return "SD卡读取失败，请检查后重试。";}
}
}
