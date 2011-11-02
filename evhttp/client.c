#include "timer.h"
#include "client.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

const char *HTTP10 = "HTTP/1.0";
//const uint32_t EOH = 0x0a0d0a0d; //\r\n\r\n
const char *response_header_fmt = "%s %u %s\r\nContent-Length: %i\r\nConnection: close\r\nContent-Type: %s\r\n\r\n";

struct client* new_client(int fd)
{
	struct client *cli = calloc(1, sizeof(*cli));

	cli->fd = fd;
	cli->fd_be = -1;
	cli->status = 0;

	cli->req.buffer[0] = '\0';
	cli->req.len = 0;
/*
	cli->req.method = cli->req.buffer;
	cli->req.uri = NULL;
	cli->req.version = NULL;
	cli->req.header = NULL;
	cli->req.header_len = -1;
	cli->req.content = NULL;
	cli->req.content_len = 0;
*/
	cli->res.version = HTTP10;
	cli->res.header[0] = '\0';
	cli->res.header_len = 0;
	cli->res.content = NULL;
	cli->res.content_len = 0;

/*
	cli->res.buffer[sizeof(cli->res.buffer) - 1] = 0;
	cli->res.len = 0;
	cli->res.method = NULL; //nao aplicavel
	cli->res.uri = NULL; //nao aplicavel
	cli->res.version = HTTP10;
	cli->res.header = cli->res.buffer;
	cli->res.header_len = -1;
	cli->res.content = NULL;
	cli->res.content_len = 0;
*/
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


