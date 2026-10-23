#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace core::mp3 {
inline constexpr const char *Directory="/sdcard/MP3";
inline constexpr size_t MaxTracks=512, MaxLyrics=1500, MaxLrcBytes=128*1024;
struct Lyric {uint32_t ms=0;std::string text;};
inline bool extension(const std::string &s,const char *suffix) {
    const size_t n=std::char_traits<char>::length(suffix);
    if(s.size()<n)return false;
    for(size_t i=0;i<n;++i) {
        char c=s[s.size()-n+i];if(c>='A'&&c<='Z')c+=32;
        if(c!=suffix[i])return false;
    }
    return true;
}
inline bool trackPath(const std::string &path) {
    const std::string prefix=std::string(Directory)+"/";
    return path.compare(0,prefix.size(),prefix)==0 && path.size()>prefix.size()+4 &&
        path.find_first_of("/\\",prefix.size())==std::string::npos && extension(path,".mp3");
}
inline std::string shortText(std::string text,size_t bytes=240) {
    if(text.size()>bytes){while(bytes && (uint8_t(text[bytes])&0xc0)==0x80)--bytes;text.resize(bytes);}
    for(auto &c:text)if(uint8_t(c)<32 && c!='\t')c=' ';
    return text;
}
inline bool timestamp(const std::string &s,uint32_t &ms) {
    size_t i=0;uint32_t minute=0,second=0,fraction=0,digits=0;
    while(i<s.size()&&s[i]>='0'&&s[i]<='9') {
        if(++digits>4)return false;
        minute=minute*10+s[i++]-'0';
    }
    if(!digits||i>=s.size()||s[i++]!=':')return false;
    digits=0;
    while(i<s.size()&&s[i]>='0'&&s[i]<='9') {
        if(++digits>2)return false;
        second=second*10+s[i++]-'0';
    }
    if(!digits||second>=60)return false;
    if(i<s.size()&&(s[i]=='.'||s[i]==':')) {
        ++i;digits=0;
        while(i<s.size()&&s[i]>='0'&&s[i]<='9') {
            if(++digits>3)return false;
            fraction=fraction*10+s[i++]-'0';
        }
        if(!digits)return false;
        for(;digits<3;++digits)fraction*=10;
    }
    if(i!=s.size())return false;
    ms=(minute*60+second)*1000+fraction;return true;
}
// Input is already decoded to UTF-8. Accept repeated timestamps and a global
// signed offset, then stable-sort so repeated/translated lines remain together.
inline std::vector<Lyric> parseLrc(const std::string &input) {
    std::vector<Lyric> result;int64_t offset=0;
    if(input.size()>MaxLrcBytes*3)return result;
    size_t start=0;
    while(start<input.size()) {
        size_t end=input.find('\n',start);if(end==std::string::npos)end=input.size();
        std::string line=input.substr(start,std::min(end-start,size_t(4096)));start=end+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        if(line.compare(0,8,"[offset:")==0) {
            char *tail=nullptr;const auto value=std::strtoll(line.c_str()+8,&tail,10);
            if(tail && *tail==']')offset=std::clamp<int64_t>(value,-3600000,3600000);
            continue;
        }
        std::vector<uint32_t> times;size_t pos=0;
        while(pos<line.size()&&line[pos]=='['&&times.size()<16) {
            const auto close=line.find(']',pos);if(close==std::string::npos)break;
            uint32_t ms=0;if(!timestamp(line.substr(pos+1,close-pos-1),ms))break;
            times.push_back(ms);pos=close+1;
        }
        const auto text=shortText(line.substr(pos));
        for(auto ms:times)if(result.size()<MaxLyrics)result.push_back({ms,text});
    }
    // LRC positive offset advances lyrics; negative offset delays them.
    for(auto &line:result)line.ms=uint32_t(std::clamp<int64_t>(int64_t(line.ms)-offset,0,UINT32_MAX));
    std::stable_sort(result.begin(),result.end(),[](const Lyric&a,const Lyric&b){return a.ms<b.ms;});
    return result;
}
inline int lyricAt(const std::vector<Lyric>&lyrics,uint32_t ms) {
    auto it=std::upper_bound(lyrics.begin(),lyrics.end(),ms,[](uint32_t t,const Lyric&l){return t<l.ms;});
    if(it==lyrics.begin())return -1;
    --it;while(it!=lyrics.begin()&&(it-1)->ms==it->ms)--it;
    return int(it-lyrics.begin());
}
// Average in 32 bits, rather than discarding a stereo channel or clipping L+R.
inline int16_t mono(int16_t left,int16_t right){return int16_t((int32_t(left)+right)/2);}
}
