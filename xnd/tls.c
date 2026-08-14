/* tls.c */
#include <pthread.h>
#include "xnd/xnd.h"
#include "xnd/tls.h"

static uintptr_t _pthread_ptr_munge_token;

int thread_ptr_munge_save(void)
{
	long sig;
	uintptr_t tls, self, tsd_self, munge, tsd_munge;

	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");

	self = (uintptr_t)pthread_self();
	tsd_self = get_tls_slot(__TSD_THREAD_SELF);
	if (self != tsd_self) {
		xnd_warn("pthread_self != __TSD_THREAD_SELF\n");
		return -1;
	}

	sig = *(long *)self;
	munge = self ^ sig;
	tsd_munge = get_tls_slot(__TSD_PTR_MUNGE);
	if (munge != tsd_munge) {
		xnd_warn("_pthread_ptr_munge_token != __TSD_PTR_MUNGE\n");
		return -1;
	}

	_pthread_ptr_munge_token = munge;
	return 0;
}

void thread_ptr_munge_fixup(void)
{
	long sig;
	uintptr_t tls, self, munge;

	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
	self = get_tls_slot(__TSD_THREAD_SELF);
	sig = *(long *)self;

	if ((sig ^ _pthread_ptr_munge_token) != self) {
		sig = self ^ _pthread_ptr_munge_token;
		*(long *)self = sig;
	}

	munge = get_tls_slot(__TSD_PTR_MUNGE);
	if (munge != _pthread_ptr_munge_token)
		set_tls_slot(__TSD_PTR_MUNGE, _pthread_ptr_munge_token);
}
