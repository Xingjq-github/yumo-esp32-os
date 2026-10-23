#include "core/text_progress.hpp"
#include "apps/text_paging.hpp"
namespace record=core::reading;
namespace paging=apps::text_paging;

constexpr bool checkedRecords()
{
    const record::Mark mark{record::fingerprint("book",4),27,987};
    const auto older=record::encode({mark.fingerprint,26,900},1);
    const auto current=record::encode(mark,2);
    const auto next=record::encode({mark.fingerprint,28,1100},3);
    if(record::latest(older,current)!=1 || record::decode(current).offset!=987) return false;
    for(unsigned byte=0;byte<current.size();++byte)
        for(unsigned bit=0;bit<8;++bit) {
            auto corrupt=current;corrupt[byte]^=1u<<bit;
            if(record::valid(corrupt) || record::latest(older,corrupt)!=0) return false;
        }
    // Power loss at every write byte: the untouched current slot always survives.
    for(unsigned bytes=0;bytes<=next.size();++bytes) {
        auto partial=older;
        for(unsigned i=0;i<bytes;++i) partial[i]=next[i];
        const int chosen=record::latest(partial,current);
        if(chosen<0) return false;
        const auto restored=record::decode(chosen==0?partial:current);
        if(restored.offset!=987 && restored.offset!=1100) return false;
    }
    return record::latest(record::encode(mark,UINT32_MAX),record::encode(mark,0))==1 &&
        record::latest({}, {})==-1 && record::fingerprint("Book",4)!=mark.fingerprint;
}
constexpr bool pageBoundaries()
{
    // Known measured-line boundaries isolate the actual page grouping/restoration logic.
    auto line=[](const char *text,size_t bytes) {
        size_t i=0;
        while(i<bytes && text[i]!='\n') ++i;
        return i<bytes?i+1:i;
    };
    const std::string text="one\n\n\xE4\xB8\xAD\nlast";
    const auto pages=paging::paginate(text,2,line);
    if(pages.size()!=2 || pages[0]!=0 || pages[1]!=5) return false;
    if(paging::restore(pages,text.size(),5)!=1 || paging::restore(pages,text.size(),8)!=1 ||
       paging::restore(pages,text.size(),UINT32_MAX)!=0) return false;
    if(paging::paginate("a\nb\n",2,line).size()!=1) return false;
    if(paging::paginate("",9,line).size()!=1) return false;
    if(paging::paginate("\n\n\n",1,line).size()!=3) return false;
    return paging::paginate(text,0,line).size()==4;
}
constexpr bool fullscreenRestoresExistingPosition()
{
    // Reflow an existing 9-line bookmark into the new 10-line viewport. Every
    // previous position must still land on a page containing its saved byte.
    std::string text;
    for(unsigned i=0;i<35;++i) text+="\xE4\xB8\xAD\n";
    auto line=[](const char *,size_t) {return size_t{4};};
    const auto oldPages=paging::paginate(text,paging::linesPerPage(203,22,0),line);
    const auto newPages=paging::paginate(text,paging::linesPerPage(240,22,0),line);
    if(oldPages.size()!=4 || newPages.size()!=4 || oldPages[1]!=36 || newPages[1]!=40) return false;
    for(size_t offset:oldPages) {
        const size_t page=paging::restore(newPages,text.size(),offset);
        const size_t end=page+1<newPages.size()?newPages[page+1]:text.size();
        if(newPages[page]>offset || end<=offset || newPages[page]%4) return false;
    }
    if(paging::restore(newPages,text.size(),40)!=1) return false;
    return paging::restore(newPages,text.size(),text.size()-1)==3;
}
static_assert(checkedRecords(),"Checked alternating positions must survive corruption and sequence rollover");
static_assert(pageBoundaries(),"Page grouping must preserve UTF-8/newlines and bound restored offsets");
static_assert(fullscreenRestoresExistingPosition(),"A layout change must retain the saved text position");
constexpr bool twelveLinesRestorePreviousLayouts()
{
    std::string text;
    for(unsigned i=0;i<53;++i) text+="\xE4\xB8\xAD\n";
    auto line=[](const char *,size_t) {return size_t{4};};
    const auto pages=paging::paginate(text,paging::linesPerPage(240,20,0),line);
    if(pages.size()!=5 || pages[1]!=48) return false;
    for(unsigned oldLines:{9u,10u,11u}) {
        const auto oldPages=paging::paginate(text,oldLines,line);
        for(size_t offset:oldPages) {
            const size_t page=paging::restore(pages,text.size(),offset);
            const size_t end=page+1<pages.size()?pages[page+1]:text.size();
            if(pages[page]>offset || offset>=end || pages[page]%4) return false;
        }
    }
    return pages.back()==192;
}
static_assert(twelveLinesRestorePreviousLayouts(),"12-row reading must restore 9/10/11-row bookmarks by byte position");
static_assert(paging::linesPerPage(240,20,0)==12);
static_assert(paging::linesPerPage(240,22,0)==10);
static_assert(paging::linesPerPage(240,22,2)==10);
static_assert(paging::linesPerPage(203,22,0)==9);
static_assert(paging::linesPerPage(203,22,2)==8);
static_assert(paging::linesPerPage(1,22,0)==1);
static_assert(record::RecordStride>=512 && record::RecordStride%512==0);
