/*************************************************************************
	> File Name: evco.c
	> Author: 
	> Mail: 
	> Created Time: Tue 05 Dec 2017 04:50:17 AM PST
 ************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "list.h"
#include "hashtab.h"
#include "event2/event.h"
#include "eventco.h"

#include <coro.h>

#ifdef DEBUG
    #define evco_debug(fmt, ...) \
            fprintf(stderr, "[%s:%04d]"fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define evco_debug(fmt, ...)
#endif


struct evsc
{
	struct event_base *ev_base;
	hashtab_t *fd_tb;
	struct list_head cond_ready_queue;
	struct evco *root;
};

struct evco
{
	struct evco *prev;
	coro_context self;
	struct coro_stack stack;
	struct event *timer;
	int flag_iotimeout;
	int flag_running;
	evsc_t *psc;
	evco_func func;
};

typedef struct fd_item
{
	int fd;

	struct event *ev_recv;
	struct event *ev_send;

	evco_t *evco_recv;
	evco_t *evco_send;
}fd_item_t;

typedef struct cond_item
{
	struct list_head entry;
	int timeout_msec;
	evco_t *pco;
}cond_item_t;

struct evco_cond
{
	struct list_head wait_pcos;
};

#ifdef WIN32
__declspec( thread ) evco_t *g_current_pco = NULL;
#else
__thread evco_t *g_current_pco = NULL;
#endif

#define FREE_POINTER(ptr)       \
do {                           \
    if ( ptr != NULL ) {       \
        free(ptr);             \
        ptr = NULL;            \
    }                          \
} while ( 0 )

static void __evsc_fd_dispatch(int sockfd, short events, void *vitem);

static void __evsc_timer_dispatch(int fd, short events, void *vitem);

static void __evco_resume(evco_t *pco);

#define msec2tv(msec, tv) \
do {								\
	tv.tv_sec = msec/1000;			\
	tv.tv_usec = (msec%1000)*1000;	\
} while ( 0 )

static unsigned get_page_size()
{
	static unsigned sz;
	long retval;
	if (sz > 0)
		return sz;
	retval = sysconf(_SC_PAGESIZE);
	if (0 > retval) {
		fprintf(stderr, "libevfibers: sysconf(_SC_PAGESIZE): %s",
				strerror(errno));
		abort();
	}
	sz = retval;
	return sz;
}

static size_t round_up_to_page_size(size_t size)
{
	unsigned sz = get_page_size();
	size_t remainder;
	remainder = size % sz;
	if (remainder == 0)
		return size;
	return size + sz - remainder;
}

int __evco_cond_ready_clear(evsc_t *psc)
{
	cond_item_t *pitem = NULL;
	while ( 1 ) {
		if ( list_empty(&psc->cond_ready_queue) ) {
			return 0;
		}
		pitem = list_entry(psc->cond_ready_queue.next, cond_item_t, entry);
		__evco_resume(pitem->pco);
	}
}


#define __evco_free(pco)          \
do {							  \
	if ( !pco ) break;			  \
	if ( pco->timer ) {			  \
		event_del(pco->timer);	  \
		event_free(pco->timer);   \
	}							  \
	coro_destroy(&pco->self);	  \
	coro_stack_free(&pco->stack); \
	FREE_POINTER(pco);			  \
} while ( 0 )


#ifdef WIN32
void CALLBACK __evco_entry(void *args)
#else
void __evco_entry(void *args)
#endif
{
	g_current_pco->func(args);
	g_current_pco->flag_running = 0;
}

static fd_item_t *__fd_item_alloc(evsc_t *psc, int fd)
{
	fd_item_t *pitem = (fd_item_t *)malloc(sizeof(fd_item_t));
	pitem->fd = fd;
	pitem->evco_recv = NULL;
	pitem->evco_send = NULL;
	pitem->ev_recv = event_new(psc->ev_base, fd, EV_READ, __evsc_fd_dispatch, &pitem->evco_recv);
	pitem->ev_send = event_new(psc->ev_base, fd, EV_WRITE, __evsc_fd_dispatch, &pitem->evco_send);
	return pitem;
}

static void __evco_resume(evco_t *pco)
{
	int ret = 0;
	assert(pco->prev != NULL);
	pco->prev = g_current_pco;
	g_current_pco = pco;
	
	if (pco->prev == NULL) {
		ret = -1;
	} else {
		coro_transfer(&pco->prev->self, &pco->self);
		ret = 0;
	}

	g_current_pco = pco->prev;
	if ( ret != 0 ) {
		evco_debug("__evco_resume failed...\n");
		__evco_free(pco);		
	}
	else {
		if ( pco->flag_running == 0 ) {
			__evco_free(pco);
		}
	}
}
static void inline __evco_yield()
{
    g_current_pco->flag_iotimeout = 0;
	coro_transfer(&g_current_pco->self, &g_current_pco->prev->self);
}

static void __evco_yield_by_fd(int fd, int flag, unsigned int to_msec)
{
	fd_item_t *pitem = (fd_item_t *)hashtab_query(g_current_pco->psc->fd_tb, fd);
	struct event *pev = NULL;
	int ret = 0;
	if ( pitem == NULL ) {
		pitem = __fd_item_alloc(g_current_pco->psc, fd);
		hashtab_insert(g_current_pco->psc->fd_tb, fd, pitem);
	}
	if ( flag == 0 ) {
		pitem->evco_recv = g_current_pco;
		pev = pitem->ev_recv;
	}
	else {
		pev = pitem->ev_send;
		pitem->evco_send = g_current_pco;
	}
	if ( to_msec == 0 ) {
		ret = event_add(pev, NULL);
	}
	else {
		struct timeval tv;
		msec2tv(to_msec, tv);
		ret = event_add(pev, &tv);	
	}
	if ( ret < 0 ) {
		evco_debug("event_add failed...\n");
	}

	__evco_yield();
}

static void __evsc_fd_dispatch(int sockfd, short events, void *vitem)
{
	evco_t **ppco = (evco_t **)vitem;
	evco_t *pco = *ppco;
	evsc_t *psc = NULL;
	*ppco = NULL;
	if ( pco == NULL ) {
		return;
	}
	psc = pco->psc;
    if ( events & EV_TIMEOUT ) {
        pco->flag_iotimeout = 1;
    } else {
        pco->flag_iotimeout = 0;
    }
	__evco_resume(pco);
	__evco_cond_ready_clear(psc);
}

static void __evsc_timer_dispatch(int fd, short events, void *vitem)
{
	evco_t *pco = (evco_t *)vitem;
	evsc_t *psc = pco->psc;
	pco->flag_iotimeout = 1;
	__evco_resume(pco);
	__evco_cond_ready_clear(psc);
}

void evco_yield()
{
	__evco_yield();
}

void evco_sleep(int msec)
{
	struct timeval tv;
	if ( g_current_pco->timer == NULL ) {
		g_current_pco->timer = evtimer_new(g_current_pco->psc->ev_base, __evsc_timer_dispatch, g_current_pco);
	}
	msec2tv(msec, tv);
	event_add(g_current_pco->timer, &tv);
	__evco_yield();
}

static evco_t *__evco_create_main(evsc_t *psc)
{
	evco_t *pco;
	assert(g_current_pco == NULL);
	pco = (evco_t *)malloc(sizeof(evco_t));
	if (pco == NULL) {
		return NULL;
	}
	pco->func = NULL;
	coro_create (&pco->self, NULL, NULL, NULL, 0);

	pco->flag_running = 1;

	pco->timer = NULL;
	pco->psc = psc;

	g_current_pco = pco;

	return pco;
}

evco_t *evco_create(evsc_t *psc, size_t stack_size, evco_func func, void *args)
{
	void *stack;
	evco_t *pco = (evco_t *)malloc(sizeof(evco_t));
	if (pco == NULL) {
		return NULL;
	}
	pco->func = func;
	if (coro_stack_alloc(&pco->stack, stack_size) != 1) {
		evco_debug("alloc stack failed...\n");
		goto _E1;
	}	
	coro_create (&pco->self, __evco_entry, args, pco->stack.sptr, pco->stack.ssze);

	pco->flag_running = 1;

	pco->timer = NULL;
	pco->psc = psc;
	pco->prev = g_current_pco;
	__evco_resume(pco);

	return pco;

_E1:
	__evco_free(pco);
	return NULL;
}

int evco_timed_connect(int fd, const struct sockaddr *addr, socklen_t addrlen, int msec)
{
	int ret = 0;
	evutil_make_socket_nonblocking(fd);
	ret = connect(fd, addr, addrlen);
	if ( ret == 0 ) {
		return ret;
	}
#ifdef WIN32 
	if ( WSAEINTR == WSAGetLastError() || WSAEWOULDBLOCK == WSAGetLastError() ) {
#else
	if ( errno == EINPROGRESS || errno == EWOULDBLOCK ) {
#endif
		int err = 0;
		socklen_t len = sizeof(err);
		printf("Connect INPROGREESS, will swap.\n");
		__evco_yield_by_fd(fd, 1, msec);
        if ( g_current_pco->flag_iotimeout ) {
            errno = ETIMEDOUT;
            return -1;
        }
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if ( err ) {
			errno = err;
			return -1;
		}
		else {
			return 0;
		}
	}
	else {
		return ret;
	}
	
}

int evco_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    return evco_timed_connect(fd, addr, addrlen, 0);
}

int evco_timed_accept(int fd, int msec)
{
	int clt_fd = 0;
_ACCEPT_START:
	clt_fd = accept(fd, NULL, NULL);
	if ( clt_fd > 0 ) {
		evutil_make_socket_nonblocking(clt_fd);
		return clt_fd;
	}
#ifdef WIN32 
	if ( WSAEINTR == WSAGetLastError() || WSAEWOULDBLOCK == WSAGetLastError() ) {
#else
	if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
#endif
		evco_debug("accept EAGAIN, will swap.\n");
		__evco_yield_by_fd(fd, 0, msec);
        if ( g_current_pco->flag_iotimeout ) {
            errno = ETIMEDOUT;
            return -1;
        }
		goto _ACCEPT_START;	
	}
	else {
		return -1;
	}
}

int evco_accept(int fd)
{
    return evco_timed_accept(fd, 0);
}

int evco_timed_recv(int fd, char *buffer, size_t size, int msec) 
{
	int ret = 0;
_RECV_START:
	ret = recv(fd, buffer, size, 0);
	if ( ret >= 0 ) {
		return ret;
	}
#ifdef WIN32 
	if ( WSAEINTR == WSAGetLastError() || WSAEWOULDBLOCK == WSAGetLastError() ) {
#else
	if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
#endif
		evco_debug("recv EAGAIN, will swap.\n");
		__evco_yield_by_fd(fd, 0, msec);
        if ( g_current_pco->flag_iotimeout ) {
            errno = ETIMEDOUT;
            return -1;
        }
		goto _RECV_START;
	}
	else {
		return -1;
	}
}

int evco_recv(int fd, char *buffer, size_t size) 
{
    return evco_timed_recv(fd, buffer, size, 0);
}


int evco_timed_send(int fd, char *buffer, size_t size, int msec)
{
	int ret = 0;
_SEND_START:
	ret = send(fd, buffer, size, 0);
	if ( ret >= 0 ) {
		return ret;
	}
#ifdef WIN32 
	if ( WSAEINTR == WSAGetLastError() || WSAEWOULDBLOCK == WSAGetLastError() ) {
#else
	if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
#endif
		evco_debug("send EAGAIN, will swap.\n");
		__evco_yield_by_fd(fd, 1, 0);
        if ( g_current_pco->flag_iotimeout ) {
            errno = ETIMEDOUT;
            return -1;
        }
		goto _SEND_START;
	}
	else {
		return -1;
	}
}

int evco_send(int fd, char *buffer, size_t size) 
{
    return evco_timed_send(fd, buffer, size, 0);
}

void __fd_item_free(fd_item_t *pitem)
{
	event_del(pitem->ev_recv);
	event_del(pitem->ev_send);
	event_free(pitem->ev_recv);
	event_free(pitem->ev_send);
	FREE_POINTER(pitem);
}

int evco_close(int fd)
{
	fd_item_t *pitem = hashtab_query(g_current_pco->psc->fd_tb, fd);
	evco_debug("closing fd %d.\n", fd);
#ifdef WIN32
	closesocket(fd);
#else
	close(fd);
#endif
	if ( pitem != NULL ) {
		if ( pitem->evco_recv ) {
			__evco_resume(pitem->evco_recv);
		}
		if ( pitem->evco_send ) {
			__evco_resume(pitem->evco_send);
		}
	}
    __fd_item_free(pitem);
	hashtab_delete(g_current_pco->psc->fd_tb, fd, NULL);
	return 0;
}

evsc_t *evco_get_sc()
{
	return g_current_pco->psc;
}

evco_t *evco_get_co()
{
	return g_current_pco;
}

int evco_dispatch(evsc_t *psc)
{
	return event_base_dispatch(psc->ev_base);
}

evco_cond_t *evco_cond_alloc()
{
	evco_cond_t *pcond = (evco_cond_t *)calloc(1, sizeof(evco_cond_t));
	INIT_LIST_HEAD(&pcond->wait_pcos);
	return pcond;
}

int evco_cond_timedwait(evco_cond_t *pcond, int msec)
{
	cond_item_t item;
	item.entry.prev = NULL;
	item.entry.next = NULL;
	item.pco = g_current_pco;
	item.timeout_msec = msec;
	if ( msec > 0 ) {
		struct timeval tv;
		if ( g_current_pco->timer == NULL ) {
			g_current_pco->timer = evtimer_new(g_current_pco->psc->ev_base, __evsc_timer_dispatch, g_current_pco);
		}
		msec2tv(msec, tv);
		event_add(g_current_pco->timer, &tv);
	}

	list_add_tail(&item.entry, &pcond->wait_pcos);
	g_current_pco->flag_iotimeout = 0;
	__evco_yield();
	list_del(&item.entry);
	if ( g_current_pco->flag_iotimeout == 1 ) {
		g_current_pco->flag_iotimeout = 0;
		return ETIMEDOUT;
	}
	else {
        event_del(g_current_pco->timer);
		return 0;
	}
}

int evco_cond_signal(evco_cond_t *pcond)
{
	cond_item_t *pitem = NULL;
	if ( list_empty(&pcond->wait_pcos)) {
		return 0;
	}
	pitem = list_entry(pcond->wait_pcos.next, cond_item_t, entry);
	list_del(&pitem->entry);
	if ( pitem->timeout_msec > 0 ) {
		event_del(pitem->pco->timer);
	}
	list_add_tail(&pitem->entry, &g_current_pco->psc->cond_ready_queue);
	return 0;
}

int evco_cond_broadcast(evco_cond_t *pcond)
{
	cond_item_t *pitem = NULL;
	while ( 1 ) {
		if ( list_empty(&pcond->wait_pcos)) {
			return 0;
		}
		pitem = list_entry(pcond->wait_pcos.next, cond_item_t, entry);
		list_del(&pitem->entry);
		event_del(pitem->pco->timer);
		list_add_tail(&pitem->entry, &g_current_pco->psc->cond_ready_queue);
	}
	return 0;
}

int evco_cond_free(evco_cond_t *pcond)
{
	evco_cond_broadcast(pcond);
	FREE_POINTER(pcond);
	return 0;
}

evsc_t *evsc_alloc()
{
	evsc_t *psc;
	assert(g_current_pco == NULL);
	psc = (evsc_t *)malloc(sizeof(evsc_t));
	if (psc != NULL) {
		psc->ev_base = event_base_new();
		psc->fd_tb = hashtab_alloc();
		INIT_LIST_HEAD(&psc->cond_ready_queue);
		psc->root = __evco_create_main(psc);
		if (psc->ev_base == NULL || psc->fd_tb == NULL || psc->root == NULL) {
			goto _E1;
		}
		g_current_pco = psc->root;
	}
	return psc;
_E1:
	event_base_free(psc->ev_base);
	if (psc->root != NULL) {
		__evco_free(psc->root);	
	}
	if (psc->fd_tb != NULL) {
		hashtab_free(psc->fd_tb, free);
	}
	free(psc);
	return NULL;

}
