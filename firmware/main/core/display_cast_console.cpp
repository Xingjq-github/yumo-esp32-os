// Keep primary UART diagnostics. Reserve the USB auxiliary sink only while
// casting; restore it before the task exits. No SDK/config/USB identity changes.
#include "driver/esp_private/usb_serial_jtag_vfs.h"
#include <sys/lock.h>
#include <new>
namespace {
_lock_t gate;
bool reserved=false;
const esp_vfs_fs_ops_t *original=nullptr;
alignas(esp_vfs_fs_ops_t) unsigned char storage[sizeof(esp_vfs_fs_ops_t)];
const esp_vfs_fs_ops_t *filtered=nullptr;
ssize_t writeUsb(void *ctx,int fd,const void *data,size_t size) {
    _lock_acquire(&gate);
    const auto result=reserved?static_cast<ssize_t>(size):original->write_p(ctx,fd,data,size);
    _lock_release(&gate);return result;
}
}
extern "C" const esp_vfs_fs_ops_t *__real_esp_vfs_usb_serial_jtag_get_vfs(void);
extern "C" const esp_vfs_fs_ops_t *__wrap_esp_vfs_usb_serial_jtag_get_vfs(void) {
    if(!original) {
        original=__real_esp_vfs_usb_serial_jtag_get_vfs();
        // IDF 6.1's callbacks are const members: construct an immutable table
        // in static storage during console setup, before C++ global constructors.
        filtered=new(storage) esp_vfs_fs_ops_t{
            .write_p=writeUsb,
            .lseek_p=original->lseek_p,
            .read_p=original->read_p,
            .pread_p=original->pread_p,
            .pwrite_p=original->pwrite_p,
            .open_p=original->open_p,
            .close_p=original->close_p,
            .fstat_p=original->fstat_p,
            .fcntl_p=original->fcntl_p,
            .ioctl_p=original->ioctl_p,
            .fsync_p=original->fsync_p,
#ifdef CONFIG_VFS_SUPPORT_DIR
            .dir=original->dir,
#endif
#ifdef CONFIG_VFS_SUPPORT_TERMIOS
            .termios=original->termios,
#endif
#ifdef CONFIG_VFS_SUPPORT_SELECT
            .select=original->select,
#endif
        };
    }
    return filtered;
}
void displayCastReserveConsole(bool on) {
    _lock_acquire(&gate);reserved=on;_lock_release(&gate);
}
