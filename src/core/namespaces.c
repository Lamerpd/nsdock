#define _GNU_SOURCE
#include <stdlib.h>
#include "nsdock/namespaces.h"

#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

// Converte as flags do nsdock pros flags nativos do unshare/clone
static int nsdock_flags_to_native(nsdock_ns_flags_t flags) {
    int native = 0;
    if (flags & NS_PID)  native |= CLONE_NEWPID;
    if (flags & NS_MNT)  native |= CLONE_NEWNS;
    if (flags & NS_NET)  native |= CLONE_NEWNET;
    if (flags & NS_UTS)  native |= CLONE_NEWUTS;
    if (flags & NS_IPC)  native |= CLONE_NEWIPC;
    if (flags & NS_USER) native |= CLONE_NEWUSER;
    return native;
}

nsdock_status_t nsdock_unshare(nsdock_ns_flags_t flags) {
    int native_flags = nsdock_flags_to_native(flags);

    if (unshare(native_flags) != 0) {
        if (errno == EPERM)
            return NSDOCK_ERR_PERMISSION;
        return NSDOCK_ERR_SYSCALL;
    }

    return NSDOCK_OK;
}

nsdock_status_t nsdock_setns(pid_t target_pid, nsdock_ns_flags_t flags) {
    char ns_path[NSDOCK_MAX_PATH];
    const char *ns_names[] = {"pid", "mnt", "net", "uts", "ipc", "user"};
    nsdock_ns_flags_t ns_flags[] = {NS_PID, NS_MNT, NS_NET, NS_UTS, NS_IPC, NS_USER};

    for (int i = 0; i < 6; i++) {
        if (!(flags & ns_flags[i]))
            continue;

        snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/%s", target_pid, ns_names[i]);

        int fd = open(ns_path, O_RDONLY);
        if (fd < 0)
            return NSDOCK_ERR_NOT_FOUND;

        if (setns(fd, 0) != 0) {
            close(fd);
            return (errno == EPERM) ? NSDOCK_ERR_PERMISSION : NSDOCK_ERR_SYSCALL;
        }

        close(fd);
    }

    return NSDOCK_OK;
}

// fn roda dentro dos novos namespaces, no processo filho
pid_t nsdock_clone_container(int (*fn)(void *), void *arg, nsdock_ns_flags_t flags) {
    pid_t pid = fork();

    if (pid < 0) {
        return -1; // erro no fork
    }

    if (pid == 0) {
        // processo filho: entra nos namespaces e roda fn
        if (nsdock_unshare(flags) != NSDOCK_OK) {
            _exit(EXIT_FAILURE);
        }

        int ret = fn(arg);
        _exit(ret);
    }

    // processo pai: retorna o pid do filho pro caller gerenciar
    return pid;
}
