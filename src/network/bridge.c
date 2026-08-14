#include "nsdock/bridge.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define IP_BIN "/system/bin/ip" // mesmo binário usado em veth.c

static nsdock_status_t run_ip_cmd(char *const argv[]) {
    pid_t pid = fork();

    if (pid < 0)
        return NSDOCK_ERR_SYSCALL;

    if (pid == 0) {
        execv(IP_BIN, argv);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return NSDOCK_ERR_SYSCALL;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NSDOCK_ERR_SYSCALL;

    return NSDOCK_OK;
}

// Cria a bridge e opcionalmente atribui um IP a ela (ex: "172.20.0.1/24"),
// que serve de gateway pros containers conectados
nsdock_status_t nsdock_bridge_create(const char *bridge_name, const char *bridge_ip) {
    char *argv1[] = {
        "ip", "link", "add", "name", (char *)bridge_name, "type", "bridge",
        NULL
    };
    nsdock_status_t status = run_ip_cmd(argv1);
    if (status != NSDOCK_OK)
        return status;

    if (bridge_ip != NULL && strlen(bridge_ip) > 0) {
        char *argv2[] = {
            "ip", "addr", "add", (char *)bridge_ip, "dev", (char *)bridge_name,
            NULL
        };
        status = run_ip_cmd(argv2);
        if (status != NSDOCK_OK)
            return status;
    }

    char *argv3[] = {
        "ip", "link", "set", (char *)bridge_name, "up",
        NULL
    };
    return run_ip_cmd(argv3);
}

// Anexa uma interface (tipicamente a ponta host de um veth) à bridge
nsdock_status_t nsdock_bridge_attach(const char *bridge_name, const char *iface_name) {
    char *argv[] = {
        "ip", "link", "set", (char *)iface_name, "master", (char *)bridge_name,
        NULL
    };
    return run_ip_cmd(argv);
}

nsdock_status_t nsdock_bridge_destroy(const char *bridge_name) {
    // derruba a interface antes de deletar, evita erro de "device busy" em alguns kernels
    char *argv1[] = {
        "ip", "link", "set", (char *)bridge_name, "down",
        NULL
    };
    run_ip_cmd(argv1); // ignora erro, segue pra deletar de qualquer forma

    char *argv2[] = {
        "ip", "link", "del", (char *)bridge_name, "type", "bridge",
        NULL
    };
    return run_ip_cmd(argv2);
}
