#ifndef BORDIFY_NET_H
#define BORDIFY_NET_H

#include "types.h"

void net_init(void);
bool net_ready(void);
const char *net_status(void);
const char *net_last_error(void);

int net_http_get(const char *host, uint16_t port, const char *path,
                 char *out, uint32_t max_out,
                 char *location, size_t location_size,
                 int *status_out);

#endif
