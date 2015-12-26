#ifndef _LINUX_STUB_MODULE_H_
#define _LINUX_STUB_MODULE_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/export.h>
#include <linux/init.h>

#include <linux/misc.h>

#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / b)

#define MODULE_AUTHOR(author)
#define MODULE_DESCRIPTION(desc)
#define MODULE_LICENSE(license)
#define MODULE_ALIAS(alias)

#endif
