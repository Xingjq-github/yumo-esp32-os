#include "core/chiptune_score.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace core::chip;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
static std::string one(std::string notes="O4 L4 A",std::string wave="pulse",std::string extra=""){
    return "{\"format\":\"chip-score\",\"version\":1,\"tempo\":120,\"tracks\":[{\"id\":\"lead\",\"wave\":\""+wave+"\",\"notes\":\""+notes+"\""+extra+"}]}";
}
static Score valid(const std::string &text){Score s;std::string e;const bool ok=parse(text.data(),text.size(),s,e);if(!ok)std::cerr<<e<<"\n";CHECK(ok);return s;}
static void invalid(const std::string &text){Score s;std::string e;CHECK(!parse(text.data(),text.size(),s,e));CHECK(!e.empty());CHECK(s.events.empty());}
static void put(std::ostream &o,unsigned n,unsigned bytes){while(bytes--){o.put(char(n&255));n>>=8;}}
int main(int argc,char **argv){
    CHECK(sizeof(Event)<=16);
    auto s=valid(one());CHECK(s.events.size()==1&&s.events[0].note==69&&s.ticks==480);
    s=valid(one("O2 C O6 B O4 C# D- L8 R C4. >C <C"));CHECK(s.events.size()==8);CHECK(s.events[2].note==s.events[3].note);CHECK(s.events[5].end-s.events[5].begin==720);
    valid("\xef\xbb\xbf"+one()+" \r\n");valid(one("l8 k h s r","percussion"));
    for(const auto &notes:{"","L3 C","L4. C","O1 C","O7 C","O2 C-","O6 B#","O2 < C","O6 > C","O 4 C","L 8 C","C..","R#","C&","[CEG]","C0","C3","C64","C999999999999999999999999999999999999999","P4","C,","C+","R-"})invalid(one(notes));
    for(const auto &notes:{"O4 K",">K","C","K#","S-","<H"})invalid(one(notes,"percussion"));
    for(const auto &extra:{",\"duty\":0.2",",\"volume\":-1",",\"volume\":2",",\"gate\":0",",\"gate\":2",",\"what\":1",",\"id\":\"again\""})invalid(one("C","pulse",extra));
    invalid(one("K","percussion",",\"gate\":0.8"));invalid(one("C","triangle",",\"duty\":0.5"));
    invalid(one("C","sine"));invalid("null");invalid("[]");invalid("{}");invalid(one()+"x");invalid(std::string(65537,' '));
    auto a=one();auto p=a.find("120");for(auto value:{"39","241","120.5","1e999","null","true"}){auto b=a;b.replace(p,3,value);invalid(b);}
    a=one();a.insert(1,"\"title\":\"中文音乐\",");CHECK(valid(a).title=="中文音乐");
    for(auto title:{"bad\\u0000title","bad\\ntitle","bad\\ttitle","bad\\u0001title","bad\\ud800title"}){auto b=one();b.insert(1,std::string("\"title\":\"")+title+"\",");invalid(b);}
    a=one();a.insert(1,"\"title\":\"\\\\u0000\",");valid(a);
    a=one();a.insert(1,"\"title\":\""+std::string(33,'x')+"\",");invalid(a);
    a=one();a.insert(1,"\"title\":\"\xc0\xaf\",");invalid(a);
    a=one();a.insert(1,"\"title\":\"x");invalid(a);
    invalid(std::string("[")+std::string(100,'['));
    a="[";for(unsigned i=0;i<10000;++i)a+="0,";a+="0]";invalid(a);
    a=one();a.insert(1,"\""+std::string(60000,'x')+"\":0,");{Score large;std::string error;CHECK(!parse(a.data(),a.size(),large,error));CHECK(error.size()<128);}
    a=one();for(size_t i=0;i<a.size();++i)invalid(a.substr(0,i));
    a=one();a.insert(20,1,'\0');invalid(a);
    std::string notes="L32 ";for(unsigned i=0;i<2048;++i)notes+="C ";s=valid(one(notes));CHECK(s.events.size()==2048);invalid(one(notes+"C"));
    notes="L1 ";for(unsigned i=0;i<300;++i)notes+="R ";s=valid(one(notes));CHECK(Synth(s).durationMs()==600000);invalid(one(notes+"R"));
    s=valid(one("L2 A","pulse",",\"gate\":1"));Synth synth(s);long long sum=0;double energy=0;int last=0,cross=0;
    for(unsigned i=0;i<Rate;++i){auto v=synth.next(false);CHECK(std::abs(int(v))<=32767);if(i>1000&&i<Rate-1000){sum+=v;energy+=double(v)*v;if(last<=0&&v>0)++cross;}last=v;}
    CHECK(std::abs(double(sum)/(Rate-2000))<25);CHECK(energy>100000000);CHECK(cross>=398&&cross<=402);synth.next(false);CHECK(synth.ended());
    for(unsigned tempo:{40u,73u,137u,239u,240u}){
        for(uint64_t tick=0;tick<480*600*tempo/60;tick+=37){
            const long double exact=static_cast<long double>(tick)*Rate*60/(tempo*Ticks);
            CHECK(std::fabs(static_cast<long double>(sampleAt(tick,tempo))-exact)<=.50001L);
        }
    }
    s=valid(one("O5 L32 C D E F G A B >C"));s.tempo=137;
    Synth looping(s);const auto end=sampleAt(uint64_t(s.ticks)*100,s.tempo);
    for(uint64_t i=0;i<end;++i)looping.next(true);
    looping.next(true);CHECK(looping.milliseconds()==0);CHECK(!looping.ended());
    s=valid(one("L8 K S H R","percussion"));Synth first(s),second(s);
    for(unsigned i=0;i<Rate*3;++i)CHECK(first.next(true)==second.next(true));
    for(int i=1;i<argc;++i){
        std::ifstream file(argv[i],std::ios::binary);CHECK(file.good());std::string data((std::istreambuf_iterator<char>(file)),{});auto song=valid(data);
        Synth renderer(song);std::ofstream wav(std::string(argv[i]).substr(std::string(argv[i]).find_last_of("/\\")+1)+".wav",std::ios::binary);
        const unsigned frames=unsigned(sampleAt(song.ticks,song.tempo));wav.write("RIFF",4);put(wav,36+frames*2,4);wav.write("WAVEfmt ",8);put(wav,16,4);put(wav,1,2);put(wav,1,2);put(wav,Rate,4);put(wav,Rate*2,4);put(wav,2,2);put(wav,16,2);wav.write("data",4);put(wav,frames*2,4);
        long long absolute=0;for(unsigned n=0;n<frames;++n){int16_t v=renderer.next(false);put(wav,uint16_t(v),2);absolute+=std::abs(int(v));}
        CHECK(absolute>10000);renderer.next(false);CHECK(renderer.ended());
        std::cout<<song.title<<": "<<song.events.size()<<" events, "<<renderer.durationMs()<<" ms\n";
    }
    std::cout<<checks<<" parser/DSP checks passed\n";
}
