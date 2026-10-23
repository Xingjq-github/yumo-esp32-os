#include "core/app_order_model.hpp"
using namespace sys::app_order;
constexpr Order registry{{key("clock"),key("vault"),key("fc"),Settings},4};
constexpr bool ordering(){
    auto order=normalize({},registry);
    if(!(order==registry)||!move(order,0,2)||order.ids[2]!=key("clock"))return false;
    if(move(order,3,0)||move(order,0,3)||move(order,8,0)||move(order,0,0))return false;
    if(!(decode(encode(order))==order))return false;
    Order next{{key("new"),key("fc"),key("clock"),Settings},4};
    const auto restored=normalize(order,next);
    return restored.count==4&&restored.ids[0]==key("fc")&&restored.ids[1]==key("clock")&&restored.ids[2]==key("new")&&restored.ids[3]==Settings;
}
static_assert(ordering());
constexpr bool corrupt(){
    const auto good=encode(registry);
    for(size_t i=0;i<good.size();++i){auto damaged=good;damaged[i]^=0x80;if(decode(damaged).count)return false;}
    Order duplicate{{key("clock"),key("clock")},2};
    if(valid(duplicate)||decode(encode(duplicate)).count)return false;
    return normalize(duplicate,registry)==registry;
}
static_assert(corrupt());
