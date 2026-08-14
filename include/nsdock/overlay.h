#ifndef NSDOCK_OVERLAY_H
#define NSDOCK_OVERLAY_H

#include "common.h"

typedef struct {
    char lower_dirs[NSDOCK_MAX_PATH];
    char upper_dir[NSDOCK_MAX_PATH];
    char work_dir[NSDOCK_MAX_PATH];
    char merged_dir[NSDOCK_MAX_PATH];
} nsdock_overlay_config_t;

nsdock_status_t nsdock_overlay_mount(nsdock_overlay_config_t config);
nsdock_status_t nsdock_overlay_unmount(const char *merged_dir);

#endif // NSDOCK_OVERLAY_H
