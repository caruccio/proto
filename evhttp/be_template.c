#include "be_template.h"
#include "client.h"
#include "timer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdio.h>
#include "be_template.h"

static BE_STATUS be_template_init(struct be_oper *be)
{
	be->active = 1;
	be->retry = 3;
	be->tout = 2.0;

	return BE_OK;
}

static BE_STATUS be_template_finish(struct be_oper *be)
{
	be->active = 0;
	return BE_OK;
}

static BE_STATUS be_template_start(struct be_oper *be, struct client *cli)
{
	cli->fd_be = socket(AF_INET, SOCK_DGRAM, 0);
	if (cli->fd_be == -1)
		return -1;

	if (setnonblock(cli->fd_be) < 0)
		return -1;

	cli->retry = be->retry;

	return BE_OK;
}

static void be_template_read_cb(EV_P, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_read)));

	ev_timer_stop(EV_A_ &cli->ev_tout);
	mark_time(cli, READ_BE);

	static uint8_t buffer[4096];
	int ret = recv(cli->fd_be, buffer, sizeof(buffer), 0);

#ifdef DEBUG
	buffer[ret] = 0;
	printf("template recv: [%s]\n", buffer);
#endif

	cli->res.content_len = 0;
	if (ret > 0) {
		cli->status = HTTP_200;
		cli->res.content = buffer;
		cli->res.content_len = ret;
	} else if (ret == 0) {
		cli->status = HTTP_404;
	} else {
		cli->status = HTTP_500;
	}

	ev_io_stop(EV_A_ w);
	ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
	ev_io_start(EV_A, &cli->ev_write);
}

static BE_STATUS be_template_send(struct client* cli)
{
	mark_time(cli, WRITE_BE);
	if (cli->fd_be == -1) {
		if (be_template_start(&be_template_oper, cli) != BE_OK)
			return BE_ERROR;
	}
	struct sockaddr_in in;
	in.sin_family = AF_INET;
	in.sin_port = htons(7);
	inet_aton("127.0.0.1", &in.sin_addr);
	char buffer[] = "Hello World!";
	size_t len = sizeof(buffer) - 1;

	const int ret = sendto(cli->fd_be, buffer, len, 0, (struct sockaddr *)&in, sizeof(in));
	return ret == len ? BE_SENT_REQ :
			 ret < len ?  BE_SENT_REQ_PARTIAL :
			BE_SEND_ERROR;
}

static void be_template_write_cb(EV_P, struct ev_io *w, int revents)
{
	struct client *cli = ((struct client*) (((char*)w) - offsetof(struct client, ev_write)));

	ev_io_stop(EV_A_ w);
	if (be_template_send(cli) == BE_SENT_REQ) {
		ev_io_init(&cli->ev_read, be_template_read_cb, cli->fd_be, EV_READ);
		ev_io_start(EV_A, &cli->ev_read);
	} else {
		cli->status = HTTP_500;
		ev_io_init(&cli->ev_write, cli->write_cb, cli->fd, EV_WRITE);
		ev_io_start(EV_A, &cli->ev_write);
	}
}

struct be_oper be_template_oper = {
	.name = "template",
	.init = be_template_init,
	.finish = be_template_finish,
	.read_cb = be_template_read_cb,
	.write_cb = be_template_write_cb,
	.send = be_template_send,
};
