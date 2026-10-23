#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <climits>
namespace core::usb_disk {
// The USB task owns the lock and one internal DMA sector. Backends return 0
// only after a sector operation completes; no deferred write acknowledgements.
template<class Read,class Write>
int32_t transfer(uint64_t sectors,uint32_t lba,uint32_t offset,void *buffer,
                 uint32_t size,bool writing,uint8_t (&sector)[512],int &error,
                 Read read,Write write) {
    error=0;
    const uint64_t begin=uint64_t(lba)*512+offset;
    if(size>INT32_MAX||sectors>UINT64_MAX/512||begin>sectors*512||size>sectors*512-begin||(!buffer&&size))
        return -1;
    auto data=static_cast<uint8_t *>(buffer);
    for(uint32_t done=0;done<size;) {
        const uint64_t address=begin+done;
        const size_t at=address%512,n=std::min<size_t>(512-at,size-done);
        if(!writing||at||n!=512)error=read(address/512,sector);
        if(error)return -1;
        if(writing){std::memcpy(sector+at,data+done,n);error=write(address/512,sector);}
        else std::memcpy(data+done,sector+at,n);
        if(error)return -1;
        done+=n;
    }
    return int32_t(size);
}
}
