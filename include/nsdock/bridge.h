#ifndef NSDOCK_BRIDGE_H
#define NSDOCK_BRIDGE_H

#include "common.h"

nsdock_status_t nsdock_bridge_create(const char *bridge_name, const char *bridge_ip);
nsdock_status_t nsdock_bridge_attach(const char *bridge_name, const char *iface_name);
nsdock_status_t nsdock_bridge_destroy(const char *bridge_name);

#endif // NSDOCK_BRIDGE_H
