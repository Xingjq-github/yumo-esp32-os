#include "chiptune_score.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <memory>
#include <cstdio>

namespace core::chip {
namespace {
bool utf8(const char *s,size_t size,unsigned &count) {
    count=0;
    for(size_t i=0;i<size;++count) {
        const uint8_t a=s[i++];uint32_t cp=a;unsigned n=0;
        if(a<0x80){if(a<0x20&&a!='\n'&&a!='\r'&&a!='\t')return false;continue;}
        if(a>=0xc2&&a<=0xdf){n=1;cp=a&31;}else if(a>=0xe0&&a<=0xef){n=2;cp=a&15;}
        else if(a>=0xf0&&a<=0xf4){n=3;cp=a&7;}else return false;
        if(i+n>size)return false;
        for(unsigned k=0;k<n;++k){uint8_t b=s[i++];if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if((n==1&&cp<128)||(n==2&&cp<2048)||(n==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }
    return true;
}
// Preflight prevents cJSON's recursive parser/DOM consuming unbounded stack or
// heap on hostile files. Strict objects below contain at most 4*6+5 fields.
bool preflight(const char *s,size_t n,std::string &err) {
    unsigned cps=0;if(!utf8(s,n,cps)){err="乐谱不是有效UTF-8";return false;}
    int depth=0;unsigned fields=0,nodes=0;bool str=false,escape=false;
    for(size_t i=0;i<n;++i) {
        const char c=s[i];
        if(str){
            if(escape){
                if(c=='u' && i+4<n && !std::memcmp(s+i+1,"0000",4)){err="不允许空字符";return false;}
                escape=false;
            }else if(c=='\\')escape=true;else if(c=='"')str=false;else if(static_cast<unsigned char>(c)<32){err="JSON字符串含控制字符";return false;}
        }else if(c=='"')str=true;
        else if(c=='{'||c=='['){if(++depth>4){err="JSON嵌套过深";return false;}if(++nodes>64){err="JSON节点过多";return false;}}
        else if(c==',' && ++nodes>64){err="JSON节点过多";return false;}
        else if(c=='}'||c==']'){if(--depth<0){err="JSON括号错误";return false;}}
        else if(c==':' && ++fields>40){err="JSON字段过多";return false;}
    }
    return true;
}
bool fields(const cJSON *o,const char *const *allowed,unsigned n,std::string &err) {
    if(!cJSON_IsObject(o)){err="需要JSON对象";return false;}
    for(auto p=o->child;p;p=p->next){
        bool found=false;for(unsigned i=0;i<n;++i)if(p->string&&!std::strcmp(p->string,allowed[i]))found=true;
        if(!found){err="未知字段，请检查字段名称";return false;}
        for(auto q=o->child;q!=p;q=q->next)if(!std::strcmp(q->string,p->string)){err="重复字段: ";err+=p->string;return false;}
    }
    return true;
}
const cJSON *get(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
bool number(const cJSON *p,double lo,double hi){return cJSON_IsNumber(p)&&std::isfinite(p->valuedouble)&&p->valuedouble>=lo&&p->valuedouble<=hi;}
bool denominator(unsigned n){return n==1||n==2||n==4||n==8||n==16||n==32;}
bool notes(const char *s,Track &t,Score &out,std::string &err) {
    const size_t len=std::strlen(s);if(len>16384){err="notes超过16KiB";return false;}
    size_t i=0;unsigned octave=4,den=4,tick=0;t.first=out.events.size();
    auto fail=[&](const char *why){char pos[48];std::snprintf(pos,sizeof(pos)," / 字符 %u: ",unsigned(i));err=t.id+pos+why;return false;};
    auto digits=[&](unsigned &v){size_t begin=i;v=0;while(i<len&&s[i]>='0'&&s[i]<='9'){if(v>10000)return false;v=v*10+unsigned(s[i++]-'0');}return i>begin;};
    while(i<len){
        const unsigned char raw=s[i];if(raw==' '||raw=='\t'||raw=='\r'||raw=='\n'){++i;continue;}
        char c=char(std::toupper(raw));++i;
        if(c=='L'||c=='O'){
            unsigned v=0;if(!digits(v))return fail("状态指令需要连续数字");
            if(c=='L'){if(!denominator(v))return fail("时值仅支持1/2/4/8/16/32");den=v;}
            else {if(t.wave==Wave::Percussion||v<2||v>6)return fail("八度只支持O2至O6音高轨道");octave=v;}continue;
        }
        if(c=='<'||c=='>'){
            if(t.wave==Wave::Percussion||(c=='<'&&octave==2)||(c=='>'&&octave==6))return fail("八度超出范围");
            octave+=c=='>'?1:-1;continue;
        }
        int pitch=0;
        if(t.wave==Wave::Percussion){if(c=='K')pitch=1;else if(c=='S')pitch=2;else if(c=='H')pitch=3;else if(c!='R')return fail("打击轨只支持K/S/H/R/L");}
        else if(c!='R') {
            constexpr char letters[]="CDEFGAB";
            const char *letter=std::strchr(letters,c);if(!letter)return fail("不支持的音符或指令");
            constexpr int semitones[]={0,2,4,5,7,9,11};pitch=12*(int(octave)+1)+semitones[letter-letters];
            if(i<len&&(s[i]=='#'||s[i]=='-'))pitch+=s[i++]=='#'?1:-1;
            if(pitch<36||pitch>95)return fail("音高超出C2至B6");
        }
        unsigned d=den;if(i<len&&std::isdigit(static_cast<unsigned char>(s[i]))){if(!digits(d)||!denominator(d))return fail("音符时值无效");}
        unsigned ticks=Ticks*4/d;if(i<len&&s[i]=='.'){ticks=ticks*3/2;++i;}
        if(out.events.size()>=MaxEvents)return fail("全曲事件超过2048个");
        if(uint64_t(tick+ticks)*60>uint64_t(out.tempo)*Ticks*600)return fail("单轮超过10分钟");
        out.events.push_back({tick,tick+ticks,uint8_t(pitch)});tick+=ticks;
    }
    t.count=out.events.size()-t.first;if(!t.count)return fail("轨道没有音符或休止符");out.ticks=std::max(out.ticks,tick);return true;
}
float blep(float phase,float step){
    if(phase<step){float x=phase/step;return x+x-x*x-1;}
    if(phase>1-step){float x=(phase-1)/step;return x*x+x+x+1;}
    return 0;
}
}
bool parse(const char *data,size_t bytes,Score &out,std::string &error) {
    out=Score{};error.clear();
    try {
        if(!data||!bytes||bytes>MaxFile){error="文件为空或超过64KiB";return false;}
        if(bytes>=3&&!std::memcmp(data,"\xef\xbb\xbf",3)){data+=3;bytes-=3;}
        if(!preflight(data,bytes,error))return false;
        const char *end=nullptr;
        std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_ParseWithLengthOpts(data,bytes,&end,false),cJSON_Delete);
        if(!root){unsigned line=1,col=1;for(auto p=data;end&&p<end&&p<data+bytes;++p){if(*p=='\n'){++line;col=1;}else ++col;}char msg[96];std::snprintf(msg,sizeof(msg),"JSON错误 行%u 列%u",line,col);error=msg;return false;}
        while(end<data+bytes&&(*end==' '||*end=='\t'||*end=='\r'||*end=='\n'))++end;
        if(end!=data+bytes){error="JSON后有多余内容";return false;}
        constexpr const char *keys[]={"format","version","title","tempo","loop","tracks"};
        if(!fields(root.get(),keys,6,error))return false;
        auto format=get(root.get(),"format"),version=get(root.get(),"version"),tempo=get(root.get(),"tempo"),title=get(root.get(),"title"),loop=get(root.get(),"loop"),tracks=get(root.get(),"tracks");
        if(!cJSON_IsString(format)||std::strcmp(format->valuestring,"chip-score")){error="format必须是chip-score";return false;}
        if(!number(version,1,1)){error="仅支持version 1";return false;}
        if(!number(tempo,40,240)||std::floor(tempo->valuedouble)!=tempo->valuedouble){error="tempo必须是40至240整数";return false;}
        Score score;score.tempo=unsigned(tempo->valuedouble);score.events.reserve(MaxEvents);
        if(title){unsigned n=0;if(!cJSON_IsString(title)||!utf8(title->valuestring,std::strlen(title->valuestring),n)||n>32){error="title最多32个有效字符";return false;}score.title=title->valuestring;for(unsigned char c:score.title)if(c<32||c==127){error="title不能包含控制字符";return false;}}
        if(loop){if(!cJSON_IsBool(loop)){error="loop需要布尔值";return false;}score.loop=cJSON_IsTrue(loop);}
        if(!cJSON_IsArray(tracks)||cJSON_GetArraySize(tracks)<1||cJSON_GetArraySize(tracks)>4){error="需要1至4条轨道";return false;}
        score.count=cJSON_GetArraySize(tracks);unsigned counts[3]{};
        constexpr const char *tkeys[]={"id","wave","duty","volume","gate","notes"};
        for(unsigned i=0;i<score.count;++i){
            auto o=cJSON_GetArrayItem(tracks,i);auto &t=score.tracks[i];
            if(!fields(o,tkeys,6,error))return false;
            auto id=get(o,"id"),wave=get(o,"wave"),duty=get(o,"duty"),volume=get(o,"volume"),gate=get(o,"gate"),seq=get(o,"notes");
            if(!cJSON_IsString(id)||!id->valuestring[0]||std::strlen(id->valuestring)>16){error="轨道id需要1至16位英文数字或下划线";return false;}
            t.id=id->valuestring;for(unsigned char c:t.id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')){error="轨道id含不支持字符";return false;}
            for(unsigned j=0;j<i;++j)if(t.id==score.tracks[j].id){error="重复轨道id: "+t.id;return false;}
            if(!cJSON_IsString(wave)){error="wave需要音色名称";return false;}
            if(!std::strcmp(wave->valuestring,"pulse"))t.wave=Wave::Pulse;
            else if(!std::strcmp(wave->valuestring,"triangle"))t.wave=Wave::Triangle;
            else if(!std::strcmp(wave->valuestring,"percussion"))t.wave=Wave::Percussion;
            else {error="音色只支持pulse/triangle/percussion";return false;}
            if(++counts[unsigned(t.wave)]>(t.wave==Wave::Pulse?2u:1u)){error="最多双脉冲/单三角/单打击轨";return false;}
            if(duty){if(t.wave!=Wave::Pulse||!number(duty,.125,.5)||(duty->valuedouble!=.125&&duty->valuedouble!=.25&&duty->valuedouble!=.5)){error="duty只用于pulse:0.125/0.25/0.5";return false;}t.duty=duty->valuedouble;}
            if(gate){if(t.wave==Wave::Percussion||!number(gate,.1,1)){error="gate只用于音高轨，范围0.1至1";return false;}t.gate=gate->valuedouble;}
            if(volume){if(!number(volume,0,1)){error="volume范围0至1";return false;}t.volume=volume->valuedouble;}
            if(!cJSON_IsString(seq)||!notes(seq->valuestring,t,score,error)) {if(error.empty())error="notes需要字符串";return false;}
        }
        out=std::move(score);return true;
    }catch(const std::bad_alloc &){out=Score{};error="内存不足，乐谱未加载";return false;}
}
uint64_t sampleAt(uint64_t tick,unsigned tempo){return (tick*uint64_t(Rate)*60+tempo*Ticks/2)/(tempo*Ticks);}
void Synth::round(){
    roundStart_=sampleAt(roundTick_,score_.tempo);roundEnd_=sampleAt(roundTick_+score_.ticks,score_.tempo);
    for(unsigned i=0;i<4;++i){voices_[i]=Voice{};voices_[i].noise=0x1234abcdu+i*101u;}
}
void Synth::reset(){sample_=roundTick_=0;ended_=false;round();}
unsigned Synth::milliseconds()const{return unsigned((sample_-roundStart_)*1000/Rate);}
unsigned Synth::durationMs()const{return unsigned(sampleAt(score_.ticks,score_.tempo)*1000/Rate);}
float Synth::voice(unsigned i){
    auto &v=voices_[i];const auto &t=score_.tracks[i];
    while(v.event<t.count){
        if(!v.end){const auto &e=score_.events[t.first+v.event];v.begin=sampleAt(roundTick_+e.begin,score_.tempo);v.end=sampleAt(roundTick_+e.end,score_.tempo);}
        if(sample_<v.end)break;
        ++v.event;v.phase=0;v.step=0;v.filter=0;v.end=0;
    }
    if(v.event==t.count)return 0;
    const auto &e=score_.events[t.first+v.event];if(!e.note)return 0;
    const auto begin=v.begin,end=v.end;
    const unsigned age=unsigned(sample_-begin),slot=unsigned(end-begin);
    unsigned life=unsigned(slot*t.gate);
    if(t.wave==Wave::Percussion)life=std::min(slot,e.note==1?Rate*14/100:e.note==2?Rate*10/100:Rate*4/100);
    if(age>=life||!life)return 0;
    const unsigned attack=std::max(1u,std::min(Rate*2/1000,life/4)),release=std::max(1u,std::min(Rate*5/1000,life/4));
    float envelope=std::min(1.f,float(age)/attack)*std::min(1.f,float(life-age)/release),value=0;
    if(t.wave==Wave::Percussion){
        float tail=1-float(age)/life;envelope*=tail*tail;
        v.noise^=v.noise<<13;v.noise^=v.noise>>17;v.noise^=v.noise<<5;
        const float noise=float(v.noise&65535)/32768.f-1;
        if(e.note==1){v.step=(48.f+120.f*tail*tail)/Rate;value=std::sin(v.phase*6.283185307f);}
        else if(e.note==2){v.step=180.f/Rate;v.filter+=.32f*(noise-v.filter);value=.75f*v.filter+.25f*std::sin(v.phase*6.283185307f);}
        else {v.step=0;float previous=v.filter;v.filter+=.45f*(noise-v.filter);value=(noise-previous)*.6f;}
    }else {
        if(!v.step)v.step=440.f*std::pow(2.f,(int(e.note)-69)/12.f)/Rate;
        if(t.wave==Wave::Pulse){
            value=(v.phase<t.duty?1.f:0.f)-t.duty;
            float second=v.phase-t.duty;if(second<0)second+=1;
            value+=.5f*(blep(v.phase,v.step)-blep(second,v.step));
            value/=std::max(t.duty,1-t.duty);
        }else value=1-4*std::fabs(v.phase-.5f);
    }
    v.phase+=v.step;if(v.phase>=1)v.phase-=1;
    return value*t.volume*envelope;
}
int16_t Synth::next(bool loop){
    if(ended_||!score_.ticks)return 0;
    if(sample_>=roundEnd_){if(!loop){ended_=true;return 0;}roundTick_+=score_.ticks;round();}
    float value=0;for(unsigned i=0;i<score_.count;++i)value+=voice(i);
    // Whole-song fade joins all voices at a loop boundary without added time.
    const auto fade=uint64_t(Rate*5/1000);
    value*=std::min(1.f,float(sample_-roundStart_)/fade)*std::min(1.f,float(roundEnd_-sample_)/fade);
    ++sample_;return int16_t(std::max(-32767.f,std::min(32767.f,value*7000.f)));
}
} // namespace core::chip
