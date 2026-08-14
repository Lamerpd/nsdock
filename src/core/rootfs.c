#include "nsdock/rootfs.h"
#include "nsdock/overlay.h"

#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>

#define NSDOCK_DATA_ROOT "/data/local/nsdock"

// Monta os caminhos padrão de um container: onde ficam as camadas,
// a camada gravável e o ponto final (merged) que vira o "/" do container
static void build_container_paths(const char *container_id, nsdock_overlay_config_t *cfg) {
    snprintf(cfg->lower_dirs, NSDOCK_MAX_PATH,
             "%s/images/base/layer", NSDOCK_DATA_ROOT); // camada(s) somente-leitura da imagem base

    snprintf(cfg->upper_dir, NSDOCK_MAX_PATH,
             "%s/containers/%s/upper", NSDOCK_DATA_ROOT, container_id);

    snprintf(cfg->work_dir, NSDOCK_MAX_PATH,
             "%s/containers/%s/work", NSDOCK_DATA_ROOT, container_id);

    snprintf(cfg->merged_dir, NSDOCK_MAX_PATH,
             "%s/containers/%s/merged", NSDOCK_DATA_ROOT, container_id);
}

static nsdock_status_t ensure_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return NSDOCK_ERR_SYSCALL;
    return NSDOCK_OK;
}

// Cria a estrutura de pastas (upper/work/merged) e monta o overlay
// usando image_path como a camada base (lower) da imagem
nsdock_status_t nsdock_rootfs_prepare(const char *container_id, const char *image_path) {
    nsdock_overlay_config_t cfg;
    build_container_paths(container_id, &cfg);

    // image_path sobrescreve a camada base padrão, se fornecido
    if (image_path != NULL && strlen(image_path) > 0) {
        snprintf(cfg.lower_dirs, NSDOCK_MAX_PATH, "%s", image_path);
    }

    char container_dir[NSDOCK_MAX_PATH];
    snprintf(container_dir, sizeof(container_dir), "%s/containers/%s", NSDOCK_DATA_ROOT, container_id);

    if (ensure_dir(NSDOCK_DATA_ROOT) != NSDOCK_OK) return NSDOCK_ERR_SYSCALL;
    if (ensure_dir(container_dir) != NSDOCK_OK) return NSDOCK_ERR_SYSCALL;
    if (ensure_dir(cfg.upper_dir) != NSDOCK_OK) return NSDOCK_ERR_SYSCALL;
    if (ensure_dir(cfg.work_dir) != NSDOCK_OK) return NSDOCK_ERR_SYSCALL;
    if (ensure_dir(cfg.merged_dir) != NSDOCK_OK) return NSDOCK_ERR_SYSCALL;

    return nsdock_overlay_mount(cfg);
}

// Troca o "/" do processo atual pro rootfs montado (merged_dir do overlay).
// Precisa rodar DENTRO do container, já isolado no mount namespace (NS_MNT),
// senão você troca o "/" do sistema real -- perigoso.
nsdock_status_t nsdock_rootfs_pivot(const char *new_root) {
    char put_old[NSDOCK_MAX_PATH];
    snprintf(put_old, sizeof(put_old), "%s/.old_root", new_root);

    if (ensure_dir(put_old) != NSDOCK_OK)
        return NSDOCK_ERR_SYSCALL;

    // new_root precisa ser um mount point -- overlay já garante isso
    if (chdir(new_root) != 0)
        return NSDOCK_ERR_SYSCALL;

    if (syscall(SYS_pivot_root, ".", ".old_root") != 0)
        return (errno == EPERM) ? NSDOCK_ERR_PERMISSION : NSDOCK_ERR_SYSCALL;

    if (chdir("/") != 0)
        return NSDOCK_ERR_SYSCALL;

    // desmonta o root antigo de dentro do novo namespace de mount,
    // sem afetar o sistema real (já estamos isolados por NS_MNT)
    if (umount2("/.old_root", MNT_DETACH) != 0)
        return NSDOCK_ERR_SYSCALL;

    rmdir("/.old_root");

    return NSDOCK_OK;
}

nsdock_status_t nsdock_rootfs_cleanup(const char *container_id) {
    nsdock_overlay_config_t cfg;
    build_container_paths(container_id, &cfg);

    nsdock_overlay_unmount(cfg.merged_dir);

    // Nota: aqui só desmonta. Remover upper/work/merged do disco
    // fica pra um nsdock_rootfs_destroy separado, já que às vezes
    // você quer manter o estado do container parado (tipo docker stop).

    return NSDOCK_OK;
}
