#ifndef _LINUX_STUB_UACCESS_H_
#define _LINUX_STUB_UACCESS_H_

#include <linux/compiler.h>

static inline int copy_to_user(void __user *to, void const *from, long n)
{
	memcpy(to, from, n);
	return 0;
}

static inline int copy_from_user(void *to, void const __user *from, long n)
{
	memcpy(to, from, n);
	return 0;
}

#endif
