#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace apps::morse {
// International Latin Morse alphabet, digits and commonly used punctuation.
// ! & ; _ $ are common extensions to the ITU-R M.1677 punctuation repertoire.
struct Entry { char symbol; const char *code; unsigned group; };
inline constexpr Entry Entries[]={
 {'A',".-",1},{'B',"-...",1},{'C',"-.-.",1},{'D',"-..",1},{'E',".",1},
 {'F',"..-.",1},{'G',"--.",1},{'H',"....",1},{'I',"..",1},{'J',".---",1},
 {'K',"-.-",1},{'L',".-..",1},{'M',"--",1},{'N',"-.",1},{'O',"---",1},
 {'P',".--.",1},{'Q',"--.-",1},{'R',".-.",1},{'S',"...",1},{'T',"-",1},
 {'U',"..-",1},{'V',"...-",1},{'W',".--",1},{'X',"-..-",1},{'Y',"-.--",1},{'Z',"--..",1},
 {'0',"-----",2},{'1',".----",2},{'2',"..---",2},{'3',"...--",2},{'4',"....-",2},
 {'5',".....",2},{'6',"-....",2},{'7',"--...",2},{'8',"---..",2},{'9',"----.",2},
 {'.',".-.-.-",4},{',',"--..--",4},{'?',"..--..",4},{'\'',".----.",4},{'!',"-.-.--",4},
 {'/',"-..-.",4},{'(',"-.--.",4},{')',"-.--.-",4},{'&',".-...",4},{':',"---...",4},
 {';',"-.-.-.",4},{'=',"-...-",4},{'+',".-.-.",4},{'-',"-....-",4},{'_',"..--.-",4},
 {'"',".-..-.",4},{'$',"...-..-",4},{'@',".--.-.",4}
};
inline constexpr size_t Count=sizeof(Entries)/sizeof(*Entries), MaxInput=160, MaxText=96;
constexpr char decode(std::string_view code) {
 for(const auto &e:Entries)if(code==e.code)return e.symbol;
 return 0;
}
constexpr bool prefix(std::string_view code) {
 for(const auto &e:Entries)if(std::string_view(e.code).substr(0,code.size())==code)return true;
 return false;
}
struct Token { std::string_view code; char symbol; bool word; };
template<class Emit> void convert(std::string_view input,Emit emit) {
 size_t at=0;bool first=true;
 while(at<input.size()) {
  unsigned gap=0;while(at<input.size()&&input[at]==' '){++at;++gap;}
  const size_t start=at;while(at<input.size()&&input[at]!=' ')++at;
  if(start==at)break;
  if(!first&&gap>=2)emit(Token{{},' ',true});
  auto code=input.substr(start,at-start);emit(Token{code,decode(code),false});first=false;
 }
}
// Human keying is deliberately more forgiving than the 1:3 playback rhythm.
struct Keyer {
 static constexpr uint32_t DashMs=240, LetterMs=600, WordMs=1400, MaxHoldMs=4000;
 std::string code;
 uint32_t downAt=0,upAt=0;
 bool down=false,finished=false,overflow=false,wordSent=false;
 void reset(){code.clear();down=finished=overflow=wordSent=false;}
 void press(uint32_t now){if(down)return;if(finished)reset();down=true;downAt=now;}
 bool release(uint32_t now) {
  if(!down)return false;
  down=false;const uint32_t ms=now-downAt;
  if(ms<20)return false;
  if(code.size()<8)code+=ms>=DashMs?'-':'.';else overflow=true;
  upAt=now;return true;
 }
 bool poll(uint32_t now) {
  if(down||finished||code.empty()||uint32_t(now-upAt)<LetterMs)return false;
  finished=true;return true;
 }
 bool word(uint32_t now) {
  if(!finished||down||wordSent||uint32_t(now-upAt)<WordMs)return false;
  wordSent=true;return true;
 }
 char value()const{return overflow?0:decode(code);}
};
struct Practice {
 std::array<uint8_t,Count> items{};
 unsigned count=0,position=0;bool random=false;uint32_t seed=1;
 uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
 void shuffle(){for(unsigned i=count;i>1;--i){unsigned j=next()%i;auto t=items[i-1];items[i-1]=items[j];items[j]=t;}}
 void start(unsigned groups,bool mixed,uint32_t entropy) {
  count=position=0;random=mixed;seed=entropy?entropy:1;
  for(size_t i=0;i<Count;++i)if(Entries[i].group&groups)items[count++]=i;
  if(random)shuffle();
 }
 void move(int direction) {
  if(!count)return;
  if(direction<0)position=position?position-1:count-1;
  else if(++position>=count){position=0;if(random){const auto last=items[count-1];shuffle();if(count>1&&items[0]==last){auto t=items[0];items[0]=items[1];items[1]=t;}}}
 }
 const Entry &entry()const{return Entries[items[position]];}
};
static_assert(Count==54 && decode("...-..-")=='$' && decode("..--..")== '?');
}
