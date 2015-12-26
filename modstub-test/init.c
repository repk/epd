/*
 * XXX This file should be link before all drv-* files
 */

#include <linux/init.h>
#include <linux/module.h>

int devices_init(void);
int devices_exit(void);

static struct module_initcall __minit = {
	.name = "devices_init",
	.f = (void *)devices_init,
};

static struct module_exitcall __mexit = {
	.name = "devices_exit",
	.f = (void *)devices_exit,
};

struct module_initcall *__initcall_start
	__attribute__((__section__(".initcall"))) = &__minit;

int devices_init(void)
{
	struct module_initcall **__initcall_tab = &__initcall_start;
	struct module_initcall *initcall;
	int ret;

	if(__initcall_tab[0]->f != devices_init) {
		printk("module_init tab corrupt\n");
		return -EINVAL;
	}

	++__initcall_tab;

	while(*__initcall_tab == FUNC_MAGIC) {
		++__initcall_tab;
		initcall = __initcall_tab[0];
		printk("Run %s()\n", initcall->name);
		ret = initcall->f();
		if(ret < 0)
			printk("\t%s fail with %d\n",
					initcall->name, ret);
		++__initcall_tab;
	}

	return 0;
}

struct module_exitcall *__exitcall_start
	__attribute__((__section__(".exitcall"))) = &__mexit;

int devices_exit(void)
{
	struct module_exitcall **__exitcall_tab = &__exitcall_start;
	struct module_exitcall *exitcall;

	if(__exitcall_tab[0]->f != (void *)devices_exit) {
		printk("module_exit tab corrupt\n");
		return -EINVAL;
	}

	++__exitcall_tab;

	while(*__exitcall_tab == FUNC_MAGIC) {
		++__exitcall_tab;
		exitcall = __exitcall_tab[0];
		printk("Run %s()\n", exitcall->name);
		exitcall->f();
		++__exitcall_tab;
	}

	return 0;
}
