#include "file_operations.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <new>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif
namespace core::files {
bool validName(const std::string &n) {
    if(n.empty() || n.size()>255 || n=="." || n==".." || n.back()=='.' || n.back()==' ')return false;
    for(unsigned char c:n)if(c<32 || c==127 || std::strchr("\\/:*?\"<>|",c))return false;
    return true;
}
bool validPath(const std::string &root,const std::string &path,bool allowRoot) {
    if(path==root)return allowRoot;
    if(path.size()>768 || path.rfind(root+"/",0)!=0)return false;
    size_t start=root.size()+1;
    while(start<path.size()) {
        const auto end=path.find('/',start);
        if(!validName(path.substr(start,end==std::string::npos?end:end-start)))return false;
        if(end==std::string::npos)return true;
        start=end+1;
    }
    return false;
}
namespace {
int failure() {return errno?errno:EIO;}
int makeDirectory(const char *path) {
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path,0775);
#endif
}
bool same(const std::string &a,const std::string &b) {return !strcasecmp(a.c_str(),b.c_str());}
bool inside(const std::string &a,const std::string &b) {
    return b.size()>a.size() && !strncasecmp(a.c_str(),b.c_str(),a.size()) && b[a.size()]=='/';
}
int inspect(const std::string &path,struct stat &st) {
#ifdef _WIN32
    const auto attrs=GetFileAttributesA(path.c_str());
    if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return ELOOP;
#endif
#if !defined(ESP_PLATFORM) && !defined(_WIN32)
    if(lstat(path.c_str(),&st)!=0)return failure();
#else
    if(stat(path.c_str(),&st)!=0)return failure();
#endif
    return S_ISREG(st.st_mode)||S_ISDIR(st.st_mode)?0:EINVAL;
}
struct Directory {
    DIR *value=nullptr;
    explicit Directory(const std::string &p):value(opendir(p.c_str())){}
    ~Directory(){if(value)closedir(value);}
};
struct File {
    FILE *value=nullptr;
    int *closeError=nullptr;
    ~File(){if(value && fclose(value)!=0 && closeError && !*closeError)*closeError=EIO;}
};
class Engine {
    const std::string &root;
    Hooks hooks;
    unsigned visited=0;
    std::unique_ptr<unsigned char[]> buffer;
    bool temporaryOwned=false;
    int closeError=0;
    bool stop(bool cancellable=true) {return cancellable && hooks.cancel && hooks.cancel(hooks.context);}
    void progress() {
        if(hooks.progress)hooks.progress(hooks.context,result.bytes,result.properties.valid?
            result.properties.files+result.properties.folders:result.changed);
    }
    int guard(unsigned depth,bool cleanup=false) {
        if(cleanup && hooks.cancel)(void)hooks.cancel(hooks.context); // yield, but finish our cleanup
        if(depth>16)return ELOOP;
        if(!cleanup && ++visited>20000)return E2BIG;
        return stop(!cleanup)?ECANCELED:0;
    }
    // Prevent aliases/escape through a parent; FAT has no symlinks. Host tests
    // reject them too, rather than silently traversing outside their test root.
    int parents(const std::string &path) {
        size_t end=root.size();
        while(end<path.size()) {
            struct stat st{};const int err=inspect(path.substr(0,end),st);
            if(err)return err;
            if(!S_ISDIR(st.st_mode))return ENOTDIR;
            end=path.find('/',end+1);if(end==std::string::npos)break;
        }
        return 0;
    }
    int vacant(const std::string &path) {
        struct stat st{};const int err=inspect(path,st);
        return err==ENOENT?0:err?err:EEXIST;
    }
    int tree(const std::string &source,const std::string &target,unsigned depth,bool preflight=false) {
        int err=guard(depth);if(err)return err;
        struct stat st{};err=inspect(source,st);if(err)return err;
        if(!validPath(root,source) || (!target.empty() && !validPath(root,target)))return EINVAL;
        if(S_ISDIR(st.st_mode)) {
            if(!preflight) {
                if(makeDirectory(target.c_str())!=0)return failure();
                if(depth==0)temporaryOwned=true;
            }
            Directory dir(source);if(!dir.value)return failure();
            for(;;) {
                errno=0;auto e=readdir(dir.value);if(!e)return errno;
                if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;
                const std::string name=e->d_name;
                if(!validName(name))return EINVAL;
                err=tree(source+"/"+name,target.empty()?std::string():target+"/"+name,depth+1,preflight);
                if(err)return err;
            }
        }
        if(preflight)return 0;
        File from,to;from.closeError=to.closeError=&closeError;
        from.value=fopen(source.c_str(),"rb");if(!from.value)return failure();
        // O_EXCL makes ownership of a temporary child unambiguous.
        int fd=open(target.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0664);if(fd<0)return failure();
        if(depth==0)temporaryOwned=true;
        to.value=fdopen(fd,"wb");if(!to.value){err=failure();close(fd);return err;}
        if(!buffer)buffer=std::make_unique<unsigned char[]>(4096);
        uint64_t copied=0;
        for(;;) {
            if(stop())return ECANCELED;
            const size_t n=fread(buffer.get(),1,4096,from.value);
            if(n && fwrite(buffer.get(),1,n,to.value)!=n)return failure();
            copied+=n;result.bytes+=n;progress();
            if(n<4096) {if(ferror(from.value))return failure();break;}
        }
        if(copied!=static_cast<uint64_t>(st.st_size))return EIO;
        if(fflush(to.value)!=0)return failure();
#ifdef _WIN32
        if(_commit(fileno(to.value))!=0)return failure();
#else
        if(fsync(fileno(to.value))!=0)return failure();
#endif
        auto f=to.value;to.value=nullptr;if(fclose(f)!=0)return failure();
        f=from.value;from.value=nullptr;if(fclose(f)!=0)return failure();
        ++result.changed;progress();return 0;
    }
    int removeTree(const std::string &path,unsigned depth,bool cleanup=false) {
        int err=guard(depth,cleanup);if(err)return err;
        struct stat st{};err=inspect(path,st);if(err)return err;
        if(S_ISDIR(st.st_mode)) {
            Directory dir(path);if(!dir.value)return failure();
            // Reopen after each deletion: FAT directory offsets may change.
            for(;;) {
                errno=0;auto e=readdir(dir.value);if(!e){err=errno;break;}
                if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;
                err=removeTree(path+"/"+e->d_name,depth+1,cleanup);if(err)break;
                rewinddir(dir.value);
            }
            if(err)return err;
            auto d=dir.value;dir.value=nullptr;if(closedir(d)!=0)return failure();
            if(rmdir(path.c_str())!=0)return failure();
        } else if(unlink(path.c_str())!=0)return failure();
        if(!cleanup){++result.changed;progress();}
        return 0;
    }
    int measure(const std::string &path,unsigned depth) {
        int err=guard(depth);if(err)return err;
        struct stat st{};err=inspect(path,st);if(err)return err;
        auto &info=result.properties;
        if(!depth) {
            info.valid=true;info.directory=S_ISDIR(st.st_mode);
            info.modified=static_cast<int64_t>(st.st_mtime);
        }
        if(S_ISDIR(st.st_mode)) {
            if(depth)++info.folders; // The selected folder is not its own child.
            progress();
            Directory dir(path);if(!dir.value)return failure();
            for(;;) {
                if(stop())return ECANCELED;
                errno=0;auto entry=readdir(dir.value);if(!entry){err=errno;break;}
                if(!std::strcmp(entry->d_name,".") || !std::strcmp(entry->d_name,".."))continue;
                const auto child=path+"/"+entry->d_name;
                if(!validPath(root,child))return EINVAL;
                err=measure(child,depth+1);if(err)return err;
            }
            auto handle=dir.value;dir.value=nullptr;
            if(closedir(handle)!=0 && !err)err=failure();
            return err;
        }
        if(st.st_size<0)return EIO;
        ++info.files;info.bytes+=static_cast<uint64_t>(st.st_size);
        result.bytes=info.bytes;progress();return 0;
    }
    void list(const Request &r) {
        Directory dir(r.source);if(!dir.value){result.error=failure();return;}
        auto less=[](const Entry &a,const Entry &b) {
            if(a.directory!=b.directory)return a.directory>b.directory;
            const int order=strcasecmp(a.name.c_str(),b.name.c_str());
            return order?order<0:a.name<b.name;
        };
        const bool reverse=r.before&&!r.anchor.empty();
        auto order=[&](const Entry &a,const Entry &b){return reverse?less(b,a):less(a,b);};
        Entry anchor{r.anchor,r.anchorDirectory,0};bool anchored=!r.anchor.empty();
        size_t skip=anchored?0:r.offset;
        // Keep at most one page plus its look-ahead. UI cursors need one scan;
        // offset-only callers use bounded passes rather than an unbounded index.
        result.entries.reserve(PageSize+1);
        for(;;) {
            result.entries.clear();rewinddir(dir.value);
            for(;;) {
                if(stop()){result.error=ECANCELED;return;}
                errno=0;auto entry=readdir(dir.value);
                if(!entry){result.error=errno;break;}
                if(!std::strcmp(entry->d_name,".")||!std::strcmp(entry->d_name,".."))continue;
                struct stat st{};const int err=inspect(r.source+"/"+entry->d_name,st);
                if(err){result.error=err;return;}
                Entry item{entry->d_name,bool(S_ISDIR(st.st_mode)),static_cast<uint64_t>(st.st_size)};
                if(anchored&&!order(anchor,item))continue;
                auto at=std::lower_bound(result.entries.begin(),result.entries.end(),item,order);
                const auto position=at-result.entries.begin();
                if(result.entries.size()==PageSize+1) {
                    if(at==result.entries.end())continue;
                    result.entries.pop_back();
                }
                result.entries.insert(result.entries.begin()+position,std::move(item));
            }
            if(result.error)return;
            if(skip) {
                const auto advance=std::min(skip,PageSize);
                if(advance>=result.entries.size()){result.entries.clear();result.more=false;return;}
                anchor=result.entries[advance-1];anchored=true;skip-=advance;continue;
            }
            result.more=reverse?true:result.entries.size()>PageSize;
            if(result.entries.size()>PageSize)result.entries.resize(PageSize);
            if(reverse)std::reverse(result.entries.begin(),result.entries.end());
            return;
        }
    }
public:
    Result result;
    Engine(const std::string &r,Hooks h):root(r),hooks(h){}
    void run(const Request &r) {
        if(!validPath(root,r.source,r.operation==Operation::List)){result.error=EINVAL;return;}
        if(stop()){result.error=ECANCELED;return;}
        result.error=parents(r.source);if(result.error)return;
        if(r.operation==Operation::List){list(r);return;}
        if(r.operation==Operation::Properties) {
            result.error=measure(r.source,0);
            result.properties.complete=!result.error;
            return;
        }
        if(r.operation==Operation::Mkdir) {
            if(makeDirectory(r.source.c_str())!=0)result.error=failure();else result.changed=1;
            return;
        }
        if(r.operation==Operation::Remove) {
            result.error=tree(r.source,{},0,true);visited=0;
            if(!result.error)result.error=removeTree(r.source,0);
            return;
        }
        if(!validPath(root,r.target) || same(r.source,r.target) || inside(r.source,r.target)) {result.error=EINVAL;return;}
        result.error=parents(r.target);if(result.error)return;
        result.error=vacant(r.target);if(result.error)return;
        if(r.operation==Operation::Move || r.operation==Operation::Rename) {
            struct stat st{};result.error=inspect(r.source,st);if(result.error)return;
            // Same-card rename is atomic, with no copy/delete window.
            if(std::rename(r.source.c_str(),r.target.c_str())!=0)result.error=failure();else result.changed=1;
            return;
        }
        if(r.operation!=Operation::Copy){result.error=EINVAL;return;}
        result.error=tree(r.source,{},0,true);visited=0;if(result.error)return;
        static std::atomic<unsigned> sequence{0};
        const auto parent=r.target.substr(0,r.target.find_last_of('/'));
        std::string temporary;
        for(unsigned i=0;i<128;++i) {
            char suffix[40];std::snprintf(suffix,sizeof(suffix),"/.yumo-copy-%08x",++sequence);
            temporary=parent+suffix;
            const int err=vacant(temporary);
            if(!err)break;
            temporary.clear();if(err!=EEXIST){result.error=err;return;}
        }
        if(temporary.empty()){result.error=EEXIST;return;}
        // Clean up only a path we successfully created, never an existing user
        // target. A power loss can leave this clearly named temporary item.
        try {result.error=tree(r.source,temporary,0);}
        catch(const std::bad_alloc &){result.error=ENOMEM;}
        if(closeError)result.error=closeError;
        if(!result.error && stop())result.error=ECANCELED;
        if(!result.error)result.error=vacant(r.target);
        if(!result.error && std::rename(temporary.c_str(),r.target.c_str())!=0)result.error=failure();
        if(result.error && temporaryOwned) {
            try {
                const int cleanupError=removeTree(temporary,0,true);
                if(cleanupError && cleanupError!=ENOENT)result.error=cleanupError;
            }catch(const std::bad_alloc &){result.error=ENOMEM;}
        }
    }
};
}
Result execute(const std::string &root,const Request &request,Hooks hooks) {
    Engine engine(root,hooks);
    try {engine.run(request);}catch(const std::bad_alloc &){engine.result.error=ENOMEM;}
    return std::move(engine.result);
}
}
