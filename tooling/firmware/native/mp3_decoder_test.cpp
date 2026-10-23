#include "core/mp3_model.hpp"
#include "minimp3_ex.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <climits>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
struct Input {
 std::vector<uint8_t> data;size_t at=0,reads=0;
 static size_t read(void *p,size_t n,void *arg){auto &s=*static_cast<Input*>(arg);n=std::min(n,s.data.size()-s.at);std::memcpy(p,s.data.data()+s.at,n);s.at+=n;++s.reads;return n;}
 static int seek(uint64_t n,void *arg){auto &s=*static_cast<Input*>(arg);if(n>s.data.size())return -1;s.at=n;return 0;}
};
static void model() {
 using namespace core::mp3;
 CHECK(trackPath("/sdcard/MP3/a.Mp3"));CHECK(!trackPath("/sdcard/a.mp3"));CHECK(!trackPath("/sdcard/MP3/../a.mp3"));CHECK(!trackPath("/sdcard/MP3/a/b.mp3"));CHECK(!trackPath("/sdcard/MP3/a.wav"));
 CHECK(mono(32767,32767)==32767);CHECK(mono(-32768,-32768)==-32768);CHECK(mono(32767,-32767)==0);
 uint32_t t=0;CHECK(timestamp("123:59.123",t)&&t==7439123);CHECK(timestamp("00:01.2",t)&&t==1200);CHECK(!timestamp("1:60",t));CHECK(!timestamp("99999:01",t));CHECK(!timestamp("1:20x",t));
 auto l=parseLrc("\xef\xbb\xbf[ar:雨墨]\n[00:02.50][00:01.1]你好\r\n[00:03]世界\n[offset:-200]\n[00:03]translation\n");
 CHECK(l.size()==4&&l[0].ms==1300&&l[1].ms==2700&&l[2].ms==3200);CHECK(l[0].text=="你好");CHECK(lyricAt(l,1299)==-1);CHECK(lyricAt(l,1300)==0);CHECK(lyricAt(l,UINT_MAX)==2);
 CHECK(parseLrc("[offset:200]\n[00:01]ahead")[0].ms==800);
 CHECK(parseLrc(std::string(MaxLrcBytes*3+1,'x')).empty());
 std::string large;for(int i=0;i<2000;++i)large+="[0:00]a\n";CHECK(parseLrc(large).size()==MaxLyrics);
 CHECK(shortText("中文中文",5)=="中");CHECK(shortText("a\rb\nc")=="a b c");
}
int main(int argc,char **argv) {
 model();CHECK(argc>1);unsigned files=0;
 for(int a=1;a<argc;++a) {
  std::cerr<<argv[a]<<"\n";FILE *f=fopen(argv[a],"rb");CHECK(f);fseek(f,0,SEEK_END);auto bytes=ftell(f);rewind(f);Input in;in.data.resize(bytes);CHECK(fread(in.data.data(),1,bytes,f)==size_t(bytes));fclose(f);
  mp3dec_io_t io{Input::read,&in,Input::seek,&in};auto d=std::make_unique<mp3dec_ex_t>();
  int rc=mp3dec_ex_open_cb(d.get(),&io,MP3D_SEEK_TO_SAMPLE);CHECK(!rc);CHECK(d->info.channels>=1&&d->info.channels<=2&&d->info.layer==3);CHECK(d->info.hz>=8000&&d->info.hz<=48000);CHECK(d->samples>0&&d->samples<10000000);
  std::vector<int16_t> pcm(d->samples);size_t total=0;
  while(total<pcm.size()) {auto n=mp3dec_ex_read(d.get(),pcm.data()+total,std::min(size_t(1152),pcm.size()-total));if(!n)break;total+=n;}
  // Deliberately concatenated format changes are rejected before an output
  // buffer can be interpreted with the wrong rate or channel count.
  const std::string path=argv[a];
  if(path.find("l3-he_mode.bit")!=std::string::npos||path.find("l3-nonstandard-he_44_48khz.bit")!=std::string::npos) {
   CHECK(d->last_error==MP3D_E_DECODE);CHECK(total>0&&total<pcm.size());mp3dec_ex_close(d.get());++files;continue;
  }
  CHECK(!d->last_error);CHECK(total==pcm.size());
  for(int percent:{0,20,50,90}) {
   const uint64_t target=(d->samples*percent/100)/d->info.channels*d->info.channels;
   CHECK(mp3dec_ex_seek(d.get(),target)==0);int16_t b[512];auto n=mp3dec_ex_read(d.get(),b,512);CHECK(n>0);
   for(size_t k=0;k<n;++k)CHECK(std::abs(int(b[k])-pcm[target+k])<=2);
  }
  mp3dec_ex_close(d.get());++files;
  // Broken file/tag input must complete under the same bounded decoder.
  in.data.resize(std::min(in.data.size(),size_t(93)));in.at=0;
  rc=mp3dec_ex_open_cb(d.get(),&io,MP3D_SEEK_TO_SAMPLE);mp3dec_ex_close(d.get());
 }
 std::cout<<"{\"checks\":"<<checks<<",\"files\":"<<files<<",\"passed\":true}\n";
}
