#pragma once

#include "callback.h"

typedef enum {
	BE_ERROR              = -1,
	BE_OK                 = 0,
	BE_SENT_REQ,
	BE_SENT_REQ_PARTIAL,
	BE_SEND_ERROR,
} BE_STATUS;

struct be_oper;
struct client;

typedef BE_STATUS (*be_send)(struct client *);
typedef BE_STATUS (*be_env)(struct be_oper *);

struct be_oper {
	const char* name;
	int active;
	size_t retry;
	float tout;
	be_env init;
	be_env finish;
	event_cb read_cb;
	event_cb write_cb;
	be_send send;
};

BE_STATUS be_init_all();
