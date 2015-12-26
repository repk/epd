#ifndef _LINUX_STUB_DELAY_H_
#define _LINUX_STUB_DELAY_H_

#include <unistd.h>

#define mdelay(n) usleep(n * 1000)

#endif
