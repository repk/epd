#ifndef _LINUX_STUB_INIT_H_
#define _LINUX_STUB_INIT_H_

#define __init
#define __exit

#define FUNC_MAGIC ((void *)0x12121212)

struct module_initcall {
	char const *name;
	int (*f)(void);
};

struct module_exitcall {
	char const *name;
	void (*f)(void);
};

#define module_init(fn)							\
	static struct module_initcall __initcall_##fn = {		\
		.name = #fn,						\
		.f = fn,						\
	};								\
	struct module_initcall *__initcall_##fn_magic			\
		__attribute__((__section__(".initcall"))) = FUNC_MAGIC;	\
	struct module_initcall *__initcall_##fn_ptr			\
		__attribute__((__section__(".initcall"))) = & __initcall_##fn


#define module_exit(fn)							\
	static struct module_exitcall __exitcall_##fn = {		\
		.name = #fn,						\
		.f = fn,						\
	};								\
	struct module_exitcall *__exitcall_##fn_magic			\
		__attribute__((__section__(".exitcall"))) = FUNC_MAGIC;	\
	struct module_exitcall *__exitcall_##fn_ptr			\
		__attribute__((__section__(".exitcall"))) = & __exitcall_##fn

int devices_init(void);
int devices_exit(void);


#endif
