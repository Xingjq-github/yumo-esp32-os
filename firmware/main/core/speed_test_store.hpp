#pragma once
#include "speed_test_model.hpp"
#include <string>
#include <vector>

namespace core::speed::store {
inline constexpr size_t MaxRecordBytes=65536;
struct Entry {std::string filename;Record record;};
struct Page {std::vector<Entry> entries;std::string cursor;bool more=false;unsigned skipped=0;int error=0;};
struct Hooks {void *context=nullptr;bool (*cancelled)(void *)=nullptr;};
bool validFilename(const std::string &name);
// Pure filesystem layer. Device callers hold an SD Session for all operations,
// and an exclusive mutation claim for save/erase. No SSID is used as a path.
int save(const std::string &directory,const std::string &filename,const Record &record,Hooks hooks={});
int erase(const std::string &directory,const Entry &expected,Hooks hooks={});
int read(const std::string &directory,const std::string &filename,Record &record);
Page list(const std::string &directory,const std::string &before={},Hooks hooks={});
std::string encode(const Record &record);
bool decode(const std::string &json,Record &record);
} // namespace core::speed::store
