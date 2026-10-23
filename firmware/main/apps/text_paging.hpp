#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace apps::text_paging {
constexpr unsigned linesPerPage(int height,int fontHeight,int spacing) {
    return height<fontHeight || fontHeight<=0 || spacing<0?1:
        static_cast<unsigned>((height+spacing)/(fontHeight+spacing));
}
// nextLine uses the same LVGL font/wrapping function as the visible label.
template<class NextLine>
constexpr std::vector<size_t> paginate(const std::string &text,unsigned lines,NextLine nextLine) {
    std::vector<size_t> pages{0};
    size_t offset=0; unsigned row=0;
    while(offset<text.size()) {
        size_t bytes=nextLine(text.c_str()+offset,text.size()-offset);
        if(!bytes || bytes>text.size()-offset) break;
        offset+=bytes;
        if(++row>=std::max(1u,lines) && offset<text.size()) {
            pages.push_back(offset); row=0;
        }
    }
    return pages;
}
constexpr size_t restore(const std::vector<size_t> &pages,size_t bytes,size_t offset) {
    if(pages.empty() || offset>=bytes) return 0;
    return static_cast<size_t>(std::upper_bound(pages.begin(),pages.end(),offset)-pages.begin()-1);
}
} // namespace apps::text_paging
