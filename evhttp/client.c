#include "client.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

const char *HTTP10 = "HTTP/1.0";
//const uint32_t EOH = 0x0a0d0a0d; //\r\n\r\n
const char *response_header_fmt = "%s %u %s\r\nContent-Length: %i\r\nConnection: close\r\nContent-Type: %s\r\n\r\n";

struct client* new_client(event_cb r, event_cb w)
{
	struct client *cli = calloc(1, sizeof(*cli));

	cli->fd = -1;
	cli->fd_be = -1;
	cli->be = NULL;
	cli->retry = 0;
	cli->status = HTTP_NONE;
	cli->read_cb = r;
	cli->write_cb = w;

	cli->req.buffer[0] = '\0';
	cli->req.len = 0;
	cli->res.version = HTTP10;
	cli->res.header[0] = '\0';
	cli->res.header_len = 0;
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
	//printf("- %lu.%09lu %lu.%09lu\n", cli->times[READ].tv_sec, cli->times[READ].tv_nsec, cli->times[WRITE].tv_sec, cli->times[WRITE].tv_nsec);
}

int setnonblock(int fd)
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
