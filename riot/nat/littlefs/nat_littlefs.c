#include <fs/littlefs_fs.h>
#include <stdio.h>
#include <mutex.h>
#include <board.h>

#include "nat_littlefs.h"


static littlefs_desc_t fs_desc = {
    .lock = MUTEX_INIT,
};

static vfs_mount_t flash_mount = {
    .fs = &littlefs_file_system,
    .mount_point = "/main",
    .private_data = &fs_desc,
};

int init_nat_littlefs(void)
{
    fs_desc.dev = MTD_0;

    int res = vfs_mount(&flash_mount);
    if (res < 0) {
        printf("Failed mounting flash, trying format...\n");

        res = vfs_format(&flash_mount);
        if (res < 0)
        {
            printf("Format failed: %d\n", res);
            return res;
        }

        res = vfs_mount(&flash_mount);
        if (res < 0) {
            printf("Failed mounting flash: %d\n", res);
            return res;
        }
    }

    printf("Mounted flash at /main\n");

    struct statvfs buf;
    res = vfs_statvfs("/main/dummy.txt", &buf);
    if (res < 0)
        printf("Failed getting flash stats\n");
    else
        printf("Flash usage: %ld/%ld bytes in %ld files.\n",
               (unsigned long int)((buf.f_blocks - buf.f_bavail) * buf.f_bsize),
               (unsigned long int)(buf.f_blocks * buf.f_bsize),
               (unsigned long int)buf.f_files);

    return 0;
}
