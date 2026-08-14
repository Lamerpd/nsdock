#include "nsdock/common.h"
#include <stdarg.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define NSDOCK_DATA_ROOT "/data/local/nsdock"
#define SOCKET_PATH      "/data/local/nsdock/nsdockd.sock"
#define POLL_INTERVAL_SEC 5
#define LOG_PATH         "/data/local/nsdock/nsdockd.log"

static volatile sig_atomic_t running = 1;

static void handle_shutdown(int sig) {
    (void)sig;
    running = 0;
}

static void log_msg(const char *fmt, ...) {
    FILE *f = fopen(LOG_PATH, "a");
    if (!f)
        return;

    time_t now = time(NULL);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] ", timestr);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}

// Vira um daemon de verdade: desanexa do terminal, joga stdio pro /dev/null
static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0); // processo original sai, deixa o filho seguir sozinho

    setsid(); // vira líder de sessão, se desconecta do terminal controlador

    pid_t pid2 = fork(); // segundo fork: garante que não pode reabrir terminal
    if (pid2 < 0) exit(1);
    if (pid2 > 0) exit(0);

    chdir("/");
    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

// Varre /data/local/nsdock/containers e remove entradas cujo processo
// já não existe mais (container morreu sem passar por "nsdock rm")
static void reap_dead_containers(void) {
    char containers_dir[NSDOCK_MAX_PATH];
    snprintf(containers_dir, sizeof(containers_dir), "%s/containers", NSDOCK_DATA_ROOT);

    DIR *dir = opendir(containers_dir);
    if (!dir)
        return;

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
        int got_pid = (fscanf(f, "%d", &pid) == 1);
        fclose(f);

        if (!got_pid)
            continue;

        // sinal 0 só testa existência, não mata ninguém
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            log_msg("container %s (pid %d) morreu sem cleanup -- marcando como parado", entry->d_name, pid);
            // por ora só loga; remoção de dados fica a cargo de "nsdock rm" manual,
            // pra não apagar volumes/estado que o usuário talvez queira recuperar
        }
    }

    closedir(dir);
}

// Monta a resposta de "PS" pro socket: lista containers e status, mesmo
// formato usado no cmd_ps do CLI, mas servido sem precisar reabrir tudo via novo processo
static void handle_ps_request(int client_fd) {
    char containers_dir[NSDOCK_MAX_PATH];
    snprintf(containers_dir, sizeof(containers_dir), "%s/containers", NSDOCK_DATA_ROOT);

    DIR *dir = opendir(containers_dir);
    if (!dir) {
        const char *msg = "nenhum container\n";
        write(client_fd, msg, strlen(msg));
        return;
    }

    char response[8192] = "";
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
            const char *status = (kill(pid, 0) == 0) ? "rodando" : "parado";
            char line[256];
            snprintf(line, sizeof(line), "%s\t%d\t%s\n", entry->d_name, pid, status);
            strncat(response, line, sizeof(response) - strlen(response) - 1);
        }
        fclose(f);
    }
    closedir(dir);

    write(client_fd, response, strlen(response));
}

// Cria o socket Unix onde o daemon escuta comandos simples (só "PS" por enquanto)
static int setup_socket(void) {
    unlink(SOCKET_PATH); // remove socket velho de uma execução anterior, se existir

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock_fd);
        return -1;
    }

    if (listen(sock_fd, 8) != 0) {
        close(sock_fd);
        return -1;
    }

    chmod(SOCKET_PATH, 0666); // permite qualquer processo local consultar status

    return sock_fd;
}

int main(int argc, char *argv[]) {
    bool foreground = (argc > 1 && strcmp(argv[1], "--foreground") == 0);

    if (getuid() != 0) {
        fprintf(stderr, "nsdockd: precisa rodar como root\n");
        return 1;
    }

    mkdir(NSDOCK_DATA_ROOT, 0755);

    if (!foreground)
        daemonize();

    signal(SIGTERM, handle_shutdown);
    signal(SIGINT, handle_shutdown);

    log_msg("nsdockd iniciado (pid %d)", getpid());

    int sock_fd = setup_socket();
    if (sock_fd < 0) {
        log_msg("falha ao criar socket em %s", SOCKET_PATH);
        return 1;
    }

    // non-blocking pra poder intercalar entre aceitar conexões e o reaper periódico
    fcntl(sock_fd, F_SETFL, O_NONBLOCK);

    time_t last_reap = 0;

    while (running) {
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd >= 0) {
            char cmd[64] = "";
            ssize_t n = read(client_fd, cmd, sizeof(cmd) - 1);
            if (n > 0) {
                cmd[n] = '\0';
                if (strncmp(cmd, "PS", 2) == 0) {
                    handle_ps_request(client_fd);
                }
            }
            close(client_fd);
        }

        time_t now = time(NULL);
        if (now - last_reap >= POLL_INTERVAL_SEC) {
            reap_dead_containers();
            last_reap = now;
        }

        usleep(200000); // 200ms -- evita busy loop consumindo CPU à toa
    }

    log_msg("nsdockd finalizando");
    close(sock_fd);
    unlink(SOCKET_PATH);
    return 0;
}
