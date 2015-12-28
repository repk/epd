#ifndef _LINUX_STUB_COMPILER_H_
#define _LINUX_STUB_COMPILER_H_

# ifndef likely
#  define likely(x)	(x)
# endif
# ifndef unlikely
#  define unlikely(x)	(x)
# endif

#define __must_be_array(a) 0

#define WRITE_ONCE(x, val) (x = val)
#define READ_ONCE(x) (x)
#define __user
#define typeof __typeof__

#endif
