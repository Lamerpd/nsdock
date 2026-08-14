#ifndef NSDOCK_CGROUPS_H
#define NSDOCK_CGROUPS_H

#include "common.h"

typedef struct {
    uint64_t memory_limit_bytes;
    uint32_t cpu_shares;
    uint32_t pids_max;
} nsdock_cgroup_limits_t;

nsdock_status_t nsdock_cgroup_create(const char *container_id, nsdock_cgroup_limits_t limits);
nsdock_status_t nsdock_cgroup_attach(const char *container_id, pid_t pid);
nsdock_status_t nsdock_cgroup_destroy(const char *container_id);

#endif // NSDOCK_CGROUPS_H
