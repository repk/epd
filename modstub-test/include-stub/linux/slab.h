#ifndef _LINUX_STUB_SLAB_H_
#define _LINUX_STUB_SLAB_H_

#include <stdlib.h>

#define kmalloc(s, f) malloc(s)
#define kzalloc(s, f) calloc(1, s)
#define kfree(p) free((void *)p)

#endif
