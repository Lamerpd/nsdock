#ifndef NSDOCK_NAMESPACES_H
#define NSDOCK_NAMESPACES_H

#include "common.h"
#include <sys/types.h>

typedef enum {
    NS_PID  = 1 << 0,
    NS_MNT  = 1 << 1,
    NS_NET  = 1 << 2,
    NS_UTS  = 1 << 3,
    NS_IPC  = 1 << 4,
    NS_USER = 1 << 5,
    NS_ALL  = NS_PID | NS_MNT | NS_NET | NS_UTS | NS_IPC | NS_USER
} nsdock_ns_flags_t;

nsdock_status_t nsdock_unshare(nsdock_ns_flags_t flags);
nsdock_status_t nsdock_setns(pid_t target_pid, nsdock_ns_flags_t flags);
pid_t nsdock_clone_container(int (*fn)(void *), void *arg, nsdock_ns_flags_t flags);

#endif // NSDOCK_NAMESPACES_H
