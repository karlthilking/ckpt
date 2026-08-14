/* xthread.h */
#ifndef XND_XTHREAD_H
#define XND_XTHREAD_H

#include <pthread.h>
#include <string.h>
#include "compiler.h"

#define xthread(op, ...)					  \
	do {							  \
		int __err = CONCAT(pthread_, op)(__VA_ARGS__);	  \
		if (unlikely(__err != 0))			  \
			xnd_panic("%s error: %s\n",		  \
				  #op, strerror(__err)); \
	} while (0)

#define xpthread_create(t, attr, fn, arg) xthread(create, t, attr, fn, arg)
#define xpthread_join(t, ptr) xthread(join, t, ptr)

#define xpthread_mutex_init(mtx, attr) xthread(mutex_init, mtx, attr)
#define xpthread_mutex_destroy(mtx) xthread(mutex_destroy, mtx)
#define xpthread_mutex_lock(mtx) xthread(mutex_lock, mtx)
#define xpthread_mutex_unlock(mtx) xthread(mutex_unlock, mtx)

#define xpthread_cond_init(cond, attr) xthread(cond_init, cond, attr)
#define xpthread_cond_destroy(cond) xthread(cond_destroy, cond)
#define xpthread_cond_wait(cond, mtx) xthread(cond_wait, cond, mtx)
#define xpthread_cond_signal(cond) xthread(cond_signal, cond)
#define xpthread_cond_broadcast(cond) xthread(cond_broadcast, cond)

#define xpthread_sigmask(how, set, oset) xthread(sigmask, how, set, oset)

#endif /* XND_THREAD_H */
