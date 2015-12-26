#ifndef _LINUX_STUB_MUTEX_H_
#define _LINUX_STUB_MUTEX_H_

#include <sched.h>

struct mutex {
	char locked;
};

#define DEFINE_MUTEX(m) struct mutex m = {.locked = 0}

static inline void mutex_init(struct mutex *lock)
{
	lock->locked = 0;
}

static inline void mutex_lock(struct mutex *lock)
{
	/* XXX Not thread safe, but ok for stub */
	while(lock->locked)
		sched_yield();
	lock->locked = 1;
}

static inline void mutex_unlock(struct mutex *lock)
{
	/* XXX Not thread safe, but ok for stub */
	lock->locked = 0;
}

#endif
