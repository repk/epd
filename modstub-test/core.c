#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <stdarg.h>

static LIST_HEAD(devlst);

struct device *device_create_vargs(struct class *class, struct device *parent,
		dev_t devt, void *drvdata, char const *fmt, va_list args)
{
	struct device *dev;
	char *name;
	size_t len;

	(void)class;
	(void)parent;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if(dev == NULL)
		return ERR_PTR(-ENOMEM);

	name = kmalloc(1024 , GFP_KERNEL);
	if(name == NULL) {
		kfree(dev);
		return ERR_PTR(-ENOMEM);
	}

	vsnprintf(name, 1024, fmt, args);

	dev->devt = devt;
	dev->name = name;
	INIT_LIST_HEAD(&dev->next);
	dev_set_drvdata(dev, drvdata);

	list_add_tail(&dev->next, &devlst);

	return dev;
}

struct device *device_create(struct class *class, struct device *parent,
		dev_t devt, void *drvdata, char const *fmt, ...)
{
	struct device *dev;
	va_list vargs;

	va_start(vargs, fmt);
	dev = device_create_vargs(class, parent, devt, drvdata, fmt, vargs);
	va_end(vargs);

	return dev;
}

void device_destroy(struct class *class, dev_t devt)
{
	struct device *dev = NULL;

	list_for_each_entry(dev, &devlst, next) {
		if(dev->devt == devt)
			break;
	}

	if(dev && dev->devt == devt) {
		list_del(&dev->next);
		kfree(dev->name);
		kfree(dev);
	}
}
