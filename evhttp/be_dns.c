#include "be_dns.h"
#include "client.h"
#include "timer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdio.h>

static BE_STATUS be_dns_start(struct client *cli)
{
	cli->fd_be = socket(AF_INET, SOCK_DGRAM, 0);
	if (cli->fd_be == -1)
		return -1;

	if (setnonblock(cli->fd_be) < 0)
		return -1;

	return BE_STARTED;
}

void be_dns_read_cb(struct ev_loop *loop, struct ev_io *w, int revents)
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

	dns_buffer[ret] = 0;
	printf("recv: %s\n", dns_buffer);

	if (ret > 0) {
		cli->status = 200;
	} else {
		cli->status = 404;
	}

	ev_io_stop(EV_A_ w);
	ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
}

BE_STATUS be_dns_send(struct client* cli)
{
	mark_time(cli, WRITE_BE);
	if (be_dns_start(cli) != BE_STARTED)
		return BE_ERROR;
	struct sockaddr_in in;
	in.sin_family = AF_INET;
#if 0
	in.sin_port = htons(53);
	inet_aton("1.2.3.4", &in.sin_addr); //timeout
	inet_aton("8.8.8.8", &in.sin_addr);
	inet_aton("10.225.0.17", &in.sin_addr);
	char dns_buffer[] = { 0x53, 0x3a, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
								 0x00, 0x00, 0x00, 0x00, 0x05, 0x74, 0x65, 0x72,
								 0x72, 0x61, 0x03, 0x63, 0x6f, 0x6d, 0x02, 0x62,
								 0x72, 0x00, 0x00, 0x01, 0x00, 0x01 };
	size_t len = sizeof(dns_buffer);
#else
	in.sin_port = htons(7);
	inet_aton("127.0.0.1", &in.sin_addr);
	char dns_buffer[] = "Hello World!";
	size_t len = sizeof(dns_buffer) - 1;
#endif

	const int ret = sendto(cli->fd_be, dns_buffer, len, 0, (struct sockaddr *)&in, sizeof(in));
	return ret == len ? BE_SENT_REQ :
			 ret  < len ?  BE_SENT_REQ_PARTIAL :
			BE_SEND_ERROR;
}

void be_dns_write_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));

	ev_io_stop(EV_A_ w);
	if (be_dns_send(cli) == BE_SENT_REQ) {
		ev_io_init(&cli->ev_read, be_dns_read_cb, cli->fd_be, EV_READ);
		ev_io_start(loop, &cli->ev_read);
	} else {
		cli->status = 500;
		ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
		ev_io_start(loop, &cli->ev_write);
	}
}
