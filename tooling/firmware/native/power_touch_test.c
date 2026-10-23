// Production adapter/wrappers + upstream read callback, fake LVGL/RTOS/GPIO.
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stddef.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef int esp_err_t;typedef int BaseType_t;typedef unsigned TickType_t;
typedef void* esp_timer_handle_t;typedef void* SemaphoreHandle_t;
typedef int lv_coord_t;typedef int lv_indev_state_t;
typedef struct {int x,y;} lv_point_t;
typedef struct {lv_point_t point;int state;} lv_indev_data_t;
typedef struct {int unused;} lv_timer_t;
typedef struct {int unused;} lv_display_t;
typedef struct {void *driver_data;} lv_indev_t;
typedef void (*lv_indev_read_cb_t)(lv_indev_t*,lv_indev_data_t*);
typedef void* esp_lcd_touch_handle_t;
typedef struct {uint16_t x,y,strength;uint8_t track_id;} esp_lcd_touch_point_data_t;
enum {ESP_ERR_INVALID_STATE=259,ESP_ERR_TIMEOUT=263,ESP_OK=0,LV_DEF_REFR_PERIOD=16,pdTRUE=1,pdFALSE=0,LV_INDEV_STATE_RELEASED=0,LV_INDEV_STATE_PRESSED=1};
#define MAX_TOUCH_POINTS 1
#define portMAX_DELAY 0xffffffffu
#define BSP_LCD_TOUCH_INT 48
#define pdMS_TO_TICKS(x) (x)
#define portYIELD_FROM_ISR() ((void)0)
static unsigned activities,notifications,handlers,at_handler,reads;
static bool sem,packet_pressed,irq_enabled=true,line_low=false,pause_sem,pause_timeout;
static void *current_task=(void*)1;
static int64_t now;
static unsigned pause_wait_notifications;static int read_error,get_error;
static lv_indev_t input;static lv_timer_t input_timer,refresh_timer,pump;static lv_display_t display;
static int64_t esp_timer_get_time(void){return now;}
static void lvgl_touch_read(lv_indev_t*,lv_indev_data_t*);
static BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t t){
 if(s==(void*)2){
  if(!t){bool v=pause_sem;pause_sem=false;return v;}
  CHECK(notifications>pause_wait_notifications); // sleeping GUI notified before wait
  if(pause_timeout)return pdFALSE;
  pause_sem=false;return pdTRUE;
 }
 bool v=sem;sem=false;return v;
}
static esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t h){(void)h;++reads;return read_error;}
static esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t h,esp_lcd_touch_point_data_t*p,uint8_t*n,uint8_t m){
    (void)h;(void)m;*n=packet_pressed?1:0;p->x=100;p->y=120;return get_error;
}
static void* lv_indev_get_driver_data(lv_indev_t *i){return i->driver_data;}
static void* xTaskGetCurrentTaskHandle(void){return current_task;}
static void vTaskDelay(TickType_t ticks){(void)ticks;}
static void ulTaskNotifyTake(int a,TickType_t ticks){(void)a;(void)ticks;}
static void xTaskNotifyGive(void*t){(void)t;++notifications;}
static void vTaskNotifyGiveFromISR(void*t,BaseType_t*w){(void)t;(void)w;++notifications;}
static void lv_tick_set_cb(uint32_t(*cb)(void)){(void)cb;}
lv_indev_t *bsp_display_get_input_dev(void){return &input;}
static lv_display_t *lv_display_get_default(void){return &display;}
static lv_timer_t *lv_display_get_refr_timer(lv_display_t*d){(void)d;return &refresh_timer;}
static lv_timer_t *lv_indev_get_read_timer(lv_indev_t*i){(void)i;return &input_timer;}
static lv_indev_read_cb_t lv_indev_get_read_cb(lv_indev_t*i){(void)i;return lvgl_touch_read;}
static void lv_timer_set_period(lv_timer_t*t,uint32_t p){(void)t;(void)p;}
static void lv_timer_ready(lv_timer_t*t){(void)t;}
static void lv_refr_now(void*d){(void)d;}
static uint32_t lv_timer_handler(void){++handlers;at_handler=activities;return 10;}
void bsp_power_touch_sample(bool pressed);
bool bsp_power_take_touch_wakeup(void);
void bsp_power_touch_gate(bool active);
void bsp_power_touch_reset(void);
void bsp_power_touch_rearm(void);
bool bsp_power_touch_pending(void);
static void gpio_intr_disable(int pin){CHECK(pin==48);irq_enabled=false;}
static void gpio_intr_enable(int pin){CHECK(pin==48);irq_enabled=true;}
static int gpio_get_level(int pin){CHECK(pin==48);return !line_low;}
typedef void (*esp_lcd_touch_interrupt_callback_t)(esp_lcd_touch_handle_t);

#include "adapter.inc"
#include "touch.inc"
#include "touch_irq.inc"
static void activity(void){++activities;bsp_power_wake();}
static void packet(bool pressed,int error) {
    now+=100000;sem=true;packet_pressed=pressed;read_error=error;bsp_power_touch_irq();power_handler();
}
int main(void) {
    esp_lv_adapter_touch_ctx_t ctx={0};ctx.handle=(void*)1;ctx.with_irq=true;
    ctx.scale.x=ctx.scale.y=1;input.driver_data=&ctx;
    bsp_power_set_pump(&pump,activity);
    // Spurious IRQs and general GPIO light-sleep wakes cannot renew idle time.
    for(int i=0;i<10000;++i){packet(false,0);CHECK(activities==0);}
    for(int i=0;i<100;++i){bsp_power_touch_wakeup();power_handler();CHECK(activities==0);}
    packet(true,0);CHECK(activities==1);CHECK(at_handler==1); // before timeout processing
    CHECK(ctx.last_state==LV_INDEV_STATE_PRESSED);
    power_handler();CHECK(activities==1); // cached press without new packet is not activity
    packet(true,0);CHECK(activities==2); // motion or sustained touch packet
    packet(false,0);CHECK(activities==3);
    packet(false,0);CHECK(activities==3); // duplicate release
    packet(true,1);CHECK(activities==3);CHECK(ctx.last_state==LV_INDEV_STATE_RELEASED);
    packet(true,0);CHECK(activities==4);
    get_error=1;packet(true,0);CHECK(activities==4);get_error=0;
    packet(false,0);CHECK(activities==5);
    bsp_power_touch_active(false);unsigned before=notifications,old_reads=reads;
    for(int i=0;i<1000;++i){sem=true;packet_pressed=true;bsp_power_touch_irq();bsp_power_touch_wakeup();power_handler();}
    CHECK(activities==5);CHECK(notifications==before);CHECK(reads==old_reads);
    bsp_power_touch_active(true);packet_pressed=false;power_handler();CHECK(activities==5);
    packet(true,0);CHECK(activities==6);CHECK(ctx.last_state==LV_INDEV_STATE_PRESSED);
    packet(false,0);CHECK(activities==7);
    CHECK(handlers>11000);
    // Lost release IRQ is recovered by a bounded held-touch poll.
    packet(true,0);packet_pressed=false;now+=20000;
    lv_indev_data_t data={0};lvgl_touch_read(&input,&data);
    CHECK(data.state==LV_INDEV_STATE_RELEASED);
    // A level IRQ is masked once; a stuck-low line is drained without storms.
    line_low=true;power_touch_irq(ctx.handle);CHECK(!irq_enabled);
    read_error=1;sem=true;lvgl_touch_read(&input,&data);unsigned count=reads;
    for(unsigned i=0;i<99;++i){now+=1000;lvgl_touch_read(&input,&data);}
    CHECK(reads==count);CHECK(!irq_enabled);
    now+=1000;lvgl_touch_read(&input,&data);CHECK(reads==count+1);
    read_error=0;line_low=false;now+=100000;lvgl_touch_read(&input,&data);
    CHECK(irq_enabled && data.state==LV_INDEV_STATE_RELEASED);
    packet(true,0);bsp_power_touch_active(false);CHECK(!irq_enabled);
    CHECK(ctx.last_state==LV_INDEV_STATE_RELEASED && !touch_held);
    bsp_power_touch_active(true);packet_pressed=false;power_handler();CHECK(irq_enabled);
    // Clear stale ack BEFORE publishing pause. Timeout rollback wakes late ack.
    current_task=(void*)3;pause_sem=true;pause_wait_notifications=notifications;
    CHECK(bsp_power_pause(1000)==ESP_OK);CHECK(s_ctx.paused);
    s_ctx.paused=false;s_ctx.pause_ack=false;pause_timeout=true;
    pause_wait_notifications=notifications;unsigned notified=notifications;
    CHECK(bsp_power_pause(1000)==ESP_ERR_TIMEOUT);
    CHECK(!s_ctx.paused && notifications==notified+2);
    pause_timeout=false;pause_wait_notifications=notifications;
    CHECK(bsp_power_pause(1000)==ESP_OK);
    s_ctx.paused=false;s_ctx.inited=false;CHECK(bsp_power_pause(1000)==ESP_ERR_INVALID_STATE);

    printf("%u touch/wake checks passed\n",checks);
}
