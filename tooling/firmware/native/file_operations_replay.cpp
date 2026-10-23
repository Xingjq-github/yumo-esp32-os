#include "core/file_operations.hpp"
#include <cstring>
#include <cerrno>
extern "C" {
struct FileResult {int error,more,count,changed,steps;unsigned long long bytes;char names[32][256];};
__declspec(dllexport) void file_run(const char *root,int op,const char *source,const char *target,
    unsigned offset,int cancelAt,FileResult *out) {
    struct Context {int count=0,at=0;} context{0,cancelAt};
    core::files::Hooks hooks{&context,[](void *p) {
        auto c=static_cast<Context *>(p);++c->count;return c->at && c->count>=c->at;
    },nullptr};
    auto result=core::files::execute(root,{static_cast<core::files::Operation>(op),source,target,offset},hooks);
    *out={};out->error=result.error;out->more=result.more;out->count=int(result.entries.size());
    out->changed=result.changed;out->bytes=result.bytes;out->steps=context.count;
    for(size_t i=0;i<result.entries.size();++i)std::strncpy(out->names[i],result.entries[i].name.c_str(),255);
}
__declspec(dllexport) int file_name(const char *name){return core::files::validName(name);}
__declspec(dllexport) int file_loop_error(){return ELOOP;}
struct FileProperties {int error,valid,directory,complete;unsigned files,folders;unsigned long long bytes;long long modified;unsigned changed;};
__declspec(dllexport) void file_properties(const char *root,const char *path,int cancelAt,FileProperties *out) {
    struct Context {int count=0,at=0;} ctx{0,cancelAt};
    core::files::Hooks hooks{&ctx,[](void *p) {auto c=static_cast<Context *>(p);return c->at && ++c->count>=c->at;},nullptr};
    const auto result=core::files::execute(root,{core::files::Operation::Properties,path,{}},hooks);
    const auto &p=result.properties;
    *out={result.error,p.valid,p.directory,p.complete,p.files,p.folders,p.bytes,p.modified,result.changed};
}
__declspec(dllexport) void file_page(const char *root,const char *source,const char *anchor,int directory,int before,FileResult *out){
    core::files::Request request;request.source=source;request.anchor=anchor;request.anchorDirectory=directory;request.before=before;
    auto result=core::files::execute(root,request);*out={};out->error=result.error;out->more=result.more;out->count=int(result.entries.size());
    for(size_t i=0;i<result.entries.size();++i)std::strncpy(out->names[i],result.entries[i].name.c_str(),255);
}
__declspec(dllexport) int file_path(const char *root,const char *path,int allowRoot){return core::files::validPath(root,path,allowRoot);}
}
