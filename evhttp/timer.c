#include "timer.h"
#include "client.h"
#include <time.h>

void mark_time(struct client *cli, TIMER which)
{
	clock_gettime(CLOCK_MONOTONIC, &cli->times[which]);
}
