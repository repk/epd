#ifndef _LINUX_STUB_KERNEL_H_
#define _LINUX_STUB_KERNEL_H_

#include <linux/compiler.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#endif
