#include "client.h"
#include "http_parser.h"
#include "http.h"

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

int parse_http(struct client* cli)
{
	cli->settings.on_message_begin = on_message_begin_cb;
	cli->settings.on_url = on_url_cb;
	cli->settings.on_header_field = on_header_field_cb;
	cli->settings.on_header_value = on_header_value_cb;
	cli->settings.on_headers_complete = on_headers_complete_cb;
	cli->settings.on_body = on_body_cb;
	cli->settings.on_message_complete = on_message_complete_cb;
	http_parser_init(&cli->parser, HTTP_REQUEST);
	cli->parser.data = cli;

	if (http_parser_execute(&cli->parser, &cli->settings, cli->req.buffer, cli->req.len) != cli->req.len) {
		return -1;
	}
	return 0;
}
