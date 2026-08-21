/* xthread.h */
#ifndef XND_XTHREAD_H
#define XND_XTHREAD_H

#include <pthread.h>
#include <string.h>
#include "compiler.h"

#ifdef xnd_panic
# define xthread_die(fmt, ...) xnd_panic(fmt, ##__VA_ARGS__)
#else
# include <stdlib.h>
# define xthread_die(fmt, ...)			     \
	do {					     \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
		exit(-1);			     \
	} while (0)
#endif

#define xthread(op, ...)						\
	do {								\
		int __err = CONCAT(pthread_, op)(__VA_ARGS__);		\
		if (unlikely(__err != 0))				\
			xthread_die("%s: %s\n",				\
				    TOSTRING(op), strerror(__err));	\
	} while (0)

#define xpthread_create(thread, attr, start_routine, arg) \
	xthread(create, thread, attr, start_routine, arg)
#define xpthread_join(thread, value_ptr) \
	xthread(join, thread, value_ptr)

#define xpthread_mutex_init(mutex, attr) \
	xthread(mutex_init, mutex, attr)
#define xpthread_mutex_destroy(mutex) \
	xthread(mutex_destroy, mutex)
#define xpthread_mutex_lock(mutex) \
	xthread(mutex_lock, mutex)
#define xpthread_mutex_unlock(mutex) \
	xthread(mutex_unlock, mutex)

#define xpthread_cond_init(cond, attr) \
	xthread(cond_init, cond, attr)
#define xpthread_cond_destroy(cond) \
	xthread(cond_destroy, cond)
#define xpthread_cond_wait(cond, mutex) \
	xthread(cond_wait, cond, mutex)
#define xpthread_cond_signal(cond) \
	xthread(cond_signal, cond)
#define xpthread_cond_broadcast(cond) \
	xthread(cond_broadcast, cond)

#define xpthread_attr_init(attr) \
	xthread(attr_init, attr)
#define xpthread_attr_destroy(attr) \
	xthread(attr_destroy, attr)
#define xpthread_attr_setstack(attr, stackaddr, stacksize) \
	xthread(attr_setstack, attr, stackaddr, stacksize)
#define xpthread_attr_getstack(attr, stackaddr, stacksize) \
	xthread(attr_getstack, attr, stackaddr, stacksize)
#define xpthread_attr_setstacksize(attr, stacksize) \
	xthread(attr_setstacksize, attr, stacksize)
#define xpthread_attr_getstacksize(attr, stacksize) \
	xthread(attr_getstacksize, attr, stacksize)
#define xpthread_attr_setstackaddr(attr, stackaddr) \
	xthread(attr_setstackaddr, attr, stackaddr)
#define xpthread_attr_getstackaddr(addr, stackaddr) \
	xthread(attr_getstackaddr, attr, stackaddr)
#define xpthread_attr_setguardsize(attr, guardsize) \
	xthread(attr_setguardsize, attr, guardsize)
#define xpthread_attr_getguardsize(attr, guardsize) \
	xthread(attr_setguardsize, attr, guardsize)

#define xpthread_kill(thread, sig) \
	xthread(kill, thread, sig)
#define xpthread_sigmask(how, set, oset) \
	xthread(sigmask, how, set, oset)

#endif /* XND_THREAD_H */
