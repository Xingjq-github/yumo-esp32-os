#include "garden_store.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif
namespace core::garden::store {
namespace {
struct Bank {State state;uint64_t revision=0;bool exists=false,valid=false;};
int error(){return errno?errno:EIO;}
std::string path(const std::string &directory,unsigned bank){return directory+(bank?"/garden-b.sav":"/garden-a.sav");}
int inspect(const std::string &name,Bank &out){
    FILE *file=std::fopen(name.c_str(),"rb");if(!file)return errno==ENOENT?0:error();
    out.exists=true;Record bytes{};const auto got=std::fread(bytes.data(),1,bytes.size(),file);
    const bool failed=std::ferror(file);const int trailing=std::fgetc(file);
    const bool readError=failed || std::ferror(file);const int closed=std::fclose(file);
    if(readError || closed)return EIO;
    // Never replace a bank produced by a newer schema with this older firmware.
    if(got>=10 && !std::memcmp(bytes.data(),"YMGARD",6) &&
       (std::memcmp(bytes.data(),"YMGARD01",8) || bytes[8]!=1 || bytes[9]!=0))return ENOTSUP;
    out.valid=got==bytes.size() && trailing==EOF && decode(bytes,out.state,out.revision);return 0;
}
int select(const std::string &directory,Bank (&banks)[2],int &best){
    for(unsigned i=0;i<2;++i){const int e=inspect(path(directory,i),banks[i]);if(e)return e;}
    if(banks[0].valid && banks[1].valid && banks[0].revision==banks[1].revision &&
       encode(banks[0].state,banks[0].revision)!=encode(banks[1].state,banks[1].revision))return EBADMSG;
    best=banks[1].valid && (!banks[0].valid || banks[1].revision>banks[0].revision)?1:banks[0].valid?0:-1;
    if(best<0 && (banks[0].exists || banks[1].exists))return EBADMSG;
    return 0;
}
}
int load(const std::string &directory,Snapshot &out){
    Bank banks[2];int best=-1;const int e=select(directory,banks,best);if(e)return e;
    Snapshot found;
    if(best>=0){found.exists=true;found.state=banks[best].state;found.revision=banks[best].revision;found.recovered=banks[best^1].exists && !banks[best^1].valid;}
    out=found;return 0;
}
int save(const std::string &directory,const State &state,uint64_t expected,Snapshot &out){
    if(!valid(state))return EINVAL;
    Bank banks[2];int best=-1;int e=select(directory,banks,best);if(e)return e;
    const auto revision=best<0?0:banks[best].revision;
    if(revision!=expected){
        // A prior write may have reached the card before its acknowledgement failed.
        if(expected!=UINT64_MAX && revision==expected+1 && encode(state,revision)==encode(banks[best].state,revision)){
            out={banks[best].state,revision,true,false};return 0;
        }
        return ESTALE;
    }
    if(revision==UINT64_MAX)return EOVERFLOW;
    // Once a valid bank exists, only its older peer is replaced. A torn FIRST save has no backup.
    const unsigned target=best<0?0:unsigned(best^1);const auto bytes=encode(state,revision+1);
    FILE *file=std::fopen(path(directory,target).c_str(),"wb");if(!file)return error();
    bool ok=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size() && std::fflush(file)==0;
#ifdef _WIN32
    if(ok)ok=_commit(_fileno(file))==0;
#else
    if(ok)ok=fsync(fileno(file))==0;
#endif
    if(std::fclose(file)!=0)ok=false;
    if(!ok)return EIO;
    Bank verify;e=inspect(path(directory,target),verify);if(e)return e;
    if(!verify.valid || verify.revision!=revision+1 || encode(verify.state,verify.revision)!=bytes)return EIO;
    out={verify.state,verify.revision,true,false};return 0;
}
}
