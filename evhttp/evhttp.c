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


//
// gcc -Wall -Werror -o lighttz lighttz.c -ggdb3 -O0 -lev -I /usr/include/libev && ./lighttz
//

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
#include <time.h>
#include <ev.h>

ev_io ev_accept;
static int SERVER_PORT = 3077;
static const uint32_t EOH = 0x0a0d0a0d; //\r\n\r\n
static const char response_header_fmt[] = "%s %u %s\r\nContent-Length: %i\r\nConnection: close\r\nContent-Type: %s\r\n\r\n";
static const char HTTP10[] = "HTTP/1.0";
//static uint64_t count = 0;

typedef enum { READ, WRITE, READ_BE, WRITE_BE } TIMER;

typedef void (*event_cb)(struct ev_loop *, struct ev_io *, int);

/*
 * Client
 */

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

struct client {
	ev_io ev_read;
	ev_io ev_write;
	int fd;
	int fd_be;
	int status;
	struct header_handler req;
	struct header_handler res;
	struct timespec times[4];
};

struct client* new_client(int fd)
{
	struct client *cli = calloc(1, sizeof(*cli));

	cli->fd = fd;
	cli->fd_be = -1;
	cli->status = 0;

	cli->req.buffer[sizeof(cli->req.buffer) - 1] = 0;
	cli->req.len = 0;
	cli->req.method = cli->req.buffer;
	cli->req.uri = NULL;
	cli->req.version = NULL;
	cli->req.header = NULL;
	cli->req.header_len = -1;
	cli->req.content = NULL;
	cli->req.content_len = 0;

	cli->res.buffer[sizeof(cli->res.buffer) - 1] = 0;
	cli->res.len = 0;
	cli->res.method = NULL; //nao aplicavel
	cli->res.uri = NULL; //nao aplicavel
	cli->res.version = HTTP10;
	cli->res.header = cli->res.buffer;
	cli->res.header_len = -1;
	cli->res.content = NULL;
	cli->res.content_len = 0;

	memset(&cli->times, 0, sizeof(cli->times));

	return cli;
}

void delete_client(struct client *cli)
{
	close(cli->fd);
	if (cli->fd_be != -1)
		close(cli->fd_be);
	free(cli);
	printf("- %lu.%09lu %lu.%09lu\n", cli->times[READ].tv_sec, cli->times[READ].tv_nsec, cli->times[WRITE].tv_sec, cli->times[WRITE].tv_nsec);
}

static void mark_time(struct client *cli, TIMER which)
{
	clock_gettime(CLOCK_MONOTONIC, &cli->times[which]);
}

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
		cli->res.header_len = snprintf(cli->res.header, sizeof(cli->res.buffer), response_header_fmt,
												 cli->res.version, cli->status, "OK", cli->res.content_len, "text/plain");
		cli->res.header[cli->res.header_len] = '\0';
		//printf("res [%s]", cli->res.header);
	//	if (++count % 100 == 0)
	//		printf("res [%llu]\n", count);
		cli->res.content = cli->res.header + cli->res.header_len;
		strncat(cli->res.content, HELLO, cli->res.content_len); //TODO: overflow

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

typedef enum {
	REQUEST_INVALID         = 0, //nada recebido OU cabecalhos incompletos
	REQUEST_HEADER_COMPLETE,     //todos os cabecalhos recebidos E validos
	REQUEST_INCOMPLETE,          //cabechalos OU dados ainda faltando
	REQUEST_COMPLETE,            //todos os cabecalhos E dados recebidos E validos
} REQUEST_STATUS;

static int request_complete_header(struct header_handler *req)
{
	return req->method && req->uri && req->version && req->header_len > -1;
}

static int request_parse_method(struct header_handler *req)
{
	if (req->header == NULL) {
		int i = 0;
		//procura pelo primeiro cabecalho, depois da primeira linha
		for (i = 0; i < req->len; ++i) {
			if (req->buffer[i] == '\r' && req->buffer[i + 1] == '\n') {
				req->header = req->buffer + i + 2;
					if (req->header[0] == '\r' && req->header[1] == '\n') {
						req->header_len = req->len;
					}
				break;
			}
		}
	}

	if (req->header > req->method) {
		unsigned int mlen = 0;

		// metodo
		char *c = req->buffer;
		while (isspace(*c) && c < req->header)
			++c;
		req->method = c;
		if (req->method[0] == 'G' && req->method[1] == 'E' && req->method[2] == 'T' && isspace(req->method[3])) {
			mlen = 3;
		} else {
			printf("ret=-1\n");
			return -1;
		}
		req->method[mlen] = '\0';

		// uri
		c = req->method + mlen + 1;
		while (isspace(*c) && c < req->header)
			++c;
		if (c != req->header)
			req->uri = c;
		while ((!isspace(*c)) && c < req->header)
			++c;
		if (isspace(*c))
			*c++ = '\0';

		// versao
		while (isspace(*c) && c < req->header)
			++c;
		if (c != req->header)
			req->version = c;
		while ((!isspace(*c)) && c < req->header)
			++c;
//		if (isspace(*c))
//			*c = '\0';

		//printf("ret=1 [%s] %s [%s]\n", req->method, req->uri, req->version);
		return 1;
	}

	printf("ret=0\n");
	return 0;
}

static int request_parse_headers(struct header_handler *req)
{
	if (req->header_len == 0)
		return 1;

	if (req->len > 3 ) {
		//grande chance do cabecalho inteiro ja ter sido recebido e ser a unica coisa enviada (GET/DELETE)
		uint32_t *eoh = (uint32_t *)(req->buffer + req->len - sizeof(EOH));
		if (*eoh == EOH) {
			req->header_len = req->len;
			return 1;
		}

		//procura por "\r\n\r\n" a partir do inicio dos cabecalhos
		int i = 0;
		eoh = (uint32_t *)req->header;
		for (i = 0; i < (req->len - (req->header - req->buffer)) ; ++i) {
			if (*eoh == EOH) {
				req->header_len = (req->buffer + req->len - 4) - req->header;
				return 1;
			}
			eoh = (uint32_t *)(req->header + i);
		}

		return -1;
	}

	return 0;
}

static int request_complete_content(struct header_handler *req)
{
	if (req->content_len == 0) {
		return 1;
	} else if (req->content_len == -1) {
		return 0;
	}

	return req->len > 0 && //recebi algo
			 req->header_len > 0 &&  //tenho algum header
			 req->content_len == (req->len - req->header_len); //ja tenho todo o content
}

static int request_complete(struct header_handler *req)
{
	return request_complete_header(req) && request_complete_content(req);
}

static REQUEST_STATUS parse_request_header(struct header_handler *req)
{
	int ret = -1;

	if ((ret = request_parse_method(req)) == -1) {
		return REQUEST_INVALID;
	} else if (ret == 0) {
		return REQUEST_INCOMPLETE;
	}

	if ((ret = request_parse_headers(req)) == -1) {
		return REQUEST_INVALID;
	}

	return request_complete(req) ? REQUEST_COMPLETE : REQUEST_INCOMPLETE;
}

static void read_be_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli= ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));
	mark_time(cli, READ_BE);
	char dns_buffer[4096];
	int ret = recv(cli->fd_be, dns_buffer, sizeof(dns_buffer), 0);

	if (ret <= 0) {
		ev_io_stop(EV_A_ w);
		delete_client(cli);
		return;
	}

//	printf("recv: %i\n", ret);
	if (ret > 0)
		cli->status = 200;
	else
		cli->status = 404;
	ev_io_stop(EV_A_ w);
	ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
}


static void write_be_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));
	mark_time(cli, WRITE_BE);
	struct sockaddr_in in;
	in.sin_family = AF_INET;
	in.sin_port = htons(53);
	inet_aton("10.225.0.17", &in.sin_addr);
	char dns_buffer[] = { 0x53, 0x3a, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
								 0x00, 0x00, 0x00, 0x00, 0x05, 0x74, 0x65, 0x72,
								 0x72, 0x61, 0x03, 0x63, 0x6f, 0x6d, 0x02, 0x62,
								 0x72, 0x00, 0x00, 0x01, 0x00, 0x01
	};

	if (sendto(cli->fd_be, dns_buffer, sizeof(dns_buffer), 0, (struct sockaddr *)&in, sizeof(in)) == -1) {
		cli->status = 500;
		ev_io_stop(EV_A_ w);
		ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
		ev_io_start(loop, &cli->ev_write);
	} else {
		ev_io_stop(EV_A_ w);
		ev_io_init(&cli->ev_read, read_be_cb, cli->fd_be, EV_READ);
		ev_io_start(loop, &cli->ev_read);
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

	return 0;
}

static void read_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli= ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));

	if (revents & EV_READ) {
		int ret = read(cli->fd, cli->req.buffer, sizeof(cli->req.buffer) - cli->req.len - 1);

		if (ret > 0) {
			cli->req.len += ret;
			switch (parse_request_header(&cli->req)) {
				case REQUEST_HEADER_COMPLETE:
				case REQUEST_INCOMPLETE:
					return;

				case REQUEST_COMPLETE:
					ev_io_stop(EV_A_ w);
					if (start_backend(cli) == 0) {
						ev_io_init(&cli->ev_write, write_be_cb, cli->fd_be, EV_WRITE);
						ev_io_start(loop, &cli->ev_write);
					} else {
						cli->status = 500;
						ev_io_init(&cli->ev_write, write_cb, cli->fd, EV_WRITE);
						ev_io_start(loop, &cli->ev_write);
					}
					return;

				case REQUEST_INVALID:
				default:
					ev_io_stop(EV_A_ w);
					delete_client(cli);
					return;
			}
		} else {
			delete_client(cli);
		}
	}

	ev_io_stop(EV_A_ w);
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

//	ev_io_stop(EV_A_ w);
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
