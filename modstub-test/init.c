/*
 * XXX This file should be link before all drv-* files
 */

#include <linux/init.h>
#include <linux/module.h>

extern struct module_initcall *__initcall_start;
extern struct module_initcall *__initcall_end;

int devices_init(void)
{
	struct module_initcall **__initcall_tab = &__initcall_start;
	struct module_initcall *initcall;
	size_t len = &__initcall_end - &__initcall_start;
	size_t i;
	int ret;

	for(i = 0; i < len; ++i) {
		initcall = __initcall_tab[i];
		printk("Run %s()\n", initcall->name);
		ret = initcall->f();
		if(ret < 0)
			printk("\t%s fail with %d\n",
					initcall->name, ret);
	}

	return 0;
}

extern struct module_exitcall *__exitcall_start;
extern struct module_exitcall *__exitcall_end;

int devices_exit(void)
{
	struct module_exitcall **__exitcall_tab = &__exitcall_start;
	struct module_exitcall *exitcall;
	size_t len = &__exitcall_end - &__exitcall_start;
	size_t i;

	for(i = 0; i < len; ++i) {
		exitcall = __exitcall_tab[i];
		printk("Run %s()\n", exitcall->name);
		exitcall->f();
	}

	return 0;
}
