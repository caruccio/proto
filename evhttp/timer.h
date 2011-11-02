#pragma once

#include "client.h"

typedef enum { READ, WRITE, READ_BE, WRITE_BE } TIMER;

void mark_time(struct client *cli, TIMER which);
