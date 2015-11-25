#ifndef _LINUX_STUB_MISC_H_
#define _LINUX_STUB_MISC_H_

#include <sched.h>

#define cpu_relax sched_yield

#endif
