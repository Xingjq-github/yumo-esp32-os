#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace core::patterns {
constexpr unsigned LessonCount=26,StageCount=3;
constexpr size_t IndexBytes=16+LessonCount*16,MaxLessonBytes=24576;
struct Catalog {const char *id;const char *title;unsigned group;};
extern const Catalog Catalogs[LessonCount];
extern const unsigned char Package[];
extern const size_t PackageBytes;
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
inline uint32_t crc32(const uint8_t *p,size_t n){uint32_t v=~0u;while(n--){v^=*p++;for(int i=0;i<8;++i)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
struct Run {std::string output;std::vector<std::string> trace;};
struct Lesson {
    std::string id,title,intent,wire,boundary,question,why,caution,code;
    std::array<std::string,3> roles,options,answers;
    std::array<std::string,2> tasks;
    std::array<unsigned,2> targets{};
    std::array<Run,6> runs;
};
struct Progress {
    std::array<uint8_t,LessonCount> complete{};
    unsigned lesson=0,stage=0,design=0,option=0;
    bool started=false;
};
using Record=std::array<uint8_t,128>;
inline bool validProgress(const Progress &p){if(p.lesson>=LessonCount||p.stage>=3||p.design>=2||p.option>=3)return false;for(auto c:p.complete)if(c&~7u)return false;return true;}
inline Record encode(const Progress &p,uint32_t sequence){
    Record r{};std::memcpy(r.data(),"YMPROG1",7);put32(r.data()+8,sequence);
    r[12]=uint8_t(p.lesson);r[13]=uint8_t(p.stage);r[14]=uint8_t(p.design);r[15]=uint8_t(p.option);r[16]=p.started;
    std::memcpy(r.data()+24,p.complete.data(),LessonCount);put32(r.data()+124,crc32(r.data(),124));return r;
}
inline Progress decode(const Record &r){Progress p;p.lesson=r[12];p.stage=r[13];p.design=r[14];p.option=r[15];p.started=r[16]!=0;std::memcpy(p.complete.data(),r.data()+24,LessonCount);return p;}
inline bool valid(const Record &r){return !std::memcmp(r.data(),"YMPROG1",8)&&get32(r.data()+124)==crc32(r.data(),124)&&r[16]<=1&&validProgress(decode(r));}
inline int latest(const Record &a,const Record &b){if(!valid(a))return valid(b)?1:-1;if(!valid(b))return 0;return int32_t(get32(b.data()+8)-get32(a.data()+8))>0?1:0;}
bool parseLesson(const char *,size_t,unsigned,Lesson &);
}
