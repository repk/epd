#ifndef _LINUX_STDDEF_H_
#define _LINUX_STDDEF_H_

enum {
	false = 0,
	true = 1,
};

#define offsetof(TYPE, MEMBER)  ((size_t)&((TYPE *)0)->MEMBER)

#endif
