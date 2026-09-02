/* tls.c */
#include <pthread.h>
#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "wrappers/pthread_wrappers.h"

static uintptr_t _pthread_ptr_munge_token;

int
thread_ptr_munge_save(void)
{
	long sig;
	uintptr_t self, tsd_self, munge, tsd_munge;

	self = (uintptr_t)pthread_self();
	tsd_self = *tsd_slot_access(uintptr_t, __TSD_THREAD_SELF);
	if (self != tsd_self) {
		xnd_warn("pthread_self != __TSD_THREAD_SELF\n");
		return -1;
	}

	sig = *(long *)self;
	munge = self ^ sig;
	tsd_munge = *tsd_slot_access(uintptr_t, __TSD_PTR_MUNGE);
	if (munge != tsd_munge) {
		xnd_warn("_pthread_ptr_munge_token != __TSD_PTR_MUNGE\n");
		return -1;
	}

	_pthread_ptr_munge_token = munge;
	return 0;
}

void
thread_ptr_munge_fixup(void)
{
	long sig;
	uintptr_t self, munge, *addr;

	self = *tsd_slot_access(uintptr_t, __TSD_THREAD_SELF);
	sig = *(long *)self;

	if ((sig ^ _pthread_ptr_munge_token) != self) {
		sig = self ^ _pthread_ptr_munge_token;
		*(long *)self = sig;
	}

	addr = tsd_slot_access(uintptr_t, __TSD_PTR_MUNGE);
	munge = *addr;
	if (munge != _pthread_ptr_munge_token)
		*addr = _pthread_ptr_munge_token;
}

void
xnd_tlv_init(void)
{
	extern struct thread_info *thread_self_or_null(void);

	*tsd_slot_access(u64, __TSD_XND_FLAG) = 0ull;
	barrier();

	(void)thread_self_or_null();

	barrier();
	*tsd_slot_access(u64, __TSD_XND_FLAG) = __TSD_XND_INIT;
}

void
xnd_tlv_fini(void)
{
	*tsd_slot_access(u64, __TSD_XND_FLAG) = 0ull;
}

static inline bool
check_tsd_pthread_struct_offset(void)
{
	uintptr_t tsd, self;

	tsd = self_tsd_base();
	self = (uintptr_t)pthread_self();

	if (tsd + TSD_PTHREAD_OFFSET != self) {
		xnd_error("tsd pthread struct offset check failed\n");
		return false;
	}

	return true;
}

static inline bool
check_tsd_threadid_offset(void)
{
	u64 tid, kerntid;

	tid = self_get_threadid();
	kerntid = __thread_selfid();
	if (tid != kerntid) {
		xnd_error("threadid offset check failed:\n"
			  " found tid: %llu, actual tid: %llu\n",
			  tid, kerntid);
		return false;
	}

	return true;
}

bool
validate_tsd_relative_offsets(void)
{
	return (check_tsd_pthread_struct_offset() &&
		check_tsd_threadid_offset());
}
