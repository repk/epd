#ifndef _LINUX_STUB_CDEV_H_
#define _LINUX_STUB_CDEV_H_

#include <linux/fs.h>
#include <linux/kdev_t.h>

struct cdev {
	struct file_operations const *ops;
	struct list_head next;
	dev_t dev;
	unsigned count;
};

void cdev_init(struct cdev *cdev, struct file_operations const *fops);
int cdev_add(struct cdev *p, dev_t dev, unsigned count);
void cdev_del(struct cdev *p);
int cdev_open(struct inode *i);
int cdev_write(int fd, char const *buf, size_t len, loff_t *off);
int cdev_read(int fd, char *buf, size_t len, loff_t *off);
void cdev_close(int fd);

#endif
