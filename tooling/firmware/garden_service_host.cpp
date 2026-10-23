#include "core/garden_service.hpp"
#include "core/power_mgr.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
namespace g=core::garden;
static void (*task)(void *);static void *context;
static bool taskFail=false,sleeping=false,mutate=true,ensureOk=true;
static int sessions=0,leases=0,ioCalls=0,ensures=0,deletes=0,report=0;
static unsigned epoch=10;static uint64_t generation=7;
int xTaskCreateWithCaps(void (*f)(void *),const char *,unsigned stack,void *arg,unsigned,void *,unsigned){
    assert(stack>=4096);if(taskFail)return 0;assert(!task);task=f;context=arg;return 1;
}
void vTaskDeleteWithCaps(void *){++deletes;}
bool sys::Power::sleepPending(){return sleeping;}
sys::Power::WorkGuard::WorkGuard(){++leases;}
sys::Power::WorkGuard::~WorkGuard(){--leases;}
namespace core::sd {
Access requestAccess(bool resume){return {epoch,resume};}
Session::Session(Access a,bool recover,bool drain){assert(recover);if(a.epoch!=epoch){error_=ESTALE;return;}held_=true;generation_=::generation;++sessions;(void)drain;}
Session::~Session(){if(held_)--sessions;}
bool Session::claimMutation(){assert(held_);return mutate;}
void Session::reportError(int e){report=e;}
bool ensure(const char *path){assert(sessions && leases);assert(!std::strcmp(path,Garden));++ensures;if(!ensureOk)errno=EIO;return ensureOk;}
}
namespace core::garden::store {
int load(const std::string &path,Snapshot &out){assert(sessions==1 && leases==1 && path==sd::Garden);++ioCalls;out={create(1),5,true,false};return 0;}
int save(const std::string &path,const State &state,uint64_t expected,Snapshot &out){assert(sessions==1 && leases==1 && path==sd::Garden);++ioCalls;out={state,expected+1,true,false};return 0;}
}
static void run(){assert(task && !g::idle());auto f=task;auto c=context;task=nullptr;context=nullptr;f(c);assert(g::idle() && sessions==0 && leases==0);}
int main(){
    auto j=g::open();assert(j && !g::open() && !g::idle());run();assert(j->done && !j->error && j->generation==7 && j->result.revision==5 && !ensures);
    auto access=j->access;auto state=j->result.state;
    const auto calls=ioCalls;j=g::open();j->cancelled=true;run();assert(j->error==ECANCELED && ioCalls==calls);
    sleeping=true;j=g::open();run();assert(j->error==ECANCELED && ioCalls==calls);
    // Saving is permitted to finish during sleep/exit drain and owns a copied state only.
    j=g::save(state,5,7,access);auto weak=std::weak_ptr<g::Job>(j);j.reset();assert(!weak.expired());run();assert(weak.expired() && ioCalls==calls+1 && ensures==1);
    sleeping=false;j=g::save(state,6,6,access);run();assert(j->error==ESTALE);
    j=g::open();++epoch;run();assert(j->error==ESTALE);access=core::sd::requestAccess(true);
    mutate=false;j=g::save(state,6,7,access);run();assert(j->error==EBUSY);mutate=true;
    ensureOk=false;j=g::save(state,6,7,access);run();assert(j->error==EIO && report==EIO);ensureOk=true;
    taskFail=true;assert(!g::open() && g::idle());assert(!g::save(state,6,7,access) && g::idle());taskFail=false;
    assert(!g::save(state,0,0,access));state.miniLength=6;state.miniPosition=6;assert(!g::save(state,6,7,access));
    assert(!sessions && !leases && deletes==8);
    std::cout<<"PASS: production asynchronous service, exclusive worker, task-create failure, cancelled/read sleep fence, copied save survives exit and sleep drain, card epoch/generation fences, mutation claim and ensure failure, all SD/work leases released before publication.\n";
}
