#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include "epd.h"

#ifdef DEBUG
#define DBG(...) printk("epd: "__VA_ARGS__)
#else
#define DBG(...)
#endif

#define ERR(...) pr_err("epd: "__VA_ARGS__)

#define DRIVER_NAME "epd-ctl"
#define DRIVER_DESC "Epaper display controller driver"

struct epd {
	struct device *dev;
	struct epd_driver *drv;
	struct epd_frame *fold;
	struct epd_frame *fnew;
	struct mutex lock;
	unsigned int id;
};
#define EPD_DEVT(e) MKDEV(epd_major, e->id + 1)

#define EPD_CTL_CLEAR 'C'
#define EPD_CTL_BLACK 'B'
#define EPD_CTL_WRITE 'W'

#define EPD_MAX_DEVICES 15

static int epd_major;
static struct cdev epd_cdev;
static struct class *epddev_class;
/* TODO support a list of epddev for multiple screen */
static DEFINE_MUTEX(epddev_lock);
static struct epd *epddev;

/* TODO support a list of epddev for multiple screen */
static int epd_device_add(struct epd *epd)
{
	int ret = 0;

	mutex_lock(&epddev_lock);
	if(epddev != NULL) {
		ERR("Too much screen\n");
		ret = -ENODEV;
		goto out;
	}
	epddev = epd;
out:
	mutex_unlock(&epddev_lock);
	return ret;
}

static struct epd *epd_device_get(unsigned int id)
{
	struct epd *epd = ERR_PTR(-ENXIO);

	mutex_lock(&epddev_lock);
	if(epddev != NULL && epddev->id == id)
		epd = epddev;
	mutex_unlock(&epddev_lock);
	return epd;
}

static void epd_device_remove(struct epd *epd)
{
	mutex_lock(&epddev_lock);
	if(epddev == epd)
		epddev = NULL;
	mutex_unlock(&epddev_lock);
}

static void epd_frame_cleanup(struct epd_frame *frame)
{
	if(frame)
		kfree(frame);
}

static struct epd_frame *epd_frame_create(size_t line, size_t col)
{
	struct epd_frame *f;
	unsigned int bytes_per_line = DIV_ROUND_UP(col, 8);

	f = kmalloc(sizeof(*f) + line * bytes_per_line, GFP_KERNEL);
	if(f == NULL)
		return NULL;

	f->nrline = line;
	f->nrdot = col;
	f->bytes_per_line = bytes_per_line;

	return f;
}

static void epd_frame_black(struct epd_frame *frame)
{
	size_t i, j;

	for(i = 0; i < frame->nrline; ++i)
		for(j = 0; j < frame->bytes_per_line; ++j)
			frame->data[i * frame->bytes_per_line + j] = 0xff;
}

static void epd_frame_white(struct epd_frame *frame)
{
	size_t i, j;

	for(i = 0; i < frame->nrline; ++i)
		for(j = 0; j < frame->bytes_per_line; ++j)
			frame->data[i * frame->bytes_per_line + j] = 0x00;
}

static void epd_destroy(struct epd *epd)
{
	if(epd->dev) {
		epd_device_remove(epd);
		device_destroy(epddev_class, EPD_DEVT(epd));
	}
	if(epd->fold)
		epd_frame_cleanup(epd->fold);
	if(epd->fnew)
		epd_frame_cleanup(epd->fnew);
	kfree(epd);
}

struct epd_frame *epd_get_cur_fb(struct epd *epd)
{
	return epd->fold;
}
EXPORT_SYMBOL(epd_get_cur_fb);

struct epd_frame *epd_get_alt_fb(struct epd *epd)
{
	return epd->fnew;
}
EXPORT_SYMBOL(epd_get_alt_fb);

/* TODO kobject/kref ref counter */
void epd_put(struct epd *epd)
{
	epd_destroy(epd);
}
EXPORT_SYMBOL(epd_put);

struct epd *epd_create(struct device *dev, struct epd_driver *drv)
{
	struct epd *epd;
	struct device *edev;
	struct epd_frame_size const *framesz;
	int err;

	/* TODO: use devmanagement devm_kzalloc() */
	epd = kzalloc(sizeof(*epd), GFP_KERNEL);
	if(epd == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	framesz = drv->framesz;

	epd->fold = epd_frame_create(framesz->line, framesz->col);
	if(epd->fold == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	epd->fnew = epd_frame_create(framesz->line, framesz->col);
	if(epd->fnew == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	epd_frame_black(epd->fold);
	epd_frame_white(epd->fnew);

	/* TODO Get dynamic id here */
	epd->id = 0;
	mutex_init(&epd->lock);

	err = epd_device_add(epd);
	if(err < 0)
		goto fail;

	edev = device_create(epddev_class, dev, EPD_DEVT(epd), epd,
			"epd%u", epd->id);
	err = PTR_ERR_OR_ZERO(edev);
	if(err < 0)
		goto fail;
	epd->dev = edev;
	epd->drv = drv;

	return epd;

fail:
	epd_put(epd);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(epd_create);

static void epd_update_frame(struct epd *epd)
{
	memcpy(epd->fold->data, epd->fnew->data,
			epd->fold->nrline * epd->fold->bytes_per_line);
}

static int epd_draw_frame(struct epd *epd)
{
	struct epd_driver *drv = epd->drv;
	int ret = 0;

	if(drv->ops.draw_frame != NULL)
		ret = drv->ops.draw_frame(drv);

	/*
	 * XXX Bad perf :-(.
	 * Switch fold and fnew would cause read to not get current fb data
	 */
	epd_update_frame(epd);
	return ret;
}

static ssize_t epd_fb_read(struct file *f, char __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	size_t bufsz, sz;
	ssize_t ret = 0;

	epd = f->private_data;
	bufsz = epd->fnew->nrline * epd->fnew->bytes_per_line;
	if(*off > (loff_t)bufsz)
		goto out;

	sz = min_t(size_t, len, bufsz - *off);
	mutex_lock(&epd->lock);
	ret = copy_to_user(buf, epd->fnew->data + *off, sz);
	mutex_unlock(&epd->lock);
	if(ret < 0)
		goto out;
	ret = sz - ret;
	*off += ret;
out:
	return ret;
}

static ssize_t epd_fb_write(struct file *f, char const __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	size_t bufsz;
	long missing = 0;
	int ret = 0;

	epd = f->private_data;
	bufsz = epd->fnew->nrline * epd->fnew->bytes_per_line;

	if(len + *off > bufsz) {
		ret = -EMSGSIZE;
		goto out;
	}

	mutex_lock(&epd->lock);
	missing = copy_from_user(epd->fnew->data + *off, buf, len);
	if(missing != 0) {
		ret = -EFAULT;
		goto unlock;
	}
	*off += len;
	ret = len;
unlock:
	mutex_unlock(&epd->lock);
out:
	return ret;
}

static int epd_fb_open(struct inode *i, struct file *f)
{
	struct epd *epd;
	int ret;

	/*
	 * If realease cannot be call here we are safe, otherway we have to be
	 * sure epddev is not being destroy here.
	 * TODO Maybe use a refcounter in epd, held by epd_device_get() release
	 * by epd_device_put()
	 */
	epd = epd_device_get(iminor(i) - 1);
	ret = PTR_ERR_OR_ZERO(epd);
	if(ret < 0)
		goto out;
	f->private_data = epd;
out:
	return ret;
}

static int epd_fb_release(struct inode *i, struct file *f)
{
	f->private_data = NULL;
	return 0;
}

static struct file_operations const epd_fb_ops = {
	.owner = THIS_MODULE,
	.read = epd_fb_read,
	.write = epd_fb_write,
	.open = epd_fb_open,
	.release = epd_fb_release,
	.llseek = default_llseek,
};

static ssize_t epd_ctl_write(struct file *f, char const __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	char *msg;
	int ret = -EINVAL;
	unsigned int eid;
	u8 cmd;

	if(len < 2)
		goto out;

	msg = kmalloc((len + 1) * sizeof(*msg), GFP_KERNEL);
	if(msg == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if(copy_from_user(msg, buf, len)) {
		ret = -EFAULT;
		kfree(msg);
		goto out;
	}
	msg[len] = '\0';

	/* Get cmd and epd id */
	ret = sscanf(msg, "%c%u", &cmd, &eid);
	kfree(msg);
	if(ret != 2) {
		ret = -EINVAL;
		goto out;
	}

	epd = epd_device_get(eid);
	ret = PTR_ERR_OR_ZERO(epd);
	if(ret < 0)
		goto out;

	ret = len;
	mutex_lock(&epd->lock);
	switch(cmd) {
	case 'C':
		epd_frame_white(epd->fnew);
		epd_draw_frame(epd);
		break;
	case 'B':
		epd_frame_black(epd->fnew);
		epd_draw_frame(epd);
		break;
	case 'W':
		epd_draw_frame(epd);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&epd->lock);
out:
	return ret;
}

static struct file_operations const epd_ctl_ops = {
	.owner = THIS_MODULE,
	.write = epd_ctl_write,
	.open = nonseekable_open,
	.llseek = no_llseek,
};

static int epd_open(struct inode *i, struct file *f)
{
	struct file_operations const *fops = &epd_ctl_ops;
	int minor = iminor(i);
	int ret = 0;

	/* Mux fops: minor = 0 -> controller / minor != 0 -> framebuffer */
	if(minor != 0)
		fops = &epd_fb_ops;

	replace_fops(f, fops_get(fops));
	if(f->f_op->open)
		ret = f->f_op->open(i, f);
	return ret;
}

/* File operations mux between controller and framebuffers */
static struct file_operations const epd_ops = {
	.owner = THIS_MODULE,
	.open = epd_open,
	.llseek = noop_llseek,
};

static int __init epd_init(void)
{
	struct device *dev;
	dev_t dev_id;
	int ret;

	DBG("Init driver\n");

	/* Alloc 1 char for each screen and one for mux controller */
	ret = alloc_chrdev_region(&dev_id, 0, EPD_MAX_DEVICES + 1, "epd");
	if(ret < 0) {
		ERR("Cannot alloc char dev major number\n");
		goto err;
	}
	epd_major = MAJOR(dev_id);

	epddev_class = class_create(THIS_MODULE, "epd");
	if(IS_ERR(epddev_class)) {
		ERR("Cannot create driver class\n");
		goto err_chrdev;
	}

	cdev_init(&epd_cdev, &epd_ops);
	ret = cdev_add(&epd_cdev, dev_id, EPD_MAX_DEVICES + 1);
	if(ret < 0) {
		ERR("Cannot add char dev\n");
		goto err_class;
	}

	dev = device_create(epddev_class, NULL, dev_id, NULL, "epdctl");
	ret = PTR_ERR_OR_ZERO(dev);
	if(ret < 0)
		goto err_cdev;

	pr_info("epd: epaper display controller driver\n");

	return 0;

err_cdev:
	cdev_del(&epd_cdev);
err_class:
	class_destroy(epddev_class);
err_chrdev:
	unregister_chrdev_region(dev_id, EPD_MAX_DEVICES + 1);
err:
	return ret;
}

module_init(epd_init);

static void __exit epd_exit(void)
{
	dev_t dev_id;

	DBG("Cleanup driver\n");

	dev_id = MKDEV(epd_major, 0);

	device_destroy(epddev_class, dev_id);
	cdev_del(&epd_cdev);
	class_destroy(epddev_class);
	unregister_chrdev_region(dev_id, EPD_MAX_DEVICES + 1);
}

module_exit(epd_exit);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
