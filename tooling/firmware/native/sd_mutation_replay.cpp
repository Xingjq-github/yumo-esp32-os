// Real SD admission/session implementation; all board operations are fakes.
#include "core/sd_media.hpp"
#include <atomic>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <mutex>
#include <strings.h>
#include <sys/stat.h>
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_NOT_FINISHED=1,ESP_ERR_NO_MEM=2;
static int card=1;static bool terminal=false;
static int *bsp_sdcard_get_handle(){return card?&card:nullptr;}
static int bsp_sdcard_ready(){return 0;}
static int bsp_sdcard_unmount(){card=0;return 0;}
static int bsp_sdcard_mount(){card=1;return 0;}
static int bsp_sdcard_quiesce(){return 0;}
static int sdmmc_get_status(int *){return 0;}
static int64_t esp_timer_get_time(){return 100;}
namespace sys {struct Power {struct WorkGuard {};static bool sleepPending(){return false;}static bool sleepTerminal(){return terminal;}static bool sleepUiReady(){return true;}};}
namespace core {bool storageIdle(){return true;}namespace recorder {bool idle(){return true;}}}
#include "sd_media.inc"
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 using namespace core::sd;
 CHECK(managedDirectory(Patterns));CHECK(managedDirectory(Recordings));
 CHECK(managedDirectory(Games));CHECK(managedDirectory(Screenshots));
 CHECK(!managedDirectory(nullptr));CHECK(!managedDirectory("/sdcard/patterns/../other"));
 errno=0;CHECK(!ensure("/sdcard/unknown")&&errno==EINVAL);
 for(int n=0;n<200;++n){
  auto access=requestAccess();Session first(access,false);CHECK(first);
  {Session reader(access,false);CHECK(reader);CHECK(!first.claimMutation());}
  CHECK(first.claimMutation());CHECK(!first.claimMutation());
  {Session denied(access,false);CHECK(!denied&&denied.error()==EBUSY);}
  first.release();{Session after(access,false);CHECK(after);CHECK(after.claimMutation());}
  {Session released(access,false);CHECK(released);}
 }
 auto old=requestAccess();{
  Session writer(old,false);CHECK(writer&&writer.claimMutation());CHECK(requestEject());pollEject();CHECK(state()==State::Ejecting);
  Session denied(old,false);CHECK(!denied);
 }
 pollEject();CHECK(state()==State::Ejected&&card==0);
 {Session stale(old,false);CHECK(!stale);}
 CHECK(requestUsb());CHECK(claimUsb());{Session denied(requestAccess(),false);CHECK(!denied);}
 releaseUsb(true);CHECK(state()==State::Ejected);
 std::printf("{\"passed\":true,\"checks\":%u,\"mutation_cycles\":200,\"real_sd_used\":false}\n",checks);
}
