#include "nsdock/common.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#define MAX_MANIFEST_ENTRIES 16

typedef struct {
    char digest[128];
    char architecture[32];
    char os[32];
} nsdock_manifest_entry_t;

// Traduz o que uname() devolve pro nome de arquitetura que o Docker/OCI usa
// nos manifests. Ex: kernel diz "aarch64", Docker chama de "arm64".
static const char *normalize_arch(const char *machine) {
    if (strcmp(machine, "aarch64") == 0) return "arm64";
    if (strcmp(machine, "armv7l") == 0)  return "arm";
    if (strcmp(machine, "armv8l") == 0)  return "arm64";
    if (strcmp(machine, "x86_64") == 0)  return "amd64";
    if (strcmp(machine, "i686") == 0)    return "386";
    return machine; // desconhecido -- deixa como veio e tenta mesmo assim
}

nsdock_status_t nsdock_detect_platform(char *arch_out, size_t arch_size) {
    struct utsname info;

    if (uname(&info) != 0)
        return NSDOCK_ERR_SYSCALL;

    const char *normalized = normalize_arch(info.machine);

    if (strlen(normalized) >= arch_size)
        return NSDOCK_ERR_INVALID_ARG;

    strcpy(arch_out, normalized);
    return NSDOCK_OK;
}

// Faz o parse de uma "manifest list" (multi-arquitetura) e extrai as entradas.
// Formato esperado dentro do JSON: uma lista "manifests":[ {digest, platform:{architecture,os}}, ... ]
// Nota: scanner simplificado, mesmo estilo do pull.c -- não é um parser JSON genérico.
static nsdock_status_t parse_manifest_list(const char *json, nsdock_manifest_entry_t *entries, int *count) {
    *count = 0;
    const char *cursor = json;

    while (*count < MAX_MANIFEST_ENTRIES) {
        const char *digest_key = strstr(cursor, "\"digest\":\"");
        if (!digest_key)
            break;

        digest_key += strlen("\"digest\":\"");
        const char *digest_end = strchr(digest_key, '"');
        if (!digest_end)
            break;

        nsdock_manifest_entry_t *entry = &entries[*count];
        size_t dlen = digest_end - digest_key;
        if (dlen >= sizeof(entry->digest)) dlen = sizeof(entry->digest) - 1;
        memcpy(entry->digest, digest_key, dlen);
        entry->digest[dlen] = '\0';

        // procura architecture/os DEPOIS desse digest, ANTES do próximo
        const char *next_digest = strstr(digest_end, "\"digest\":\"");
        size_t search_limit = next_digest ? (size_t)(next_digest - digest_end) : strlen(digest_end);

        char segment[2048];
        size_t seg_len = search_limit < sizeof(segment) - 1 ? search_limit : sizeof(segment) - 1;
        memcpy(segment, digest_end, seg_len);
        segment[seg_len] = '\0';

        const char *arch_key = strstr(segment, "\"architecture\":\"");
        if (arch_key) {
            arch_key += strlen("\"architecture\":\"");
            const char *arch_end = strchr(arch_key, '"');
            if (arch_end) {
                size_t alen = arch_end - arch_key;
                if (alen >= sizeof(entry->architecture)) alen = sizeof(entry->architecture) - 1;
                memcpy(entry->architecture, arch_key, alen);
                entry->architecture[alen] = '\0';
            }
        } else {
            entry->architecture[0] = '\0';
        }

        const char *os_key = strstr(segment, "\"os\":\"");
        if (os_key) {
            os_key += strlen("\"os\":\"");
            const char *os_end = strchr(os_key, '"');
            if (os_end) {
                size_t olen = os_end - os_key;
                if (olen >= sizeof(entry->os)) olen = sizeof(entry->os) - 1;
                memcpy(entry->os, os_key, olen);
                entry->os[olen] = '\0';
            }
        } else {
            entry->os[0] = '\0';
        }

        (*count)++;
        cursor = next_digest ? next_digest : digest_end;
        if (!next_digest)
            break;
    }

    return (*count > 0) ? NSDOCK_OK : NSDOCK_ERR_NOT_FOUND;
}

// Ponto de entrada: recebe o JSON de uma manifest list e devolve o digest
// correto pra arquitetura do device atual (linux/arm64 na maioria dos casos)
nsdock_status_t nsdock_manifest_resolve_platform(const char *manifest_list_json,
                                                   char *digest_out, size_t digest_out_size) {
    char my_arch[32];
    nsdock_status_t status = nsdock_detect_platform(my_arch, sizeof(my_arch));
    if (status != NSDOCK_OK)
        return status;

    nsdock_manifest_entry_t entries[MAX_MANIFEST_ENTRIES];
    int count = 0;
    status = parse_manifest_list(manifest_list_json, entries, &count);
    if (status != NSDOCK_OK)
        return status;

    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].architecture, my_arch) == 0 &&
            strcmp(entries[i].os, "linux") == 0) {

            if (strlen(entries[i].digest) >= digest_out_size)
                return NSDOCK_ERR_INVALID_ARG;

            strcpy(digest_out, entries[i].digest);
            return NSDOCK_OK;
        }
    }

    return NSDOCK_ERR_NOT_FOUND; // arquitetura do device não disponível nessa imagem
}
