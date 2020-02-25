#include <time.h>

#include "benchutils.h"

unsigned long long clock_ns()
{
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	return 1000000000uLL * tm.tv_sec + tm.tv_nsec;
}
