#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace core::files {
inline constexpr size_t PageSize=32;
enum class Operation { List, Mkdir, Rename, Copy, Move, Remove, Properties };
constexpr bool mutates(Operation op) {return op!=Operation::List && op!=Operation::Properties;}
struct Entry {std::string name;bool directory=false;uint64_t bytes=0;};
struct Request {
    Operation operation=Operation::List;std::string source,target;size_t offset=0;
    std::string anchor;bool anchorDirectory=false,before=false;
};
struct Properties {
    bool valid=false,directory=false,complete=false;
    uint64_t bytes=0;
    unsigned files=0,folders=0;
    int64_t modified=0; // st_mtime; do not mistake FAT ctime for creation time.
};
struct Result {std::vector<Entry> entries;int error=0;bool more=false;uint64_t bytes=0;unsigned changed=0;Properties properties;};
struct Hooks {
    void *context=nullptr;
    bool (*cancel)(void *)=nullptr;
    void (*progress)(void *,uint64_t,unsigned)=nullptr;
};
bool validName(const std::string &name);
bool validPath(const std::string &root,const std::string &path,bool allowRoot=false);
// Filesystem-only engine, used by the SD worker and native synthetic tests.
// The caller holds an exclusive SD Session for every mutation.
Result execute(const std::string &root,const Request &request,Hooks hooks={});
}
