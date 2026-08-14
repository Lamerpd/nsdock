#ifndef NSDOCK_ROOTFS_H
#define NSDOCK_ROOTFS_H

#include "common.h"

nsdock_status_t nsdock_rootfs_prepare(const char *container_id, const char *image_path);
nsdock_status_t nsdock_rootfs_pivot(const char *new_root);
nsdock_status_t nsdock_rootfs_cleanup(const char *container_id);

#endif // NSDOCK_ROOTFS_H
