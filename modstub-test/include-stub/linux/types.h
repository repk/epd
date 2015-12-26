#ifndef _LINUX_STUB_TYPES_H_
#define _LINUX_STUB_TYPES_H_

#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

struct list_head {
	struct list_head *next, *prev;
};

#endif
