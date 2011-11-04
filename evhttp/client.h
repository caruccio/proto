#pragma once

#include "http_parser.h"
#include "callback.h"
#include <inttypes.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <ev.h>

extern const char *HTTP10;
extern const char *response_header_fmt;

typedef enum {
	HTTP_NONE = -1,
	HTTP_200,
	HTTP_404,
	HTTP_500,
	HTTP_503,
} HTTP_STATUS;

struct client {
	ev_io ev_read;
	ev_io ev_write;
	ev_timer ev_tout;
	event_cb read_cb;
	event_cb write_cb;
	struct timespec times[4];
	int fd;
	struct sockaddr_in addr;
	int fd_be;
	struct be_oper *be;
	size_t retry;
	HTTP_STATUS status;
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
		uint8_t *content; //set by be
		size_t content_len;
	} res;
};

struct client* new_client(event_cb r, event_cb w);
void delete_client(struct client *cli);
int setnonblock(int fd);
