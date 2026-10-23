#pragma once
#include "knowledge_format.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
namespace core::knowledge {
inline constexpr const char *Directory="/sdcard/programming";
inline constexpr const char *Package="/sdcard/programming/library.ykb";
inline constexpr const char *StatePath="/sdcard/programming/state.bin";
struct Entry {
    uint64_t id=0;
    const char *title="",*group="",*search="",*version="",*shortTitle="",*source="";
    uint32_t offset=0,bytes=0,crc=0;unsigned category=0,sections=0;
};
struct Library {
    std::array<uint8_t,HeaderBytes> header{};
    // One compact PSRAM allocation; no per-entry string allocations.
    uint8_t *index=nullptr;
    uint32_t count=0,indexBytes=0,dataBytes=0,revision=0;
    uint64_t generation=0;
    ~Library();
    Library()=default;Library(const Library &)=delete;Library &operator=(const Library &)=delete;
    Entry entry(unsigned i)const;
    int find(uint64_t id)const;
};
struct Section {uint32_t id=0,offset=0,bytes=0,crc=0;unsigned kind=0,language=0,firstLine=0;std::string title;};
struct Article {std::vector<Section> sections;std::vector<uint64_t> related;};
enum class Operation {Open,Read};
struct Job {
    Operation operation=Operation::Open;sd::Access access;
    std::shared_ptr<Library> library;unsigned entry=0,section=0;
    Article article;std::string block;std::vector<std::string> blocks;bool whole=false;
    State state;bool stateDamaged=false;
    int error=0;std::atomic<bool> cancelled{false},done{false};
};
struct Save {
    State state;sd::Access access;uint64_t generation=0;int error=0;
    std::atomic<bool> done{false};
};
std::shared_ptr<Job> open();
std::shared_ptr<Job> read(std::shared_ptr<Library> library,unsigned entry,unsigned section,bool whole=false);
std::shared_ptr<Save> save(const State &state,uint64_t generation,sd::Access access);
bool idle();
const char *errorText(int error);
}
