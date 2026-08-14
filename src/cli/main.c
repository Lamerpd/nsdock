#define _GNU_SOURCE
#include <sys/mount.h>
#include <errno.h>
#include "nsdock/common.h"
#include "nsdock/namespaces.h"
#include "nsdock/cgroups.h"
#include "nsdock/rootfs.h"
#include "nsdock/overlay.h"
#include "nsdock/network.h"
#include "nsdock/bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define NSDOCK_DATA_ROOT "/data/local/nsdock"
#define DEFAULT_BRIDGE   "nsdock0"
#define DEFAULT_BRIDGE_IP "172.20.0.1/24"

// Gera um ID curto pro container, baseado em timestamp + pid
// (simples, não é criptograficamente único, mas serve bem aqui)
static void generate_container_id(char *out, size_t out_size) {
    snprintf(out, out_size, "%lx%x", (unsigned long)time(NULL), getpid());
}

static void write_container_meta(const char *container_id, const char *name, pid_t pid) {
    char path[NSDOCK_MAX_PATH];
    snprintf(path, sizeof(path), "%s/containers/%s/pid", NSDOCK_DATA_ROOT, container_id);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d", pid); fclose(f); }

    snprintf(path, sizeof(path), "%s/containers/%s/name", NSDOCK_DATA_ROOT, container_id);
    f = fopen(path, "w");
    if (f) { fprintf(f, "%s", name ? name : container_id); fclose(f); }
}

// Roda DENTRO do processo já isolado (após unshare + segundo fork).
// Vira o PID 1 do namespace novo, faz o pivot_root e executa o comando final.
static int container_init(const char *merged_dir, char *const argv[]) {
    if (nsdock_rootfs_pivot(merged_dir) != NSDOCK_OK) {
        fprintf(stderr, "nsdock: falha no pivot_root\n");
        return 1;
    }

    // monta /proc de novo -- pivot_root não traz isso automaticamente,
    // e sem /proc o PID namespace novo não funciona direito (ps, /proc/1 etc)
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        fprintf(stderr, "nsdock: aviso -- falha ao montar /proc\n");
    }

    execvp(argv[0], argv);

    // só chega aqui se execvp falhar
    fprintf(stderr, "nsdock: falha ao executar '%s': %s\n", argv[0], strerror(errno));
    return 127;
}

static int cmd_run(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "uso: nsdock run <imagem>[:tag] [comando...]\n");
        return 1;
    }

    char image[NSDOCK_MAX_NAME];
    char tag[NSDOCK_MAX_NAME] = "latest";

    char *colon = strchr(argv[0], ':');
    if (colon) {
        size_t name_len = colon - argv[0];
        strncpy(image, argv[0], name_len);
        image[name_len] = '\0';
        strncpy(tag, colon + 1, sizeof(tag) - 1);
    } else {
        strncpy(image, argv[0], sizeof(image) - 1);
    }

    // comando a rodar dentro do container: o resto do argv, ou /bin/sh por padrão
    char *container_argv[32];
    int cmd_argc = 0;
    if (argc > 1) {
        for (int i = 1; i < argc && cmd_argc < 31; i++)
            container_argv[cmd_argc++] = argv[i];
    } else {
        container_argv[cmd_argc++] = "/bin/sh";
    }
    container_argv[cmd_argc] = NULL;

    char container_id[32];
    generate_container_id(container_id, sizeof(container_id));

    printf("nsdock: preparando container %s (imagem %s:%s)...\n", container_id, image, tag);

    // 1. Baixa a imagem se ainda não tiver em cache local
    char image_cache_dir[NSDOCK_MAX_PATH];
    snprintf(image_cache_dir, sizeof(image_cache_dir), "%s/images/%s-%s", NSDOCK_DATA_ROOT, image, tag);
    mkdir(image_cache_dir, 0755);

    if (nsdock_image_pull(image, tag, image_cache_dir) != NSDOCK_OK) {
        fprintf(stderr, "nsdock: aviso -- pull falhou (usando cache local se existir)\n");
    }

    // 2. Prepara o rootfs (overlay) em cima da imagem baixada
    if (nsdock_rootfs_prepare(container_id, image_cache_dir) != NSDOCK_OK) {
        fprintf(stderr, "nsdock: falha ao preparar rootfs\n");
        return 1;
    }

    char merged_dir[NSDOCK_MAX_PATH];
    snprintf(merged_dir, sizeof(merged_dir), "%s/containers/%s/merged", NSDOCK_DATA_ROOT, container_id);

    // 3. Cria o cgroup com limites padrão (por enquanto sem customização via flags)
    nsdock_cgroup_limits_t limits = { .memory_limit_bytes = 0, .cpu_shares = 0, .pids_max = 512 };
    nsdock_cgroup_create(container_id, limits);

    // 4. Primeiro fork + unshare: cria os namespaces
    pid_t outer_pid = fork();
    if (outer_pid < 0) {
        perror("fork");
        return 1;
    }

    if (outer_pid == 0) {
        // dentro do primeiro filho: unshare cria os namespaces novos
        if (nsdock_unshare(NS_PID | NS_MNT | NS_NET | NS_UTS | NS_IPC) != NSDOCK_OK) {
            fprintf(stderr, "nsdock: unshare falhou (rodando como root?)\n");
            _exit(1);
        }

        // segundo fork: ESTE processo nasce já dentro do PID namespace novo,
        // e portanto vira o PID 1 de dentro do container
        pid_t inner_pid = fork();
        if (inner_pid < 0) {
            perror("fork (inner)");
            _exit(1);
        }

        if (inner_pid == 0) {
            _exit(container_init(merged_dir, container_argv));
        }

        // primeiro filho vira só um "reaper", espera o PID 1 real terminar
        int status;
        waitpid(inner_pid, &status, 0);
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }

    // 5. Processo pai (fora de tudo): anexa cgroup, configura rede, registra metadata
    nsdock_cgroup_attach(container_id, outer_pid);
    write_container_meta(container_id, container_id, outer_pid);

    // rede: cria a bridge se ainda não existir (ignora erro se já existe) e o veth do container
    nsdock_bridge_create(DEFAULT_BRIDGE, DEFAULT_BRIDGE_IP);

    nsdock_veth_config_t veth_cfg;
    snprintf(veth_cfg.host_iface, sizeof(veth_cfg.host_iface), "veth%s", container_id);
    strncpy(veth_cfg.container_iface, "eth0", sizeof(veth_cfg.container_iface));
    snprintf(veth_cfg.container_ip, sizeof(veth_cfg.container_ip), "172.20.0.2/24"); // TODO: alocar IP único por container
    strncpy(veth_cfg.bridge_name, DEFAULT_BRIDGE, sizeof(veth_cfg.bridge_name));

    if (nsdock_veth_create(veth_cfg, outer_pid) == NSDOCK_OK) {
        nsdock_bridge_attach(DEFAULT_BRIDGE, veth_cfg.host_iface);
    }

    printf("nsdock: container %s rodando (pid %d)\n", container_id, outer_pid);

    int status;
    waitpid(outer_pid, &status, 0);

    printf("nsdock: container %s finalizado\n", container_id);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int cmd_ps(void) {
    char containers_dir[NSDOCK_MAX_PATH];
    snprintf(containers_dir, sizeof(containers_dir), "%s/containers", NSDOCK_DATA_ROOT);

    DIR *dir = opendir(containers_dir);
    if (!dir) {
        printf("nenhum container encontrado.\n");
        return 0;
    }

    printf("%-16s %-10s %-8s\n", "CONTAINER ID", "PID", "STATUS");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char pid_path[NSDOCK_MAX_PATH];
        snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", containers_dir, entry->d_name);

        FILE *f = fopen(pid_path, "r");
        if (!f)
            continue;

        pid_t pid;
        if (fscanf(f, "%d", &pid) == 1) {
            // kill com sinal 0 só checa se o processo existe, sem matar
            const char *status = (kill(pid, 0) == 0) ? "rodando" : "parado";
            printf("%-16s %-10d %-8s\n", entry->d_name, pid, status);
        }
        fclose(f);
    }

    closedir(dir);
    return 0;
}

static int cmd_stop(const char *container_id) {
    char pid_path[NSDOCK_MAX_PATH];
    snprintf(pid_path, sizeof(pid_path), "%s/containers/%s/pid", NSDOCK_DATA_ROOT, container_id);

    FILE *f = fopen(pid_path, "r");
    if (!f) {
        fprintf(stderr, "nsdock: container %s não encontrado\n", container_id);
        return 1;
    }

    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);

    if (kill(pid, SIGTERM) != 0) {
        fprintf(stderr, "nsdock: falha ao parar %s: %s\n", container_id, strerror(errno));
        return 1;
    }

    printf("nsdock: %s parado\n", container_id);
    return 0;
}

static int cmd_rm(const char *container_id) {
    nsdock_rootfs_cleanup(container_id);
    nsdock_cgroup_destroy(container_id);

    char host_iface[NSDOCK_MAX_NAME];
    snprintf(host_iface, sizeof(host_iface), "veth%s", container_id);
    nsdock_veth_destroy(host_iface);

    // remoção recursiva de fato fica pendente (precisa de um rm_rf helper) --
    // por ora isso remove só os metadados soltos, não os dados do container
    char meta_dir[NSDOCK_MAX_PATH];
    snprintf(meta_dir, sizeof(meta_dir), "%s/containers/%s", NSDOCK_DATA_ROOT, container_id);
    char pid_path[NSDOCK_MAX_PATH], name_path[NSDOCK_MAX_PATH];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", meta_dir);
    snprintf(name_path, sizeof(name_path), "%s/name", meta_dir);
    remove(pid_path);
    remove(name_path);
    rmdir(meta_dir);

    printf("nsdock: %s removido\n", container_id);
    return 0;
}

int main(int argc, char *argv[]) {
    if (getuid() != 0) {
        fprintf(stderr, "nsdock: precisa rodar como root (su)\n");
        return 1;
    }

    if (argc < 2) {
        printf("uso: nsdock <run|ps|stop|rm> [args...]\n");
        return 1;
    }

    mkdir(NSDOCK_DATA_ROOT, 0755);
    char containers_root[NSDOCK_MAX_PATH];
    snprintf(containers_root, sizeof(containers_root), "%s/containers", NSDOCK_DATA_ROOT);
    mkdir(containers_root, 0755);

    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 2, argv + 2);
    } else if (strcmp(argv[1], "ps") == 0) {
        return cmd_ps();
    } else if (strcmp(argv[1], "stop") == 0) {
        if (argc < 3) { fprintf(stderr, "uso: nsdock stop <id>\n"); return 1; }
        return cmd_stop(argv[2]);
    } else if (strcmp(argv[1], "rm") == 0) {
        if (argc < 3) { fprintf(stderr, "uso: nsdock rm <id>\n"); return 1; }
        return cmd_rm(argv[2]);
    }

    fprintf(stderr, "nsdock: comando desconhecido '%s'\n", argv[1]);
    return 1;
}
