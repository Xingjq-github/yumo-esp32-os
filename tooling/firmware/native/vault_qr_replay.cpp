#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include "qrcodegen.h"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace vault {
using Bytes=std::vector<uint8_t>;
void wipe(void *p,size_t n){std::memset(p,0,n);}void clear(std::string&s){wipe(s.data(),s.size());s.clear();}
struct SecretBytes{Bytes value;~SecretBytes(){if(!value.empty())wipe(value.data(),value.size());}};
struct Entry{std::string title,username,password,note,category,target;};
}
#include "vault_text.inc"
enum lv_obj_flag_t{LV_OBJ_FLAG_CLICKABLE=1,LV_OBJ_FLAG_SCROLLABLE=2};
constexpr int LV_COLOR_FORMAT_I1=1,LV_STRIDE_AUTO=0,LV_OPA_COVER=255,LV_EVENT_DELETE=1;
struct lv_draw_buf_t{uint8_t*data=nullptr;size_t data_size=0;struct{int stride=0;}header;};
struct lv_event_t{void*user_data=nullptr;};
struct lv_obj_t{lv_draw_buf_t*buffer=nullptr;void(*callback)(lv_event_t*)=nullptr;void*context=nullptr;};
lv_obj_t canvas;bool failAllocation=false;unsigned wiped=0;
lv_draw_buf_t*lv_draw_buf_create(int w,int h,int,int){if(failAllocation)return nullptr;auto b=new lv_draw_buf_t;b->header.stride=((w+7)/8+3)&~3;b->data_size=8+h*b->header.stride;b->data=new uint8_t[b->data_size];return b;}
void lv_draw_buf_destroy(lv_draw_buf_t*b){for(size_t i=0;i<b->data_size;++i)CHECK(b->data[i]==0);++wiped;delete[]b->data;delete b;}
lv_obj_t*lv_canvas_create(lv_obj_t*){canvas={};return &canvas;}
void lv_canvas_set_draw_buf(lv_obj_t*o,lv_draw_buf_t*b){o->buffer=b;}
void lv_canvas_set_palette(lv_obj_t*,int,int){}
int lv_color_white(){return 0xffffff;}int lv_color_black(){return 0;}int lv_color_to_32(int c,int){return c;}
void lv_obj_add_event_cb(lv_obj_t*o,void(*f)(lv_event_t*),int,void*c){o->callback=f;o->context=c;}
void*lv_event_get_user_data(lv_event_t*e){return e->user_data;}
void lv_image_cache_drop(void*){}
void lv_obj_set_clickable(lv_obj_t*,bool){}
void lv_obj_set_scrollable(lv_obj_t*,bool){}
#include "vault_qr.inc"
void check(vault::Entry entry,const char*name){
 auto text=vault::plainText(entry);CHECK(ui::vaultQr(nullptr,text));auto b=canvas.buffer;
 std::ofstream expected(std::string(name)+".txt",std::ios::binary);expected.write(text.data(),text.size());expected.close();
 std::ofstream image(std::string(name)+".pgm",std::ios::binary);image<<"P5\n240 240\n255\n";
 for(int y=0;y<240;++y)for(int x=0;x<240;++x){bool black=(b->data[8+y*b->header.stride+x/8]>>(7-x%8))&1;image.put(black?0:255);if(x<8||y<8||x>=232||y>=232)CHECK(!black);}
 image.close();lv_event_t event{canvas.context};canvas.callback(&event);vault::clear(text);
}
int main(){
 check({"邮箱（中文）","user@example.com","P@ss:+&?#123","换行备注\n第二行","web","用途 / 登录"},"typical");
 check({"门禁","legacy-ignored","123456","仅测试","other","家门"},"code");
 check({std::string(96,'T'),std::string(128,'U'),std::string(128,'P'),std::string(256,'N'),"work",std::string(192,'S')},"maximum");
 std::string chinese;for(int i=0;i<85;++i)chinese+="字";
 check({"长中文备注",std::string(128,'u'),std::string(128,'p'),chinese,"app",std::string(192,'s')},"unicode");
 failAllocation=true;CHECK(!ui::vaultQr(nullptr,"test"));CHECK(wiped==4);
 std::printf("%u QR pixel/bounds/wipe assertions passed\n",checks);
}
