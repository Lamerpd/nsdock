#ifndef NSDOCK_NETWORK_H
#define NSDOCK_NETWORK_H

#include "common.h"
#include <sys/types.h>

typedef struct {
    char host_iface[NSDOCK_MAX_NAME];
    char container_iface[NSDOCK_MAX_NAME];
    char container_ip[32];
    char bridge_name[NSDOCK_MAX_NAME];
} nsdock_veth_config_t;

nsdock_status_t nsdock_veth_create(nsdock_veth_config_t config, pid_t target_pid);
nsdock_status_t nsdock_veth_destroy(const char *host_iface);
nsdock_status_t nsdock_veth_configure_container_side(nsdock_veth_config_t config, pid_t target_pid);

#endif // NSDOCK_NETWORK_H
