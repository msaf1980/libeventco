/*************************************************************************
  > File Name: test_cond.c
  > Author:
  > Mail:
  > Created Time: Tue 05 Dec 2017 04:50:17 AM PST
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "evco.h"

#include "benchutils.h"

int iter_count = 1000000000;
int count = 100000;

typedef struct consumer_args {
	int index;
}consumer_args_t;

void consumer(consumer_args_t *pargs)
{
	int ret = 0;
	int i = 0;
	for ( ; iter_count > 0; iter_count-- ) {
		evco_yield();
	}
	count--;
	free(pargs);
}

int main(int argc, char *argv[])
{
	evsc_t *psc = evsc_alloc();
	evco_cond_t *pcond = evco_cond_alloc();
	int x = count, iterations = iter_count;

	unsigned long long start, end;

	for ( ; x > 0; x-- ) {
		consumer_args_t *pargs = (consumer_args_t *)malloc(sizeof(consumer_args_t));
		pargs->index = x;
		evco_create(psc, STACK_SIZE, (evco_func)consumer, pargs);
	}

	x = count;

	start = clock_ns();

	evco_dispatch(psc);

	end = clock_ns();
	printf("evco_yield (%d yields, %d coroutines): %llu\n", iterations, x, end - start);

	return 0;
}

