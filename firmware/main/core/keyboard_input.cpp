#include "keyboard_input.hpp"
#include "power_mgr.hpp"
#include "freertos/FreeRTOS.h"
#include <array>
#include <new>
#include "esp_heap_caps.h"
namespace sys {
namespace {
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;hidinput::Keys state;uint32_t generation=0;
using InputQueue=std::array<hidinput::Keys,32>;
InputQueue *queue=nullptr;unsigned head=0,count=0;
}
bool KeyboardInput::init() {
    // Called only by the serialized worker before host callbacks can run.
    if(queue) return true;
    void *memory=heap_caps_malloc(sizeof(InputQueue),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory) return false;
    auto *created=new(memory) InputQueue{};
    // GUI pop/reset can already be running. Publish under their same lock.
    portENTER_CRITICAL(&lock);queue=created;head=count=0;portEXIT_CRITICAL(&lock);
    return true;
}
void KeyboardInput::publish(const hidinput::Keys &keys) {
    bool changed=false;
    portENTER_CRITICAL(&lock);
    if(queue && state.bits!=keys.bits) {
        state=keys;++generation;changed=true;
        // Overflow releases every key before resynchronizing; never stick a key.
        if(count>=queue->size()-1) { head=0;count=1;(*queue)[0]={}; }
        (*queue)[(head+count)%queue->size()]=keys;++count;
    }
    portEXIT_CRITICAL(&lock);
    if(changed)Power::activity(); // wake the idle GUI on an actual HID report
}
void KeyboardInput::reset() {
    portENTER_CRITICAL(&lock);state={};++generation;head=0;count=queue?1:0;if(queue) (*queue)[0]={};portEXIT_CRITICAL(&lock);
}
bool KeyboardInput::pop(hidinput::Keys &keys) {
    portENTER_CRITICAL(&lock);bool have=count!=0;
    if(have) { keys=(*queue)[head];head=(head+1)%queue->size();--count; }
    portEXIT_CRITICAL(&lock);return have;
}
hidinput::Keys KeyboardInput::snapshot(uint32_t &revision) {
    portENTER_CRITICAL(&lock);auto out=state;revision=generation;portEXIT_CRITICAL(&lock);return out;
}
}
