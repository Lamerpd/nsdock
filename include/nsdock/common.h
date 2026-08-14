#ifndef NSDOCK_COMMON_H
#define NSDOCK_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define NSDOCK_VERSION "0.1.0"
#define NSDOCK_MAX_PATH 4096
#define NSDOCK_MAX_NAME 256
#define NSDOCK_MAX_ID_LEN 64

typedef enum {
    NSDOCK_OK = 0,
    NSDOCK_ERR_PERMISSION = -1,
    NSDOCK_ERR_NOT_FOUND = -2,
    NSDOCK_ERR_SYSCALL = -3,
    NSDOCK_ERR_INVALID_ARG = -4,
    NSDOCK_ERR_ALREADY_EXISTS = -5,
    NSDOCK_ERR_UNKNOWN = -99
} nsdock_status_t;

typedef struct {
    char id[NSDOCK_MAX_ID_LEN];
    char name[NSDOCK_MAX_NAME];
    pid_t pid;
} nsdock_container_t;

const char *nsdock_strerror(nsdock_status_t status);

#endif // NSDOCK_COMMON_H

// --- Image/Layer API (image/layer.c, image/pull.c, image/manifest.c) ---
nsdock_status_t nsdock_image_pull(const char *image_name, const char *tag, const char *dest_dir);
nsdock_status_t nsdock_layer_extract(const char *tar_path, const char *layers_root, int layer_index);
nsdock_status_t nsdock_layer_build_lowerdir(const char *layers_root, int layer_count, char *out, size_t out_size);
nsdock_status_t nsdock_layer_cleanup(const char *layers_root);
nsdock_status_t nsdock_detect_platform(char *arch_out, size_t arch_size);
nsdock_status_t nsdock_manifest_resolve_platform(const char *manifest_list_json, char *digest_out, size_t digest_out_size);
