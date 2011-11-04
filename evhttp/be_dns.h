#pragma once

#include "client.h"
#include <ev.h>

void be_dns_read_cb(struct ev_loop *loop, struct ev_io *w, int revents);
void be_dns_write_cb(struct ev_loop *loop, struct ev_io *w, int revents);
int be_dns_send(struct client* cli);
