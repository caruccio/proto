#pragma once

#include "http_parser.h"
#include <inttypes.h>
#include <stdlib.h>
#include <ev.h>

typedef void (*event_cb)(struct ev_loop *loop, struct ev_io *w, int revents);

extern const char *HTTP10;
extern const char *response_header_fmt;

struct client {
	ev_io ev_read;
	ev_io ev_write;
	ev_timer ev_tout;
	event_cb read_cb;
	event_cb write_cb;
	int fd;
	int fd_be;
	int status;
	struct http_parser_settings settings;
	struct http_parser parser;
	struct {
		char buffer[4096];
		size_t len;
	} req;
	struct {
		const char *version;
		char header[4096];
		size_t header_len;
		char *content;
		size_t content_len;
	} res;
	struct timespec times[4];
};

struct client* new_client(int fd, event_cb r, event_cb w);
void delete_client(struct client *cli);
int setnonblock(int fd);
