#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cdev.h>

struct chrdev {
	struct list_head next;
	dev_t dev;
	unsigned count;
};

static LIST_HEAD(chrdevlst);
static LIST_HEAD(cdevlst);
static LIST_HEAD(oflst);
static int major;
static int ofnb;

int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
	char const *name)
{
	struct chrdev *d;
	(void)name;

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if(d == NULL)
		return -ENOMEM;

	++major;
	*dev = MKDEV(major, baseminor);

	d->dev = *dev;
	d->count = count;
	INIT_LIST_HEAD(&d->next);
	list_add_tail(&d->next, &chrdevlst);
	return 0;
}

void unregister_chrdev_region(dev_t from, unsigned count)
{
	struct chrdev *d = NULL;

	list_for_each_entry(d, &chrdevlst, next) {
		if(d->dev == from && d->count == count)
			break;
	}
	if(d->dev == from && d->count == count) {
		list_del(&d->next);
		kfree(d);
	}
}

void cdev_init(struct cdev *cdev, struct file_operations const *fops)
{
	cdev->ops = fops;
}

int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	(void)p;
	(void)count;

	p->dev = dev;
	list_add_tail(&p->next, &cdevlst);
	return 0;
}

void cdev_del(struct cdev *p)
{
	list_del(&p->next);
	(void)p;
}

static struct cdev *cdev_find(dev_t dev)
{
	struct cdev *cdev;

	list_for_each_entry(cdev, &cdevlst, next) {
		if(cdev->dev == dev)
			break;
	}

	if(cdev->dev != dev)
		return NULL;
	return cdev;
}

static struct file *cdev_find_file(int fd)
{
	struct file *f;

	list_for_each_entry(f, &oflst, next) {
		if(f->fd == fd)
			break;
	}

	if(f->fd != fd)
		return NULL;
	return f;
}

int cdev_open(struct inode *i)
{
	struct cdev *cdev;
	struct file *f;
	int ret;

	cdev = cdev_find(i->i_rdev);
	if(cdev == NULL)
		return -ENODEV;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if(f == NULL)
		return -ENOMEM;

	f->f_op = (void *)cdev->ops;
	f->fd = ofnb;
	++ofnb;
	INIT_LIST_HEAD(&f->next);

	ret = f->f_op->open(i, f);
	if(ret < 0) {
		kfree(f);
		return ret;
	}

	list_add_tail(&f->next, &oflst);

	return f->fd;
}

int cdev_write(int fd, char const *buf, size_t len, loff_t *off)
{
	struct file *f;

	f = cdev_find_file(fd);
	if(f == NULL)
		return -ENODEV;

	return f->f_op->write(f, buf, len, off);
}

int cdev_read(int fd, char *buf, size_t len, loff_t *off)
{
	struct file *f;

	f = cdev_find_file(fd);
	if(f == NULL)
		return -ENODEV;

	return f->f_op->read(f, buf, len, off);
}

void cdev_close(int fd)
{
	struct file *f;

	f = cdev_find_file(fd);
	if(f == NULL)
		return;

	list_del(&f->next);
	kfree(f);
}
