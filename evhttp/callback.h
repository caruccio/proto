#pragma once

#include <ev.h>

typedef void (*event_cb)(struct ev_loop *loop, struct ev_io *w, int revents);
