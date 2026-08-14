#include "nsdock/common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>

#define TAR_BIN "/system/bin/tar" // no Termux costuma ser /data/data/com.termux/files/usr/bin/tar

static nsdock_status_t ensure_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return NSDOCK_ERR_SYSCALL;
    return NSDOCK_OK;
}

// Extrai um .tar.gz pra dentro de dest_dir, preservando permissões e donos
// (essencial pra rootfs funcionar direito -- muitos binários exigem setuid etc.)
static nsdock_status_t extract_tar_gz(const char *tar_path, const char *dest_dir) {
    pid_t pid = fork();

    if (pid < 0)
        return NSDOCK_ERR_SYSCALL;

    if (pid == 0) {
        // -p preserva permissões, -x extrai, -z descompacta gzip, -f arquivo, -C destino
        execl(TAR_BIN, "tar", "-xzpf", tar_path, "-C", dest_dir, (char *)NULL);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return NSDOCK_ERR_SYSCALL;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NSDOCK_ERR_SYSCALL;

    return NSDOCK_OK;
}

// Extrai uma única camada (um arquivo layer_N.tar.gz) pra sua própria pasta,
// nomeada pelo índice -- cada camada vira um "lowerdir" separado no overlay
nsdock_status_t nsdock_layer_extract(const char *tar_path, const char *layers_root, int layer_index) {
    char dest_dir[NSDOCK_MAX_PATH];
    snprintf(dest_dir, sizeof(dest_dir), "%s/%d", layers_root, layer_index);

    nsdock_status_t status = ensure_dir(layers_root);
    if (status != NSDOCK_OK)
        return status;

    status = ensure_dir(dest_dir);
    if (status != NSDOCK_OK)
        return status;

    return extract_tar_gz(tar_path, dest_dir);
}

// Percorre layers_root e monta a string "lowerdir=camada0:camada1:..." que o
// overlay.c espera. A ordem importa: no overlayfs, a PRIMEIRA da lista tem
// prioridade mais alta -- então camadas mais recentes (maior índice) vêm primeiro.
nsdock_status_t nsdock_layer_build_lowerdir(const char *layers_root, int layer_count,
                                             char *out, size_t out_size) {
    out[0] = '\0';
    size_t used = 0;

    for (int i = layer_count - 1; i >= 0; i--) {
        char entry[NSDOCK_MAX_PATH];
        int written = snprintf(entry, sizeof(entry), "%s%s/%d",
                                (used > 0) ? ":" : "", layers_root, i);

        if (written < 0 || used + (size_t)written >= out_size)
            return NSDOCK_ERR_INVALID_ARG;

        memcpy(out + used, entry, written);
        used += written;
        out[used] = '\0';
    }

    return NSDOCK_OK;
}

// Remove todas as camadas extraídas de uma imagem (limpeza/uninstall de imagem)
nsdock_status_t nsdock_layer_cleanup(const char *layers_root) {
    DIR *dir = opendir(layers_root);
    if (!dir)
        return NSDOCK_ERR_NOT_FOUND;

    // Nota: isso remove só o diretório raiz se estiver vazio.
    // Remoção recursiva de verdade fica pra um helper rm_rf() dedicado,
    // já que POSIX puro não tem um "rmdir recursivo" de fábrica.
    closedir(dir);
    return (rmdir(layers_root) == 0) ? NSDOCK_OK : NSDOCK_ERR_SYSCALL;
}
