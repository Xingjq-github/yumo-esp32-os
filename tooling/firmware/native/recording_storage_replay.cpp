// All filesystem paths are confined to the runner's new synthetic-card folder.
#include "core/storage_browser.hpp"
#include "core/audio_recorder.hpp"
#include "core/wav_format.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <io.h>
#ifndef ESTALE
#define ESTALE 116 // Windows CRT compatibility for the production error branch.
#endif
static int fsync(int fd){return _commit(fd);}
template<class T> static tm *localtime_r(const T *seconds,tm *out) {
  const time_t value=*seconds;return localtime_s(out,&value)==0?out:nullptr;
}
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
namespace fs=std::filesystem;
namespace fake {
fs::path root;
bool sleeping=false,exclusive=true;uint64_t generation=9;
unsigned sessions=0,claims=0,removes=0,deletedTasks=0;int ioError=0;
std::string failStat;
fs::path map(const char *p) {
  const std::string path(p);
  CHECK(path=="/sdcard"||path.rfind("/sdcard/",0)==0);
  CHECK(path.find("..") == std::string::npos && path.find('\\')==std::string::npos);
  return path=="/sdcard"?root:root/path.substr(8);
}
int stat(const char *p,struct stat *st) {
  if(p==failStat){errno=EIO;return -1;}
  return ::stat(map(p).string().c_str(),st);
}
int remove(const char *p) {++removes;CHECK(sessions==1&&claims);return std::remove(map(p).string().c_str());}
DIR *opendir(const char *p){return ::opendir(map(p).string().c_str());}
int open(const char *p,int flags,int mode){return ::open(map(p).string().c_str(),flags|O_BINARY,mode);}
int gettimeofday(timeval *tv,void *){tv->tv_sec=1790740800;tv->tv_usec=123000;return 0;}
void put(const std::string &name,const std::string &data) {std::ofstream f(root/"recordings"/name,std::ios::binary);f<<data;CHECK(bool(f));}
std::map<std::string,std::string> contents() {
  std::map<std::string,std::string> result;
  for(const auto &item:fs::directory_iterator(root/"recordings"))if(item.is_regular_file()) {
    std::ifstream f(item.path(),std::ios::binary);
    result[item.path().filename().string()]={std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
  }
  return result;
}
}
namespace sys {struct Power {struct WorkGuard {};static bool sleepPending(){return fake::sleeping;}};}
static void vTaskDelay(unsigned){}
static unsigned pdMS_TO_TICKS(unsigned n){return n;}
static void vTaskDeleteWithCaps(void *){++fake::deletedTasks;}
namespace core::sd {
Session::Session(Access,bool,bool){held_=true;generation_=fake::generation;++fake::sessions;}
Session::~Session(){release();}
void Session::release(){if(held_){held_=false;--fake::sessions;}}
void Session::reportError(int e){fake::ioError=e;}
bool Session::claimMutation(){++fake::claims;return fake::exclusive;}
bool ensure(const char *path){return fs::is_directory(fake::map(path));}
// Match production child rules, used only as the path-boundary test double.
bool child(const std::string &p,const char *dir,const char *ext) {
  const std::string prefix=std::string(dir)+"/";
  if(p.rfind(prefix,0)||p.size()>255||p.find('\0')!=std::string::npos)return false;
  const auto name=p.substr(prefix.size());
  return name.size()>strlen(ext)&&name.find_first_of("/\\")==std::string::npos&&
         !strcasecmp(name.c_str()+name.size()-strlen(ext),ext);
}
}
namespace core {
std::string ensureUtf8(const char *s){return s;}
std::string decodeText(const std::string &s,bool){return s;}
reading::Mark readProgress(const std::string &,uint64_t,uint64_t){return {};}
std::atomic<unsigned> active{0};
constexpr size_t kMaxEntries=256,kMaxTextBytes=64*1024;
#include "storage.inc"
}
namespace core::recorder {
struct File {sd::Session &card;FILE *p=nullptr;~File(){if(p)fclose(p);}};
#include "create.inc"
}
static std::shared_ptr<core::StorageJob> run(bool remove=false,const core::FileEntry &entry={},uint64_t generation=9,bool cancel=false) {
  auto job=std::make_shared<core::StorageJob>();job->path=remove?entry.path:"/sdcard/recordings";
  job->media=remove?0:1;job->removeRecording=remove;job->cardGeneration=generation;
  job->deleteIdentity=entry.identity;job->cancelled=cancel;
  core::active=1;fake::claims=0;
  core::worker(new std::shared_ptr<core::StorageJob>(job));
  CHECK(job->done&&core::active==0&&fake::sessions==0);return job;
}
int main(int argc,char **argv) {
  CHECK(argc==2);fake::root=fs::canonical(argv[1]);CHECK(fake::root.filename()=="synthetic-card"&&fs::is_empty(fake::root));
  fs::create_directory(fake::root/"recordings");
  for(int i=0;i<120;++i){char n[64];snprintf(n,sizeof(n),"2026-09-01_12-00-00_000_%02d.wav",i);fake::put(n,std::string(i+44,char(i)));}
  fake::put("new-test.wav","new test");fake::put("unfinished.wav.part","pending");
  auto baseline=fake::contents();auto listing=run();CHECK(!listing->error&&listing->entries.size()==121);
  const auto target=*std::find_if(listing->entries.begin(),listing->entries.end(),[](const auto &e){return e.name=="new-test.wav";});
  CHECK(target.identity.valid&&target.identity.size==8);
  for(auto generation:{0u,8u,10u}){auto job=run(true,target,generation);CHECK(job->error==ENODEV&&fake::removes==0);}
  auto job=run(true,target,9,true);CHECK(fake::removes==0);
  fake::sleeping=true;job=run(true,target);CHECK(job->error==ECANCELED&&fake::removes==0);fake::sleeping=false;
  fake::exclusive=false;job=run(true,target);CHECK(job->error==EBUSY&&fake::removes==0);fake::exclusive=true;
  auto missing=target;missing.identity={};job=run(true,missing);CHECK(job->error==ESTALE&&fake::removes==0);
  missing=target;++missing.identity.modified;job=run(true,missing);CHECK(job->error==ESTALE&&fake::removes==0);
  fake::put("new-test.wav","replaced file must survive");auto changed=fake::contents();
  job=run(true,target);CHECK(job->error==ESTALE&&fake::removes==0&&fake::contents()==changed);
  fake::put("new-test.wav",baseline.at("new-test.wav"));listing=run();
  const auto current=*std::find_if(listing->entries.begin(),listing->entries.end(),[](const auto &e){return e.name=="new-test.wav";});
  for(const char *p:{"/sdcard/recordings/../history.wav","/sdcard/recordings/a/b.wav","/sdcard/other.wav","/sdcard/recordings/unfinished.wav.part"}) {
    auto invalid=current;invalid.path=p;job=run(true,invalid);CHECK(job->error==EINVAL&&fake::removes==0);
  }
  fake::failStat=current.path;job=run(true,current);CHECK(job->error==EIO&&fake::removes==0);fake::failStat.clear();
  auto expected=fake::contents();expected.erase("new-test.wav");
  job=run(true,current);CHECK(!job->error&&fake::removes==1&&fake::contents()==expected);
  job=run(true,current);CHECK(job->error==ENOENT&&fake::removes==1&&fake::contents()==expected);
  // A large directory can be truncated by the display limit; no files are
  // removed by listing. Preserve this distinction in the regression evidence.
  for(int i=0;i<160;++i)fake::put("extra-"+std::to_string(i)+".wav","extra");
  auto large=fake::contents();listing=run();CHECK(listing->truncated&&listing->entries.size()==256&&fake::contents()==large);
  // New recording naming: existing final/temporary files are never reused;
  // metadata read failures stop before any temporary file is created.
  {
    core::sd::Session card({});core::recorder::Job record(true);core::recorder::File f{card};std::string part;
    CHECK(core::recorder::create(record,f,part));CHECK(record.path.find("_00.wav")!=std::string::npos);
    fclose(f.p);f.p=nullptr;
    fake::put(fs::path(record.path).filename().string(),"history with same timestamp");
    auto prior=fake::contents();
    core::recorder::Job next(true);CHECK(core::recorder::create(next,f,part));CHECK(next.path.find("_01.wav")!=std::string::npos);
    fclose(f.p);f.p=nullptr;
    for(const auto &[name,data]:prior)CHECK(fake::contents().at(name)==data);
    auto after=fake::contents();fake::failStat=record.path;
    core::recorder::Job failing(true);CHECK(!core::recorder::create(failing,f,part));CHECK(!f.p&&fake::ioError==EIO&&fake::contents()==after);
    fake::failStat.clear();
  }
  CHECK(fake::sessions==0);
  printf("%u checks; exact-target deletion, stale metadata/card, busy/cancel/I/O rejection, large-list retention and recording name safety; synthetic filesystem only.\n",checks);
}
