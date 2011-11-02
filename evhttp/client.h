#pragma once

#include "http_parser.h"
#include <inttypes.h>
#include <stdlib.h>
#include <ev.h>
/*
struct header_handler {
	char buffer[4096];
	size_t len;
	//manipulamos apenas os ponteiros para os dados
	char *method;
	char *uri;
	const char *version;
	char *header;
	int header_len;
	char *content;
	int content_len;
};

extern const uint32_t EOH;
*/
extern const char *HTTP10;
extern const char *response_header_fmt;

struct client {
	ev_io ev_read;
	ev_io ev_write;
	ev_timer ev_tout;
	int fd;
	int fd_be;
	int status;
	struct http_parser_settings settings;
	struct http_parser parser;
//	struct header_handler req;
//	struct header_handler res;
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

struct client* new_client(int fd);
void delete_client(struct client *cli);
