#include "apps/random_model.hpp"
namespace model=apps::random_model;
constexpr bool gridsFit()
{
    for(unsigned count=2;count<=model::MaxChoices;++count) {
        for(int height:{model::BodyHeight,model::BodyHeight-model::ParameterHeight-model::Gap}) {
            for(unsigned i=0;i<count;++i) {
                const auto a=model::cell(count,i,height);
                if(a.x<0 || a.y<0 || a.width<59 || a.height<30 ||
                   a.x+a.width>model::Width || a.y+a.height>height) return false;
                for(unsigned j=i+1;j<count;++j) {
                    const auto b=model::cell(count,j,height);
                    if(a.x<b.x+b.width && b.x<a.x+a.width &&
                       a.y<b.y+b.height && b.y<a.y+a.height) return false;
                }
            }
            const auto last=model::cell(count,count-1,height);
            if(last.x+last.width!=model::Width || last.y+last.height!=height) return false;
        }
    }
    return true;
}
constexpr bool diceFaces()
{
    for(unsigned i=0;i<model::DicePips.size();++i) {
        unsigned count=0;
        for(unsigned p=0;p<9;++p) count+=(model::DicePips[i]>>p)&1u;
        if(count!=i+1 || model::DicePips[i]>=512) return false;
    }
    return true;
}
static_assert(gridsFit(),"Every random-choice grid must fit the 240px viewport");
static_assert(diceFaces(),"Dice masks must contain one to six pips");
static_assert(model::cell(0,0,162).width==0);
static_assert(model::cell(17,0,162).width==0);
static_assert(model::cell(16,16,125).width==0);
