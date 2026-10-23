// Production slot loader, synthetic flash and independent Python SHA oracle.
#include "core/vault_storage.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NO_MEM=1, ESP_ERR_INVALID_STATE=2;
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, ESP_PARTITION_TYPE_DATA=1;
using esp_partition_subtype_t = int;
struct esp_partition_t { size_t address, size; };
using Oracle = int (*)(const void *, size_t, unsigned char *);
struct ProbeResult {
    int ok, slot, blank;
    unsigned generation, bundle, config;
    int reads, writes, erases, hashes, allocations, outstanding;
    char error[256], log[1024];
};
namespace {
const unsigned char *flash;
size_t flashSize;
int failRead, cancelRead;
bool failAllocation;
Oracle oracle;
ProbeResult *result;
std::atomic<bool> cancelled;
esp_partition_t partition{0xec0000, 0x100000};
void logFailure(const char *, const char *format, ...) {
    const size_t used=std::strlen(result->log);
    if (used+2>=sizeof(result->log)) return;
    va_list args; va_start(args,format);
    std::vsnprintf(result->log+used,sizeof(result->log)-used,format,args);
    va_end(args);
    const size_t end=std::strlen(result->log);
    if(end+1<sizeof(result->log)) {result->log[end]='\n';result->log[end+1]=0;}
}
}
#define ESP_LOGW(...) logFailure(__VA_ARGS__)
const char *esp_err_to_name(int) {return "INJECTED_IO_ERROR";}
void vTaskDelay(int) {}
void *heap_caps_malloc(size_t n, int caps) {
    if (n!=1024 || caps!=(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)) std::abort();
    if(failAllocation) return nullptr;
    ++result->allocations; ++result->outstanding; return std::malloc(n);
}
void heap_caps_free(void *p) {
    if(p) {--result->outstanding; std::free(p);}
}
const esp_partition_t *esp_partition_find_first(int, int, const char *) {return &partition;}
int esp_partition_read(const esp_partition_t *, size_t offset, void *output, size_t n) {
    ++result->reads;
    if(result->reads==cancelRead) cancelled.store(true);
    if(result->reads==failRead || offset>flashSize || n>flashSize-offset) return 9;
    std::memcpy(output,flash+offset,n);return ESP_OK;
}
int esp_partition_write(const esp_partition_t *, size_t, const void *, size_t) {
    ++result->writes;return 9;
}
int esp_partition_erase_range(const esp_partition_t *, size_t, size_t) {
    ++result->erases;return 9;
}
namespace vault {
bool hash(const void *p,size_t n,uint8_t digest[32]) {
    ++result->hashes;return oracle(p,n,digest)!=0;
}
}
#include "vault_storage.inc"

extern "C" __declspec(dllexport) void probe(const unsigned char *data, size_t size,
    int ioFailure, int cancellation, int allocationFailure, Oracle sha, ProbeResult *output) {
    *output={};flash=data;flashSize=size;failRead=ioFailure;cancelRead=cancellation;
    failAllocation=allocationFailure;oracle=sha;result=output;cancelled.store(false);
    vault::Disk disk;std::string error;
    output->ok=vault::load(disk,error,cancelled);
    output->slot=disk.slot;output->blank=disk.blank;output->generation=disk.generation;
    output->bundle=static_cast<unsigned>(disk.bundle.size());
    output->config=static_cast<unsigned>(disk.config.size());
    std::snprintf(output->error,sizeof(output->error),"%s",error.c_str());
}
