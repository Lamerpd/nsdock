#include "nsdock/overlay.h"

#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <errno.h>

// overlayfs precisa de uma string única "lowerdir=...,upperdir=...,workdir=..."
// passada como opções de montagem
static nsdock_status_t build_mount_options(nsdock_overlay_config_t config, char *out, size_t out_size) {
    int written = snprintf(out, out_size,
                            "lowerdir=%s,upperdir=%s,workdir=%s",
                            config.lower_dirs, config.upper_dir, config.work_dir);

    if (written < 0 || (size_t)written >= out_size)
        return NSDOCK_ERR_INVALID_ARG; // opções muito longas pro buffer

    return NSDOCK_OK;
}

nsdock_status_t nsdock_overlay_mount(nsdock_overlay_config_t config) {
    char options[NSDOCK_MAX_PATH * 3]; // três paths (lower/upper/work) cabem aqui

    nsdock_status_t status = build_mount_options(config, options, sizeof(options));
    if (status != NSDOCK_OK)
        return status;

    // "overlay" é o tipo de filesystem; source pode ser qualquer nome (convenção: "overlay")
    if (mount("overlay", config.merged_dir, "overlay", 0, options) != 0) {
        if (errno == EPERM)
            return NSDOCK_ERR_PERMISSION;
        if (errno == ENODEV)
            return NSDOCK_ERR_UNKNOWN; // kernel sem suporte a overlayfs compilado
        return NSDOCK_ERR_SYSCALL;
    }

    return NSDOCK_OK;
}

nsdock_status_t nsdock_overlay_unmount(const char *merged_dir) {
    // MNT_DETACH: desmonta mesmo se houver processos ainda usando
    // (lazy unmount) -- evita travar se o container não finalizou limpo
    if (umount2(merged_dir, MNT_DETACH) != 0) {
        if (errno == EINVAL)
            return NSDOCK_ERR_NOT_FOUND; // não estava montado
        return NSDOCK_ERR_SYSCALL;
    }

    return NSDOCK_OK;
}
