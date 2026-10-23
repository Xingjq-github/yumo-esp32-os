#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "text_progress.hpp"
#include "sd_media.hpp"
#include "storage_usage.hpp"
namespace core {
// Captured during enumeration. Not a content hash: catches stale rows whose
// same-named file changed size/time, in addition to the mounted-card identity.
struct FileIdentity { uint64_t size=0; int64_t modified=0; bool valid=false; };
struct FileEntry { std::string path, name; bool directory=false; FileIdentity identity{}; };
struct StorageJob {
    std::atomic<bool> done{false}, cancelled{false};
    bool text=false, photos=false, truncated=false, removeRecording=false;
    unsigned media=0; // 1=recordings, 2=NES; existing text/photo paths unchanged
    bool usageOnly=false;
    StorageUsage usage;
    int error=0;
    uint64_t cardGeneration=0;
    FileIdentity deleteIdentity;
    sd::Access sdAccess;
    std::string path, content;
    std::vector<FileEntry> entries;
    reading::Mark progress;
};
// Worker owns only a shared result, never an App/LVGL pointer. Consume after
// done.load(acquire); cancel on hide/destroy before releasing the GUI owner.
std::shared_ptr<StorageJob> listDirectory(const std::string &path,bool photos);
std::shared_ptr<StorageJob> readText(const std::string &path);
// One-shot worker: partition metadata + FAT free clusters. Never enumerates
// files, writes data or resumes an ejected/USB-owned card.
std::shared_ptr<StorageJob> inspectStorage();
// Final page writes outlive the App only until the SD handle is closed.
// One bounded, coalescing writer; no App/LVGL pointers or permanent task.
std::shared_ptr<StorageJob> saveTextProgress(const std::string &path,reading::Mark mark,uint64_t cardGeneration);
std::shared_ptr<StorageJob> listMedia(bool games);
// Only called after explicit GUI confirmation; removes one regular /recordings/*.wav.
std::shared_ptr<StorageJob> deleteRecording(const std::string &path,uint64_t cardGeneration,FileIdentity identity);
std::string parentDirectory(const std::string &path);
bool storageIdle(); // true only after workers close their file/directory handles
const char *storageError(int error);
} // namespace core
