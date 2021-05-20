/*************************************************************************
	> File Name: test_cond.c
	> Author: 
	> Mail: 
	> Created Time: Tue 05 Dec 2017 04:50:17 AM PST
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "eventco.h"

int count;

void producer(evco_cond_t *pcond)
{
	while ( count > 0 ) {
		evco_sleep(200);
		evco_cond_signal(pcond);
		printf("producer: evco_cond_signal..\n");
	}
}

typedef struct consumer_args {
	int index;
	evco_cond_t *pcond;
}consumer_args_t;

void consumer(consumer_args_t *pargs)
{
	int ret = 0;
	int i = 0;
	for ( i = 0; i < pargs->index * 5; i++ ) {
		ret = evco_cond_timedwait(pargs->pcond, 400);
		if ( ret == 0 ) {
			printf("consumer%02d: evco_cond_timedwait succeed...\n", pargs->index);
		}	
		else if ( ret == ETIMEDOUT ) {
			printf("consumer%02d: evco_cond_timedwait timeout...\n", pargs->index);
		}
		else {
			printf("consumer%02d: evco_cond_timedwait failed...\n", pargs->index);
		}
	}
	count--;
	printf("consumer%02d: exitting...\n", pargs->index);
	free(pargs);
}

int main(int argc, char *argv[])
{
	evsc_t *psc = evsc_alloc();
	evco_cond_t *pcond = evco_cond_alloc();
	int x = 5;

	count = x;

	(void) argc;
	(void) argv;

	evco_create(psc, STACK_SIZE, (evco_func)producer, pcond);
	
	for ( ; x > 0; x-- ) {
		consumer_args_t *pargs = (consumer_args_t *)malloc(sizeof(consumer_args_t));
		pargs->index = x;
		pargs->pcond = pcond;
		evco_create(psc, STACK_SIZE, (evco_func)consumer, pargs);
	}

	evco_dispatch(psc);
	evco_cond_free(pcond);
	evsc_free(psc);

	return 0;
}

