// Run the production HTTP handlers against synthetic sockets/vault and a real
// isolated host filesystem. Platform calls only are replaced by the adapter.
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <io.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include "cJSON.h"
#include "core/file_operations.hpp"
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NO_MEM=0x101;
constexpr int HTTP_GET=0,HTTP_POST=1,HTTP_PUT=2,HTTP_DELETE=3,HTTPD_RESP_USE_STRLEN=-1;
static uint32_t htonl(uint32_t n){unsigned char bytes[]={static_cast<unsigned char>(n>>24),static_cast<unsigned char>(n>>16),static_cast<unsigned char>(n>>8),static_cast<unsigned char>(n)};uint32_t out;std::memcpy(&out,bytes,4);return out;}
static uint32_t ntohl(uint32_t n){const auto *b=reinterpret_cast<const unsigned char*>(&n);return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
namespace fake {bool sleeping=false,available=true,exclusive=false,loginOk=true,taskFail=false;int users=0,errors=0,unlocks=0;int64_t now=1000000;uint32_t local=htonl(0xc0a80401),peer=htonl(0xc0a80402);
// 0=IPv4, 1=mapped IPv6, 2=native IPv6, 3=IPv4-compatible IPv6, 4=unknown, 5=truncated IPv6, 6=truncated IPv4.
int localMode=1,peerMode=1;bool localError=false,peerError=false;
}
namespace sys {struct Power {static bool sleepPending(){return fake::sleeping;}struct WorkGuard{};};}
namespace core::sd {
constexpr const char *Root="./sdcard";
struct Access{};Access requestAccess(bool){return {};}
struct Session {
 bool held=false;
 explicit Session(Access){held=fake::available;if(held)++fake::users;}
 ~Session(){if(held)--fake::users;fake::exclusive=false;}
 explicit operator bool()const{return held;}int error()const{return EBUSY;}
 bool claimMutation(){if(fake::users!=1||fake::exclusive)return false;fake::exclusive=true;return true;}
 void reportError(int e){if(e==EIO)++fake::errors;}
};}
namespace vault {
void wipe(void *p,size_t n){std::memset(p,0,n);}void clear(std::string &s){wipe(s.data(),s.size());s.clear();}
struct Entry {std::string id,title,username,password,note,category,target;};
struct Session {std::vector<Entry> entries;};
enum class Operation {WebUnlock};enum class StartResult{Started,Busy};
struct Job {explicit Job(Operation){}std::string master,error;std::atomic<bool>done{false},cancelled{false};bool ok=false;std::shared_ptr<Session>output;};
bool validPin(const char *p,size_t n){return n>=4&&n<=12&&std::string(p,n).find_first_not_of("0123456789")==std::string::npos;}
StartResult start(std::shared_ptr<Job> j){++fake::unlocks;j->ok=fake::loginOk&&j->master=="1234";clear(j->master);if(j->ok){j->output=std::make_shared<Session>();j->output->entries.push_back({"1","Synthetic <title>","user","Secret123!","note","work","Unit"});j->output->entries.push_back({"2","Code","legacy-account","123456","note","other","Door"});}j->error="wrong pin";j->done=true;return StartResult::Started;}
void wipeJson(cJSON *p){if(!p)return;if(cJSON_IsString(p)&&p->valuestring)wipe(p->valuestring,std::strlen(p->valuestring));for(auto c=p->child;c;c=c->next)wipeJson(c);}
}
#include "vault_text.inc"
#include "lan_web_protocol.inc"
#define LWIP_IPV4 1
#define LWIP_IPV6 1
using u8_t=uint8_t;using u16_t=uint16_t;using u32_t=uint32_t;using in_addr_t=uint32_t;using in_port_t=uint16_t;using sa_family_t=uint8_t;using socklen_t=unsigned;
#include "socket_types.inc"
constexpr int AF_INET=2,AF_INET6=10;
static int socketAddress(sockaddr *s,socklen_t *size,uint32_t ip,int mode,bool error){
 if(error)return -1;
 sockaddr_storage result{};socklen_t length;
 if(mode==0||mode==6){sockaddr_in v4{};v4.sin_len=sizeof(v4);v4.sin_family=AF_INET;v4.sin_addr.s_addr=ip;length=mode==6?sizeof(v4)-1:sizeof(v4);std::memcpy(&result,&v4,length);}
 else{sockaddr_in6 v6{};v6.sin6_len=sizeof(v6);v6.sin6_family=mode==4?99:AF_INET6;
  if(mode==1||mode==5){v6.sin6_addr.s6_addr[10]=0xff;v6.sin6_addr.s6_addr[11]=0xff;}
  if(mode==2){v6.sin6_addr.s6_addr[0]=0x20;v6.sin6_addr.s6_addr[1]=1;}
  std::memcpy(v6.sin6_addr.s6_addr+12,&ip,4);length=mode==5?sizeof(sockaddr_in6)-1:sizeof(sockaddr_in6);std::memcpy(&result,&v6,length);
 }
 // lwip_getaddrname truncates to the caller's capacity and reports the copied length.
 if(*size>length)*size=length;
 std::memcpy(s,&result,*size);return 0;
}
static int getsockname(int,sockaddr *s,socklen_t*n){return socketAddress(s,n,fake::local,fake::localMode,fake::localError);}
static int getpeername(int,sockaddr *s,socklen_t*n){return socketAddress(s,n,fake::peer,fake::peerMode,fake::peerError);}
static const char *inet_ntop(int family,const void*ip,char *s,size_t n){CHECK(family==AF_INET);uint32_t value;std::memcpy(&value,ip,4);value=ntohl(value);std::snprintf(s,n,"%u.%u.%u.%u",unsigned(value>>24),unsigned((value>>16)&255),unsigned((value>>8)&255),unsigned(value&255));return s;}
static int64_t esp_timer_get_time(){return fake::now;}
static void esp_fill_random(void *p,size_t n){static uint8_t seq=0;auto out=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)out[i]=++seq;}
static uint32_t esp_random(){return 42;}
#define fsync _commit
static void vTaskDelay(int){++fake::now;}
static int pdMS_TO_TICKS(int n){return n;}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,pdPASS=1;
static int xTaskCreateWithCaps(void(*)(void*),const char*,int,void*,int,void*,int){return fake::taskFail?0:pdPASS;}
static void vTaskDeleteWithCaps(void*){}
struct httpd_req_t {
 const char *uri="/";int method=HTTP_GET;size_t content_len=0,consumed=0;
 std::string body,response,status="200 OK",type;std::map<std::string,std::string>headers{{"Host","192.168.4.1"}},out;
 int recvLimit=613,disconnectAfter=-1,sendFailAfter=-1;bool ended=false;
};
static size_t httpd_req_get_hdr_value_len(httpd_req_t*r,const char*k){return r->headers[k].size();}
static int httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*p,size_t n){auto&s=r->headers[k];if(s.size()>=n)return ESP_FAIL;std::memcpy(p,s.c_str(),s.size()+1);return ESP_OK;}
static int httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){r->out[k]=v;return 0;}
static int httpd_resp_set_status(httpd_req_t*r,const char*v){r->status=v;return 0;}
static int httpd_resp_set_type(httpd_req_t*r,const char*v){r->type=v;return 0;}
static int httpd_resp_send(httpd_req_t*r,const char*p,int n){r->response.append(p,n<0?std::strlen(p):size_t(n));r->ended=true;return 0;}
static int httpd_resp_send_chunk(httpd_req_t*r,const char*p,size_t n){if(r->sendFailAfter>=0&&r->response.size()>=size_t(r->sendFailAfter))return ESP_FAIL;if(!p)r->ended=true;else r->response.append(p,n);return 0;}
static int httpd_req_recv(httpd_req_t*r,char*p,size_t n){if(r->disconnectAfter>=0&&r->consumed>=size_t(r->disconnectAfter))return -1;n=std::min({n,r->body.size()-r->consumed,size_t(r->recvLimit)});if(!n)return -1;std::memcpy(p,r->body.data()+r->consumed,n);r->consumed+=n;return int(n);}
static int httpd_query_key_value(const char*q,const char*k,char*p,size_t n){std::string s=q,key=std::string(k)+"=";size_t pos=0;while(pos<s.size()){const auto end=s.find('&',pos);const auto pair=s.substr(pos,end==std::string::npos?end:end-pos);if(pair.rfind(key,0)==0){auto value=pair.substr(key.size());if(value.size()>=n)return -1;std::memcpy(p,value.c_str(),value.size()+1);return 0;}if(end==std::string::npos)break;pos=end+1;}return -1;}
static int httpd_req_to_sockfd(httpd_req_t*){return 1;}
using httpd_handle_t=void*;struct httpd_config_t{size_t stack_size=0;int task_caps=0,max_open_sockets=0,recv_wait_timeout=0,send_wait_timeout=0;bool lru_purge_enable=false,enable_so_linger=false;int linger_timeout=0,uri_match_fn=0;};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
constexpr int httpd_uri_match_wildcard=1;
struct httpd_uri_t{const char*uri=nullptr;int method=0;esp_err_t(*handler)(httpd_req_t*)=nullptr;};
static int httpd_start(httpd_handle_t*h,httpd_config_t*){*h=reinterpret_cast<void*>(1);return ESP_OK;}
static int httpd_register_uri_handler(httpd_handle_t,httpd_uri_t*){return ESP_OK;}
static int httpd_stop(httpd_handle_t){return ESP_OK;}
const unsigned char lanPageStart[]={0};const unsigned char lanPageEnd[]={0};
#include "lan_web.inc"
static httpd_req_t request(const char*uri,int method=HTTP_GET,std::string body={}){httpd_req_t r;r.uri=uri;r.method=method;r.body=std::move(body);r.content_len=r.body.size();r.headers["X-Yumo-Request"]="1";return r;}
static httpd_req_t run(httpd_req_t r){sys::lanweb::dispatch(&r);CHECK(fake::users==0);return r;}
int main(){
 _set_fmode(_O_BINARY);std::filesystem::create_directory("sdcard");
 using namespace sys::lanweb;wanted=true;address=fake::local;
 // HTTPD selects PF_INET6 with CONFIG_LWIP_IPV6=y; an IPv4 phone still
 // produces AF_INET6 ::ffff:192.168.4.x in both socket address queries.
 CHECK(run(request("/api/files?path=%2F")).status=="200 OK");
 // Native IPv4 and mapped IPv6 endpoints both retain AP/peer/Host/Origin guards.
 for(int localMode:{0,1})for(int peerMode:{0,1}){
  fake::localMode=localMode;fake::peerMode=peerMode;
  CHECK(run(request("/api/files?path=%2F")).status=="200 OK");
  auto req=request("/api/files?path=%2F");req.headers["Host"]="192.168.4.1:80";CHECK(run(req).status=="200 OK");
  req.headers["Host"]="192.168.4.1:81";CHECK(run(req).status=="403 Forbidden");
  req=request("/api/files?path=%2F");req.headers["Origin"]="http://192.168.4.1";CHECK(run(req).status=="200 OK");
  req.headers["Origin"]="http://evil.test";CHECK(run(req).status=="403 Forbidden");
  fake::local=htonl(0xc0a80201);CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::local=address;
  fake::peer=htonl(0xc0a80302);CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::peer=htonl(0xc0a80402);
 }
 fake::localMode=fake::peerMode=1;
 for(int badMode:{2,3,4,5,6}){
  fake::localMode=badMode;CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::localMode=1;
  fake::peerMode=badMode;CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::peerMode=1;
 }
 fake::localError=true;CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::localError=false;
 fake::peerError=true;CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");fake::peerError=false;
 // AP address changes are compared and rendered in network byte order, without a hardcoded host.
 for(int mode:{0,1}){
  fake::localMode=fake::peerMode=mode;address=fake::local=htonl(0x0a2a0001);fake::peer=htonl(0x0a2a0002);
  auto req=request("/api/files?path=%2F");req.headers["Host"]="10.42.0.1";CHECK(run(req).status=="200 OK");
  req.headers["Host"]="192.168.4.1";CHECK(run(req).status=="403 Forbidden");
 }
 fake::localMode=fake::peerMode=1;address=fake::local=htonl(0xc0a80401);fake::peer=htonl(0xc0a80402);
 std::string value;
 CHECK(decode("%E4%B8%AD%20a%2Bb",value)&&value=="中 a+b");
 for(auto bad:{"%","%0","%GG","%00","%0A"})CHECK(!decode(bad,value));
 for(auto bad:{"/../escape","/a/../../escape","/a\\b","//bad","/a/","/a:bad","/a. ","/"})CHECK(!path(bad,value));
 CHECK(path("/",value,true));CHECK(path("/nested/ok.txt",value));
 auto r=run(request("/api/passwords"));CHECK(r.status=="401 Unauthorized"&&r.response.find("Secret123")==std::string::npos);
 r=request("/api/files?path=%2F");r.headers["Host"]="evil.test";CHECK(run(r).status=="403 Forbidden");
 r=request("/api/files?path=%2F");r.headers["Origin"]="http://evil.test";CHECK(run(r).status=="403 Forbidden");
 r=request("/api/files?path=%2F");fake::local=htonl(0xc0a80202);CHECK(run(r).status=="403 Forbidden");fake::local=address;
 fake::peer=htonl(0xc0a80302);CHECK(run(r).status=="403 Forbidden");fake::peer=htonl(0xc0a80402);
 r=request("/api/files?action=mkdir&path=%2Fblocked",HTTP_POST);r.headers.erase("X-Yumo-Request");CHECK(run(r).status=="403 Forbidden");CHECK(!std::filesystem::exists("sdcard/blocked"));
 CHECK(run(request("/api/files?path=%2F..%2Fescape")).status=="400 Bad Request");
 CHECK(run(request("/api/files?action=mkdir&path=%2Ffolder",HTTP_POST)).status=="200 OK");
 std::string bytes;for(int i=0;i<10000;++i)bytes+=char(i%256);
 r=run(request("/api/upload?path=%2Ffolder%2Fsample.bin",HTTP_PUT,bytes));CHECK(r.status=="200 OK");CHECK(r.response.find("10000")!=std::string::npos);
 r=run(request("/api/download?path=%2Ffolder%2Fsample.bin"));CHECK(r.response==bytes&&r.ended&&r.out["X-File-Size"]=="10000");CHECK(r.out["Cache-Control"].find("no-store")!=std::string::npos);
 r=run(request("/api/upload?path=%2Ffolder%2Fsample.bin",HTTP_PUT,"replace"));CHECK(r.status=="409 Conflict");CHECK(run(request("/api/download?path=%2Ffolder%2Fsample.bin")).response==bytes);
 r=request("/api/upload?path=%2Ffolder%2Fpartial.bin",HTTP_PUT,bytes);r.disconnectAfter=2000;CHECK(run(r).status!="200 OK");CHECK(!std::filesystem::exists("sdcard/folder/partial.bin"));CHECK(!std::filesystem::exists("sdcard/folder/.yumo-upload-0000002a.tmp"));
 r=request("/api/upload?path=%2Ffolder%2Fhuge.bin",HTTP_PUT);r.content_len=MaxUpload+1;CHECK(run(r).status=="413 Payload Too Large");
 CHECK(run(request("/api/upload?path=%2Ffolder%2Fempty.bin",HTTP_PUT)).status=="200 OK");
 r=request("/api/download?path=%2Ffolder%2Fsample.bin");r.sendFailAfter=4096;r=run(r);CHECK(!r.ended);
 CHECK(run(request("/api/files?action=rename&path=%2Ffolder%2Fsample.bin&target=%2Ffolder%2Frenamed.bin",HTTP_POST)).status=="200 OK");
 CHECK(run(request("/api/files?path=%2Ffolder")).response.find("renamed.bin")!=std::string::npos);
 CHECK(run(request("/api/files?path=%2Ffolder%2Frenamed.bin",HTTP_DELETE)).status=="200 OK");CHECK(!std::filesystem::exists("sdcard/folder/renamed.bin"));
 fake::available=false;CHECK(run(request("/api/files?path=%2F")).status=="503 Service Unavailable");fake::available=true;
 CHECK(run(request("/api/login",HTTP_POST,"bad")).status=="400 Bad Request");CHECK(fake::unlocks==0);
 CHECK(run(request("/api/login",HTTP_POST,"9999")).status=="401 Unauthorized");
 r=run(request("/api/login",HTTP_POST,"1234"));CHECK(r.status=="200 OK");auto cookie=r.out["Set-Cookie"].substr(0,77);CHECK(cookie.size()==77);CHECK(r.out["Set-Cookie"].find("HttpOnly; SameSite=Strict")!=std::string::npos);
 r=request("/api/passwords");r.headers["Cookie"]=cookie;r=run(r);CHECK(r.status=="200 OK");CHECK(r.response.find("Secret123!")!=std::string::npos);CHECK(r.response.find("legacy-account")==std::string::npos);
 r=request("/api/passwords");r.headers["Cookie"]="prefix"+cookie;CHECK(run(r).status=="401 Unauthorized");
 CHECK(run(request("/api/logout",HTTP_POST)).status=="200 OK");
 r=request("/api/passwords");r.headers["Cookie"]=cookie;CHECK(run(r).status=="200 OK"); // Anonymous visitor cannot lock another browser.
 auto second=run(request("/api/login",HTTP_POST,"1234")).out["Set-Cookie"].substr(0,77);
 r=request("/api/passwords");r.headers["Cookie"]=cookie;CHECK(run(r).status=="200 OK");
 r=request("/api/logout",HTTP_POST);r.headers["Cookie"]=second;CHECK(run(r).status=="200 OK");
 r=request("/api/passwords");r.headers["Cookie"]=second;CHECK(run(r).status=="401 Unauthorized");
 r.headers["Cookie"]=cookie;CHECK(run(r).status=="200 OK");
 fake::now+=SessionUs+1;CHECK(run(r).status=="401 Unauthorized");for(const auto&s:sessions)CHECK(!s.unlocked&&s.token.empty());
 CHECK(run(request("/api/login",HTTP_POST,"1234")).status=="200 OK");lockVault();for(const auto&s:sessions)CHECK(!s.unlocked&&s.token.empty());
 stop();CHECK(run(request("/api/files?path=%2F")).status=="403 Forbidden");
 std::printf("%u dual-stack/AP/Host/Origin/path/transfer/auth assertions passed (bundled lwIP layouts, synthetic sockets)\n",checks);
}
