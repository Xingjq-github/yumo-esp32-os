// Deterministic socket replay of the production independent ICMP worker.
#include "core/speed_test.hpp"
#include <winsock2.h>
#include <cassert>
#include <chrono>
#include <thread>
#include <deque>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
using namespace core::speed;
using esp_err_t=int;
constexpr int ESP_OK=0,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,pdPASS=1,MSG_DONTWAIT=0;
using socklen_t=int;
static const auto origin=std::chrono::steady_clock::now();
int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-origin).count()*5;}
void vTaskDelay(int ms){std::this_thread::sleep_for(std::chrono::microseconds(std::max(100,ms*200)));}
#define pdMS_TO_TICKS(n) (n)
std::atomic<int> workers{0};
int xTaskCreateWithCaps(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,int){++workers;std::thread([=]{fn(arg);--workers;}).detach();return pdPASS;}
void vTaskDeleteWithCaps(void *){}
unsigned esp_random(){return 712;}
void esp_fill_random(void *out,size_t n){std::memset(out,0x5a,n);}
struct wifi_ap_record_t {int rssi=-53;};
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){ap->rssi=-53;return ESP_OK;}
struct IP {uint32_t addr;};struct esp_netif_ip_info_t {IP ip,gw;};
int fakeNetif=1;bool wrongNetwork=false;
uint32_t address(const char *text){unsigned a=0,b=0,c=0,d=0;if(std::sscanf(text,"%u.%u.%u.%u",&a,&b,&c,&d)!=4)return INADDR_NONE;return a|(b<<8)|(c<<16)|(d<<24);}
void *esp_netif_get_handle_from_ifkey(const char *){return &fakeNetif;}
int esp_netif_get_ip_info(void *,esp_netif_ip_info_t *out){out->ip.addr=address(wrongNetwork?"192.168.1.9":"192.168.1.3");out->gw.addr=address("192.168.1.1");return ESP_OK;}
const char *esp_ip4addr_ntoa(const IP *,char *out,size_t n){std::snprintf(out,n,"192.168.1.1");return out;}
struct Reply {std::vector<uint8_t> bytes;uint32_t from=0;int64_t due=0;};
struct Socket {bool open=false;std::deque<Reply> replies;};
std::mutex netMutex;Socket sockets[64];int nextSocket=10;std::atomic<int> activeSockets{0};
int replyMode=0;bool failSocket=false;unsigned sends=0;
uint16_t sum(const uint8_t *p,unsigned n){uint32_t v=0;for(unsigned i=0;i+1<n;i+=2)v+=(p[i]<<8)|p[i+1];while(v>>16)v=(v&65535)+(v>>16);return uint16_t(~v);}
int fake_socket(int,int,int){std::lock_guard<std::mutex> lock(netMutex);if(failSocket){errno=ENOMEM;return -1;}int fd=nextSocket++;assert(fd<64);sockets[fd].open=true;++activeSockets;return fd;}
int fake_bind(int,const sockaddr *,int){return 0;}
int fake_setsockopt(int,int,int,const void *,int){return 0;}
int fake_close(int fd){std::lock_guard<std::mutex> lock(netMutex);assert(sockets[fd].open);sockets[fd]={};--activeSockets;return 0;}
int fake_sendto(int fd,const void *data,size_t n,int,const sockaddr *to,int){
 std::lock_guard<std::mutex> lock(netMutex);++sends;assert(n==40 && !sum(static_cast<const uint8_t *>(data),n));
 if(replyMode==1)return int(n);
 Reply reply;reply.bytes.resize(20+n);reply.bytes[0]=0x45;std::memcpy(reply.bytes.data()+20,data,n);reply.bytes[20]=0;
 auto echo=reply.bytes.data()+20;echo[2]=echo[3]=0;
 if(replyMode==2)echo[5]^=1;
 if(replyMode==3)echo[8]^=1;
 const auto checksum=sum(echo,n);echo[2]=checksum>>8;echo[3]=checksum;
 reply.from=reinterpret_cast<const sockaddr_in *>(to)->sin_addr.s_addr;
 reply.due=esp_timer_get_time()+((reply.from==address("1.1.1.1"))?50000:10000);
 sockets[fd].replies.push_back(std::move(reply));return int(n);
}
int fake_select(int,fd_set *reads,fd_set *,fd_set *,timeval *timeout){
 std::this_thread::sleep_for(std::chrono::microseconds(std::max<long>(50,timeout->tv_usec/5)));
 std::lock_guard<std::mutex> lock(netMutex);fd_set ready;FD_ZERO(&ready);
 const auto now=esp_timer_get_time();for(unsigned i=0;i<reads->fd_count;++i){const auto fd=reads->fd_array[i];if(!sockets[fd].replies.empty() && sockets[fd].replies.front().due<=now)FD_SET(fd,&ready);}
 *reads=ready;return int(ready.fd_count);
}
int fake_recvfrom(int fd,void *out,size_t size,int,sockaddr *from,socklen_t *){
 std::lock_guard<std::mutex> lock(netMutex);assert(!sockets[fd].replies.empty());auto reply=std::move(sockets[fd].replies.front());sockets[fd].replies.pop_front();
 assert(size>=reply.bytes.size());std::memcpy(out,reply.bytes.data(),reply.bytes.size());reinterpret_cast<sockaddr_in *>(from)->sin_addr.s_addr=reply.from;return int(reply.bytes.size());
}
#define socket fake_socket
#define bind fake_bind
#define setsockopt fake_setsockopt
#define close fake_close
#define sendto fake_sendto
#define select fake_select
#define recvfrom fake_recvfrom
#define inet_addr address
#include "production_probes.inc"
void waitForWorkers(){for(unsigned n=0;n<10000 && workers.load();++n)std::this_thread::sleep_for(std::chrono::microseconds(100));assert(!workers && !activeSockets);}
int main(){
 Record record;std::strcpy(record.ip,"192.168.1.3");
 {
  Probes probes(record);probes.stage(0);
  const auto deadline=esp_timer_get_time()+40000000;
  while(!probes.idleComplete() && esp_timer_get_time()<deadline)vTaskDelay(20);
  assert(probes.idleComplete());probes.snapshot(record);
  for(unsigned i=0;i<2;++i){const auto stats=probeStats(record.probes[i]);assert(stats.valid==20 && stats.attempted==20 && stats.median>0);}
  assert(record.rssiCount>0 && record.rssiMin==-53 && record.rssiMax==-53);
  probes.stage(1);vTaskDelay(1600);probes.stage(2);vTaskDelay(1600);probes.stage(-1);probes.stop();probes.snapshot(record);
  assert(probeStats(record.probes[2]).valid>0 && probeStats(record.probes[4]).valid>0);
  const auto frozen=record.probes[2].count;assert(frozen>=1 && frozen<=6);
 }
 waitForWorkers();
 for(int mode=1;mode<=3;++mode){
  replyMode=mode;
  {Probes probes(record);probes.stage(0);vTaskDelay(1800);probes.stop();probes.snapshot(record);assert(record.probes[0].count>0 && !probeStats(record.probes[0]).valid && !record.probes[0].error);}
  waitForWorkers();
 }
 replyMode=0;failSocket=true;
 {Probes probes(record);probes.stage(0);vTaskDelay(100);probes.stop();probes.snapshot(record);assert(record.probes[0].error==ENOMEM && !record.probes[0].count);}
 waitForWorkers();failSocket=false;wrongNetwork=true;
 {Probes probes(record);probes.stage(0);assert(probes.idleComplete());probes.stop();probes.snapshot(record);assert(record.probes[1].error==ENETDOWN);}
 waitForWorkers();wrongNetwork=false;replyMode=1;
 {Probes probes(record);probes.stage(1);vTaskDelay(100);const auto begin=esp_timer_get_time();probes.stop();assert(esp_timer_get_time()-begin<500000);}
 waitForWorkers();
 std::cout<<"Production ICMP: exact reply/source/payload checks, timeouts, three stages, RSSI, socket/setup failure and cancellation cleanup passed\n";
}