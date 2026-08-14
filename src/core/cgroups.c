#include "nsdock/cgroups.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define CGROUP_V1_ROOT "/sys/fs/cgroup"
#define CGROUP_V2_ROOT "/sys/fs/cgroup/unified"
#define CGROUP_V2_PROBE "/sys/fs/cgroup/cgroup.controllers"

typedef enum {
    CGROUP_UNKNOWN = 0,
    CGROUP_V1,
    CGROUP_V2
} cgroup_version_t;

// Detecta a versão disponível checando se o arquivo característico do v2 existe
static cgroup_version_t detect_cgroup_version(void) {
    struct stat st;

    if (stat(CGROUP_V2_PROBE, &st) == 0)
        return CGROUP_V2;

    if (stat(CGROUP_V1_ROOT "/memory", &st) == 0)
        return CGROUP_V1;

    return CGROUP_UNKNOWN;
}

static nsdock_status_t write_file(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f)
        return (errno == EACCES || errno == EPERM) ? NSDOCK_ERR_PERMISSION : NSDOCK_ERR_SYSCALL;

    if (fprintf(f, "%s", value) < 0) {
        fclose(f);
        return NSDOCK_ERR_SYSCALL;
    }

    fclose(f);
    return NSDOCK_OK;
}

static nsdock_status_t cgroup_create_v2(const char *container_id, nsdock_cgroup_limits_t limits) {
    char path[NSDOCK_MAX_PATH];
    char value[64];

    snprintf(path, sizeof(path), "/sys/fs/cgroup/nsdock/%s", container_id);
    if (mkdir("/sys/fs/cgroup/nsdock", 0755) != 0 && errno != EEXIST)
        return NSDOCK_ERR_SYSCALL;
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return NSDOCK_ERR_SYSCALL;

    if (limits.memory_limit_bytes > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/memory.max", path);
        snprintf(value, sizeof(value), "%llu", (unsigned long long)limits.memory_limit_bytes);
        write_file(file, value);
    }

    if (limits.pids_max > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/pids.max", path);
        snprintf(value, sizeof(value), "%u", limits.pids_max);
        write_file(file, value);
    }

    if (limits.cpu_shares > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/cpu.weight", path);
        snprintf(value, sizeof(value), "%u", limits.cpu_shares);
        write_file(file, value);
    }

    return NSDOCK_OK;
}

static nsdock_status_t cgroup_create_v1(const char *container_id, nsdock_cgroup_limits_t limits) {
    char path[NSDOCK_MAX_PATH];
    char value[64];

    const char *controllers[] = {"memory", "pids", "cpu"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s/nsdock/%s", CGROUP_V1_ROOT, controllers[i], container_id);
        char parent[NSDOCK_MAX_PATH];
        snprintf(parent, sizeof(parent), "%s/%s/nsdock", CGROUP_V1_ROOT, controllers[i]);
        mkdir(parent, 0755);
        if (mkdir(path, 0755) != 0 && errno != EEXIST)
            continue; // controller pode não existir nesse kernel, segue o baile
    }

    if (limits.memory_limit_bytes > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/memory/nsdock/%s/memory.limit_in_bytes", CGROUP_V1_ROOT, container_id);
        snprintf(value, sizeof(value), "%llu", (unsigned long long)limits.memory_limit_bytes);
        write_file(file, value);
    }

    if (limits.pids_max > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/pids/nsdock/%s/pids.max", CGROUP_V1_ROOT, container_id);
        snprintf(value, sizeof(value), "%u", limits.pids_max);
        write_file(file, value);
    }

    if (limits.cpu_shares > 0) {
        char file[NSDOCK_MAX_PATH];
        snprintf(file, sizeof(file), "%s/cpu/nsdock/%s/cpu.shares", CGROUP_V1_ROOT, container_id);
        snprintf(value, sizeof(value), "%u", limits.cpu_shares);
        write_file(file, value);
    }

    return NSDOCK_OK;
}

nsdock_status_t nsdock_cgroup_create(const char *container_id, nsdock_cgroup_limits_t limits) {
    cgroup_version_t version = detect_cgroup_version();

    switch (version) {
        case CGROUP_V2:
            return cgroup_create_v2(container_id, limits);
        case CGROUP_V1:
            return cgroup_create_v1(container_id, limits);
        default:
            return NSDOCK_ERR_UNKNOWN; // kernel sem suporte utilizável a cgroups
    }
}

nsdock_status_t nsdock_cgroup_attach(const char *container_id, pid_t pid) {
    cgroup_version_t version = detect_cgroup_version();
    char path[NSDOCK_MAX_PATH];
    char value[32];
    snprintf(value, sizeof(value), "%d", pid);

    if (version == CGROUP_V2) {
        snprintf(path, sizeof(path), "/sys/fs/cgroup/nsdock/%s/cgroup.procs", container_id);
        return write_file(path, value);
    }

    if (version == CGROUP_V1) {
        const char *controllers[] = {"memory", "pids", "cpu"};
        nsdock_status_t last_status = NSDOCK_OK;
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof(path), "%s/%s/nsdock/%s/cgroup.procs", CGROUP_V1_ROOT, controllers[i], container_id);
            last_status = write_file(path, value);
        }
        return last_status;
    }

    return NSDOCK_ERR_UNKNOWN;
}

nsdock_status_t nsdock_cgroup_destroy(const char *container_id) {
    char path[NSDOCK_MAX_PATH];
    cgroup_version_t version = detect_cgroup_version();

    if (version == CGROUP_V2) {
        snprintf(path, sizeof(path), "/sys/fs/cgroup/nsdock/%s", container_id);
        return (rmdir(path) == 0) ? NSDOCK_OK : NSDOCK_ERR_SYSCALL;
    }

    if (version == CGROUP_V1) {
        const char *controllers[] = {"memory", "pids", "cpu"};
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof(path), "%s/%s/nsdock/%s", CGROUP_V1_ROOT, controllers[i], container_id);
            rmdir(path);
        }
        return NSDOCK_OK;
    }

    return NSDOCK_ERR_UNKNOWN;
}
