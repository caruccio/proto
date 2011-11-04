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
#include <errno.h>
#include <err.h>
#include <stddef.h>
#include <ctype.h>
#include <inttypes.h>
#include <ev.h>
#include <alloca.h>
#include "client.h"
#include "timer.h"
#include "http.h"
#include "be.h"
#include "be_chain.h"

//static uint64_t count = 0;
ev_io ev_accept;
static int SERVER_PORT = 3077;
static struct {
	const int status;
	const char* desc;
} status_mesg[] = {
	{ 200, "OK" },
	{ 404, "Not Found" },
	{ 500, "Internal Server Error" },
	{ 503, "Service Unavailable" },
};

/*
 * Callbacks
 */

static void write_cb(EV_P, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));

	if (cli->status == HTTP_NONE)
		ev_timer_stop(EV_A, &cli->ev_tout);

	mark_time(cli, WRITE);
#ifdef DEBUG
	printf("write_cb: %s:%hu -> %i\n", inet_ntoa(cli->addr.sin_addr), ntohs(cli->addr.sin_port), status_mesg[cli->status].status);
#endif

	if (revents & EV_WRITE) {
		cli->res.header_len = snprintf(cli->res.header, sizeof(cli->res.header), response_header_fmt,
												 cli->res.version, status_mesg[cli->status].status, status_mesg[cli->status].desc, cli->res.content_len, "text/plain");
		cli->res.header[cli->res.header_len] = '\0';

	//	if (++count % 100 == 0)
	//		printf("res [%llu]\n", count);

		struct iovec iov[2] = {
			{ cli->res.header, cli->res.header_len },
			{ NULL, 0 },
		};

		const size_t iov_len = cli->res.content_len > 0 ? 2 : 1;

		if (iov_len > 1 && cli->res.content) {
			iov[1].iov_base = cli->res.content;
			iov[1].iov_len  = cli->res.content_len;
		}

		writev(cli->fd, iov, iov_len);
		ev_io_stop(EV_A, w);
		delete_client(cli);
	}
	//TODO: controle de buffer
}

static void timeout_cb(EV_P, ev_timer *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_tout)));
#ifdef DEBUG
	printf("timeout\n");
#endif

	if (cli->retry > 0) {
#ifdef DEBUG
		printf("retry %zu\n", cli->retry);
#endif

		ev_io_stop(EV_A, &cli->ev_read);
		ev_io_init(&cli->ev_write, cli->be->write_cb, cli->fd_be, EV_WRITE);
		ev_io_start(EV_A, &cli->ev_write);

		//restart timeout
		ev_timer_stop(loop, &cli->ev_tout);
		ev_timer_set(&cli->ev_tout, cli->be->tout, 0);
		ev_timer_start(EV_A, &cli->ev_tout);
	} else {
		ev_timer_stop(EV_A, w);
		cli->status = HTTP_503;
		ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
		ev_io_start(EV_A, &cli->ev_write);
	}
}

static void read_cb(EV_P, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));
	cli->req.len = read(cli->fd, cli->req.buffer, sizeof(cli->req.buffer));
	ev_io_stop(EV_A, w);

	if (cli->req.len < 1 || parse_http(cli) == -1) {
		goto err_500;
	}

	struct be_oper **be_it;
	for (be_it = be_list; *be_it; ++be_it) {
		struct be_oper *be = *be_it;
		if (!be->active)
			continue;

		cli->be = be;
		switch (be->send(cli)) {
			case BE_SENT_REQ_PARTIAL:
				//schedule for next write
				ev_io_init(&cli->ev_write, be->write_cb, cli->fd_be, EV_WRITE);
				ev_io_start(EV_A, &cli->ev_write);
				return;
			case BE_SENT_REQ:
				//be is waiting for response
				ev_io_init(&cli->ev_read, be->read_cb, cli->fd_be, EV_READ);
				ev_io_start(EV_A, &cli->ev_read);
				ev_timer_init(&cli->ev_tout, timeout_cb, be->tout, 0);
				ev_timer_start(EV_A, &cli->ev_tout);
				return;
			default:
				break;
		}
	}

err_500:
	cli->status = HTTP_500;
	ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
	ev_io_start(EV_A, &cli->ev_write);
}

static void accept_cb(EV_P, struct ev_io *w, int revents)
{
	struct client *cli = new_client(read_cb, write_cb);
	socklen_t addr_len = sizeof(cli->addr);
	cli->fd = accept(w->fd, (struct sockaddr *)&cli->addr, &addr_len);

	if (cli->fd == -1) {
		delete_client(cli);
		return;
	}

	if (setnonblock(cli->fd) < 0) {
		perror("setnonblock");
		delete_client(cli);
		return;
	}

	mark_time(cli, READ);
	ev_io_init(&cli->ev_read, read_cb, cli->fd, EV_READ);
	ev_io_start(EV_A,  &cli->ev_read);
}

static void sigint_cb(EV_P, struct ev_signal *w, int revents)
{
	ev_unloop(EV_A, EVUNLOOP_ALL);
}

int main()
{
	struct ev_loop *loop = ev_default_loop(0);
	struct sockaddr_in listen_addr;
	int reuseaddr_on = 1;
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (listen_fd < 0) {
		perror("listen");
		goto out;
	}

	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on)) == -1) {
		perror("setsockopt");
		goto out;
	}

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);

	if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		perror("bind");
		goto out;
	}

	if (listen(listen_fd, 5) < 0) {
		perror("listen");
		goto out;
	}

	if (setnonblock(listen_fd) < 0) {
		perror("setnonblock");
		goto out;
	}

	if (be_init_all() != BE_OK) {
		goto out;
	}

	struct ev_signal ev_signal;
	ev_signal_init(&ev_signal, sigint_cb, SIGINT);
	ev_signal_start(loop, &ev_signal);

	ev_io_init(&ev_accept, accept_cb, listen_fd, EV_READ);
	ev_io_start(loop, &ev_accept);

#ifdef DEBUG
	printf("listen %s:%hi\n", inet_ntoa(listen_addr.sin_addr), SERVER_PORT);
#endif

	ev_loop (loop, 0);

#ifdef DEBUG
	printf("exit\n");
#endif 

out:
	close(listen_fd);
	return 0;
}
