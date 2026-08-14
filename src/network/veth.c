#include "nsdock/network.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define IP_BIN "/system/bin/ip" // ajuste se usar o ip do Termux/iproute2 estático

// Executa "ip <args...>" via fork+exec e espera terminar.
// argv precisa terminar com NULL.
static nsdock_status_t run_ip_cmd(char *const argv[]) {
    pid_t pid = fork();

    if (pid < 0)
        return NSDOCK_ERR_SYSCALL;

    if (pid == 0) {
        execv(IP_BIN, argv);
        _exit(127); // execv só retorna se falhar
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return NSDOCK_ERR_SYSCALL;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NSDOCK_ERR_SYSCALL;

    return NSDOCK_OK;
}

nsdock_status_t nsdock_veth_create(nsdock_veth_config_t config, pid_t target_pid) {
    // ip link add <host_iface> type veth peer name <container_iface>
    char *argv1[] = {
        "ip", "link", "add", config.host_iface,
        "type", "veth", "peer", "name", config.container_iface,
        NULL
    };
    nsdock_status_t status = run_ip_cmd(argv1);
    if (status != NSDOCK_OK)
        return status;

    // move a ponta do container pro namespace de rede do processo alvo
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", target_pid);

    char *argv2[] = {
        "ip", "link", "set", config.container_iface, "netns", pid_str,
        NULL
    };
    status = run_ip_cmd(argv2);
    if (status != NSDOCK_OK)
        return status;

    // sobe a ponta que fica no host
    char *argv3[] = {
        "ip", "link", "set", config.host_iface, "up",
        NULL
    };
    return run_ip_cmd(argv3);
}

// Precisa rodar DENTRO do namespace de rede do container (via nsdock_setns antes),
// já que "ip" sempre opera no namespace do processo que o chama.
nsdock_status_t nsdock_veth_configure_container_side(nsdock_veth_config_t config, pid_t target_pid) {
    (void)target_pid; // o caller já deve ter feito setns(NS_NET) antes de chamar isso

    char *argv1[] = {
        "ip", "addr", "add", config.container_ip, "dev", config.container_iface,
        NULL
    };
    nsdock_status_t status = run_ip_cmd(argv1);
    if (status != NSDOCK_OK)
        return status;

    char *argv2[] = {
        "ip", "link", "set", config.container_iface, "up",
        NULL
    };
    status = run_ip_cmd(argv2);
    if (status != NSDOCK_OK)
        return status;

    // sobe a loopback também, muita coisa dentro do container espera 127.0.0.1 funcionando
    char *argv3[] = {
        "ip", "link", "set", "lo", "up",
        NULL
    };
    return run_ip_cmd(argv3);
}

nsdock_status_t nsdock_veth_destroy(const char *host_iface) {
    // destruir a ponta host derruba o par inteiro (peer junto)
    char *argv[] = {
        "ip", "link", "del", (char *)host_iface,
        NULL
    };
    return run_ip_cmd(argv);
}
