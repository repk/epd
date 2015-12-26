#ifndef _LINUX_STUB_FS_H_
#define _LINUX_STUB_FS_H_

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <unistd.h>
#include <errno.h>

struct file;

struct inode {
	dev_t i_rdev;
};

struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, char const __user *, size_t, loff_t *);
	int (*open)(struct inode *, struct file *);
	int (*release)(struct inode *, struct file *);
};

struct file {
	struct file_operations *f_op;
	void *private_data;
	struct list_head next;
	int fd;
};

static inline loff_t default_llseek(struct file *file, loff_t off, int whence)
{
	(void)file;
	(void)off;
	(void)whence;
	return 0;
}

static inline loff_t noop_llseek(struct file *file, loff_t off, int whence)
{
	(void)file;
	(void)off;
	(void)whence;
	return 0;
}

static inline loff_t no_llseek(struct file *file, loff_t off, int whence)
{
	(void)file;
	(void)off;
	(void)whence;
	return -ENOSYS;
}

static inline int nonseekable_open(struct inode *i, struct file *f)
{
	(void)i;
	(void)f;
	return 0;
}

static inline unsigned iminor(struct inode const *inode)
{
        return MINOR(inode->i_rdev);
}

static inline unsigned imajor(struct inode const *inode)
{
        return MAJOR(inode->i_rdev);
}

#define replace_fops(f, fops) do {					\
		struct file *file = (f);				\
		file->f_op = (void *)(fops);				\
} while(0)

#define fops_get(fops) (fops)

int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
		char const *name);
void unregister_chrdev_region(dev_t from, unsigned count);

#endif
