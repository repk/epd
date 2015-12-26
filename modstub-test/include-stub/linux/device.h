#ifndef _LINUX_STUB_DEVICE_H_
#define _LINUX_STUB_DEVICE_H_

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kdev_t.h>

struct class {
	const char *name;
	struct module *owner;
};

struct device {
	struct list_head	next;
	void			*platform_data;
	void			*driver_data;
	char const *name;
	dev_t devt;
};

struct device_driver {
	const char		*name;

	struct module		*owner;
	const char		*mod_name;	/* used for built-in modules */

	const struct of_device_id	*of_match_table;

	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*resume) (struct device *dev);
};

static inline void *dev_get_platdata(const struct device *dev)
{
	return dev->platform_data;
}

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

struct device *device_create(struct class *class, struct device *parent,
		dev_t devt, void *drvdata, const char *fmt, ...);

void device_destroy(struct class *class, dev_t devt);

static inline struct class *class_create(struct module *owner,
		char const *name)
{
	struct class *c;

	c = kmalloc(sizeof(*c), GFP_KERNEL);
	c->name = name;
	c->owner = owner;

	return c;
}

static inline void class_destroy(struct class *c)
{
	kfree(c);
}

#endif
