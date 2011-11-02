/*
Copyright (c) 2008, Arek Bochinski
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, 
    * this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, 
    * this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Arek Bochinski nor the names of its contributors may be used to endorse or 
    * promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Compile command on my system, may be changed depending on your setup.

gcc -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"lighttz.d" -MT"lighttz.d" -o"lighttz.o" "../lighttz.c"
gcc  -o"lighttz"  ./lighttz.o   -lev

You need to have Libev installed:
http://software.schmorp.de/pkg/libev.html
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h> 
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <stddef.h>
#include <ctype.h>
#include <inttypes.h>
#include <ev.h>
#include <alloca.h>
#include "client.h"
#include "timer.h"
#include "http_parser.h"

ev_io ev_accept;
static int SERVER_PORT = 3077;
//static uint64_t count = 0;

/*
 * Callbacks
 */

static void write_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));
	mark_time(cli, WRITE);

	if (revents & EV_WRITE) {
		#define HELLO "Hello World!"
		cli->res.content_len = sizeof(HELLO) - 1;
		cli->res.header_len = snprintf(cli->res.header, sizeof(cli->res.header), response_header_fmt,
												 cli->res.version, cli->status, "OK", cli->res.content_len, "text/plain");
		cli->res.header[cli->res.header_len] = '\0';

	//	if (++count % 100 == 0)
	//		printf("res [%llu]\n", count);

		cli->res.content = alloca(cli->res.content_len);
		cli->res.content[0] = '\0';
		strncat(cli->res.content, HELLO, cli->res.content_len); //TODO: overflow or segv (man alloca)

		struct iovec /*{
			void  *iov_base;    // Starting address
			size_t iov_len;     // Number of bytes to transfer
		} */ vdata[2] = {
			{ cli->res.header, cli->res.header_len },
			{ cli->res.content, cli->res.content_len },
		};

		writev(cli->fd, vdata, sizeof(vdata) / sizeof(struct iovec));
		ev_io_stop(EV_A_ w);
		delete_client(cli);
	}
	//TODO: controle de buffer
}

static void read_be_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));
	ev_timer_stop(EV_A_ &cli->ev_tout);
	mark_time(cli, READ_BE);
	char dns_buffer[4096];
	int ret = recv(cli->fd_be, dns_buffer, sizeof(dns_buffer), 0);

	if (ret <= 0) {
		ev_io_stop(EV_A_ w);
		delete_client(cli);
		return;
	}

//	printf("recv: %i\n", ret);
	if (ret > 0) {
		cli->status = 200;
	} else {
		cli->status = 404;
	}

	ev_io_stop(EV_A_ w);
	ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
}

static int send_be(struct client* cli)
{
	mark_time(cli, WRITE_BE);
	struct sockaddr_in in;
	in.sin_family = AF_INET;
	in.sin_port = htons(53);
	inet_aton("8.8.8.8", &in.sin_addr);
	char dns_buffer[] = { 0x53, 0x3a, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
								 0x00, 0x00, 0x00, 0x00, 0x05, 0x74, 0x65, 0x72,
								 0x72, 0x61, 0x03, 0x63, 0x6f, 0x6d, 0x02, 0x62,
								 0x72, 0x00, 0x00, 0x01, 0x00, 0x01
	};

	const int ret = sendto(cli->fd_be, dns_buffer, sizeof(dns_buffer), 0, (struct sockaddr *)&in, sizeof(in));
	return ret == sizeof(dns_buffer) ? 0 : -1;
}

static void write_be_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));

	ev_io_stop(EV_A_ w);
	if (send_be(cli) == 0) {
		ev_io_init(&cli->ev_read, read_be_cb, cli->fd_be, EV_READ);
		ev_io_start(loop, &cli->ev_read);
	} else {
		cli->status = 500;
		ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
		ev_io_start(loop, &cli->ev_write);
	}
}

static int setnonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

	return 0;
}

static int start_backend(struct client *cli)
{
	cli->fd_be = socket(AF_INET, SOCK_DGRAM, 0);
	if (cli->fd_be == -1)
		return -1;

	if (setnonblock(cli->fd_be) < 0)
		return -1;

	return 1;
}

static int on_message_begin_cb(http_parser* parser)
{
	//printf("%s\n", __FUNCTION__);
	return 0;
}

static int on_url_cb(http_parser* parser, const char *at, size_t length)
{
//	printf("%s %s [%s] %zu.\n", __FUNCTION__, http_method_str(parser->method), at, length);
	return 0;
}

static int on_header_field_cb(http_parser* parser, const char *at, size_t length)
{
//	printf("%s HTTP/%hu.%hu [%s] %zu.\n", __FUNCTION__, parser->http_major, parser->http_minor, at, length);
	return 0;
}

static int on_header_value_cb(http_parser* parser, const char *at, size_t length)
{
//	printf("%s [%s] %zu.\n", __FUNCTION__, at, length);
	return 0;
}

static int on_headers_complete_cb(http_parser* parser)
{
//	printf("%s\n", __FUNCTION__);
	return 0;
}

static int on_body_cb(http_parser* parser, const char *at, size_t length)
{
//	printf("%s [%s] %zu.\n", __FUNCTION__, at, length);
	return 0;
}

static int on_message_complete_cb(http_parser* parser)
{
//	printf("%s\n", __FUNCTION__);
	return 0;
}

static void read_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));
	const int ret = read(cli->fd, cli->req.buffer, sizeof(cli->req.buffer) - cli->req.len - 1);

	if (ret < 1) {
		goto err_500;
	}

	cli->settings.on_message_begin = on_message_begin_cb;
	cli->settings.on_url = on_url_cb;
	cli->settings.on_header_field = on_header_field_cb;
	cli->settings.on_header_value = on_header_value_cb;
	cli->settings.on_headers_complete = on_headers_complete_cb;
	cli->settings.on_body = on_body_cb;
	cli->settings.on_message_complete = on_message_complete_cb;
	http_parser_init(&cli->parser, HTTP_REQUEST);

	if (http_parser_execute(&cli->parser, &cli->settings, cli->req.buffer, ret) != ret) {
		goto err_500;
	}

	switch (start_backend(cli)) {
		case 0:
			ev_io_stop(EV_A_ w);
			ev_io_init(&cli->ev_write, write_be_cb, cli->fd_be, EV_WRITE);
			ev_io_start(loop, &cli->ev_write);
			return;
		case 1:
			ev_io_stop(EV_A_ w);
			if (send_be(cli) == 0) {
				ev_io_init(&cli->ev_read, read_be_cb, cli->fd_be, EV_READ);
				ev_io_start(loop, &cli->ev_read);
			} else {
				cli->status = 500;
				ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
				ev_io_start(loop, &cli->ev_write);
			}
			return;
		default:
			break;
	}

err_500:
	cli->status = 500;
	ev_io_stop(EV_A_ w);
	ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
}

static void tout_cb(EV_P_ ev_timer *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_tout)));
	ev_timer_stop(EV_A_ w);
	cli->status = 503;
	ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
}

static void accept_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	int client_fd;
	struct client *cli;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	client_fd = accept(w->fd, (struct sockaddr *)&client_addr, &client_len);

	if (client_fd == -1) {
		return;
	}

	cli = new_client(client_fd);
	mark_time(cli, READ);

	if (setnonblock(cli->fd) < 0)
		err(1, "failed to set client socket to non-blocking");

	ev_timer_init(&cli->ev_tout, tout_cb, 5.0, 0.0);
	ev_timer_start(loop, &cli->ev_tout);

	ev_io_init(&cli->ev_read, read_cb, cli->fd, EV_READ);
	ev_io_start(loop, &cli->ev_read);
}

int main()
{
	struct ev_loop *loop = ev_default_loop(0);
	struct sockaddr_in listen_addr;
	int reuseaddr_on = 1;
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (listen_fd < 0)
		err(1, "listen failed");

	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on)) == -1)
		err(1, "setsockopt failed");

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);

	if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0)
		err(1, "bind failed");

	if (listen(listen_fd, 5) < 0)
		err(1, "listen failed");

	if (setnonblock(listen_fd) < 0)
		err(1, "failed to set server socket to non-blocking");

	ev_io_init(&ev_accept, accept_cb, listen_fd, EV_READ);
	ev_io_start(loop, &ev_accept);
	printf("start :%s:%hi\n", inet_ntoa(listen_addr.sin_addr), SERVER_PORT);
	ev_loop (loop, 0);
	printf("exit\n");

	return 0;
}
