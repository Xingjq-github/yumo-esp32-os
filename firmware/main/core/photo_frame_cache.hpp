#pragma once
#include "photo_loader.hpp"
#include "photo_sink.h"
#include <cstdio>

namespace core::photo::frames {
struct Access {
    sd::Session &card;
    void *owner;
    bool (*claim)(void *);
    void *context;
    int (*cancelled)(void *);
    bool stopping() const {return cancelled(context)!=0;}
};
bool contains(const Source &,const photo_view &,const Access &);
uint8_t *load(const Source &,const photo_view &,const Access &);
// Entire scaled level, divided into 128x128 RGB565 tiles. Only eight rows of
// tiles are buffered at once; no full image allocation, C++ stays outside longjmp.
class Writer {
public:
    Writer(const Source &,const photo_view &,const Access &);
    ~Writer();
    photo_sink *sink() {return good_?&sink_:nullptr;}
    bool commit(const Source &);
    bool ready() const {return good_;}
private:
    Access access_;
    photo_view view_;
    FILE *file_=nullptr;
    uint8_t *memory_=nullptr,*band_=nullptr;
    uint64_t hashes_[128]{};
    uint32_t columns_=0,bandY_=0,pass_=0,nextY_=0;
    bool good_=false,owned_=false,complete_=false;
    char path_[96]{},temporary_[96]{};
    photo_sink sink_{};
    void row(uint32_t,unsigned);
    bool stripe(bool writing,uint32_t,unsigned);
    void fail() {good_=false;}
};
}
