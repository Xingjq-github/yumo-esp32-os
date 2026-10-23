#include "pattern_format.hpp"
#include "cJSON.h"
#include <cmath>
#include <memory>
namespace core::patterns {
namespace {
bool text(cJSON *obj,const char *key,std::string &out,size_t limit){auto v=cJSON_GetObjectItemCaseSensitive(obj,key);if(!cJSON_IsString(v)||!v->valuestring)return false;size_t n=strnlen(v->valuestring,limit+1);if(n>limit||!n)return false;out.assign(v->valuestring,n);return true;}
template<size_t N>bool strings(cJSON *obj,const char *key,std::array<std::string,N> &out){auto a=cJSON_GetObjectItemCaseSensitive(obj,key);if(!cJSON_IsArray(a)||cJSON_GetArraySize(a)!=int(N))return false;for(size_t i=0;i<N;++i){auto v=cJSON_GetArrayItem(a,int(i));if(!cJSON_IsString(v)||!v->valuestring||strnlen(v->valuestring,1025)>1024)return false;out[i]=v->valuestring;}return true;}
}
bool parseLesson(const char *data,size_t bytes,unsigned index,Lesson &l){
    if(index>=LessonCount||!bytes||bytes>MaxLessonBytes)return false;
    const char *end=nullptr;std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_ParseWithLengthOpts(data,bytes,&end,false),cJSON_Delete);
    if(!root||!cJSON_IsObject(root.get())||end!=data+bytes)return false;
    auto o=root.get();
    if(!text(o,"id",l.id,64)||l.id!=Catalogs[index].id||!text(o,"title",l.title,64)||l.title!=Catalogs[index].title)return false;
    for(auto p:{std::make_pair("intent",&l.intent),{"wire",&l.wire},{"boundary",&l.boundary},{"question",&l.question},{"why",&l.why},{"caution",&l.caution}})if(!text(o,p.first,*p.second,1536))return false;
    if(!text(o,"code",l.code,12288)||!strings(o,"roles",l.roles)||!strings(o,"options",l.options)||!strings(o,"answers",l.answers)||!strings(o,"tasks",l.tasks))return false;
    auto targets=cJSON_GetObjectItemCaseSensitive(o,"targets"),runs=cJSON_GetObjectItemCaseSensitive(o,"runs");
    if(!cJSON_IsArray(targets)||cJSON_GetArraySize(targets)!=2||!cJSON_IsArray(runs)||cJSON_GetArraySize(runs)!=6)return false;
    for(unsigned i=0;i<2;++i){auto n=cJSON_GetArrayItem(targets,i);if(!cJSON_IsNumber(n)||n->valuedouble<0||n->valuedouble>=3||std::floor(n->valuedouble)!=n->valuedouble)return false;l.targets[i]=unsigned(n->valueint);}
    for(unsigned i=0;i<6;++i){auto r=cJSON_GetArrayItem(runs,i);if(!text(r,"output",l.runs[i].output,96))return false;auto t=cJSON_GetObjectItemCaseSensitive(r,"trace");int n=cJSON_GetArraySize(t);if(!cJSON_IsArray(t)||n<1||n>12)return false;
        l.runs[i].trace.clear();for(int j=0;j<n;++j){auto v=cJSON_GetArrayItem(t,j);if(!cJSON_IsString(v)||!v->valuestring||strnlen(v->valuestring,161)>160)return false;l.runs[i].trace.emplace_back(v->valuestring);}}
    // Different processes may legitimately finish with the same value (e.g.
    // select A versus B then A). The process, not unique output text, matters.
    for(unsigned i=0;i<3;++i)if(l.runs[i].output!=l.runs[i+3].output)return false;
    return true;
}
}
