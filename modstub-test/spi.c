#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/list.h>
#include <linux/of_device.h>

int spi_setup(struct spi_device *spi)
{
	(void)spi;
	return 0;
}

int spi_sync_transfer(struct spi_device *spi, struct spi_transfer *xfers,
		unsigned int num_xfers)
{
	unsigned int i, j;

	(void)spi;

	printk("SPI TRANSFER BEGIN \n");
	for(i = 0; i < num_xfers; ++i) {
		for(j = 0; j < xfers[i].len; ++j)
			printk("0x%02x ", ((u8 *)xfers[i].tx_buf)[j]);

		if(xfers[i].cs_change)
			printk(" -- ");
	}
	printk("\nSPI TRANSFER END \n");

	return 0;
}

struct spidev {
	struct spi_driver *drv;
	struct list_head next;
	struct list_head spidev;
};

static LIST_HEAD(spilst);

static struct spidev *spi_find_info(struct spi_board_info *info)
{
	struct spidev *res = NULL;
	char const *compat;
	struct of_device_id const *of_devid;
	int found = 0;

	list_for_each_entry(res, &spilst, next) {
		compat = strchr(res->drv->driver.name, ',');
		if(compat == NULL)
			compat = res->drv->driver.name;
		else
			++compat;
		if(strcmp(info->modalias, compat) == 0) {
			found = 1;
			break;
		}
		for(of_devid = res->drv->driver.of_match_table;
				of_devid != NULL && of_devid->compatible != NULL;
				++of_devid) {
			compat = strchr(of_devid->compatible, ',');
			if(compat == NULL)
				compat = of_devid->compatible;
			else
				++compat;
			if(strcmp(info->modalias, compat) == 0) {
				found = 1;
				break;
			}
		}
	}

	if(found == 0)
		return NULL;

	return res;
}

static struct spidev *spi_find_drv(struct spi_driver *drv)
{
	struct spidev *res = NULL;

	list_for_each_entry(res, &spilst, next) {
		if(res->drv == drv)
			break;
	}

	if(res == NULL || res->drv != drv)
		return NULL;

	return res;
}

int spi_register_driver(struct spi_driver *drv)
{
	struct spidev *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if(dev == NULL)
		return -ENOMEM;

	dev->drv = drv;
	INIT_LIST_HEAD(&dev->next);
	INIT_LIST_HEAD(&dev->spidev);
	list_add_tail(&dev->next, &spilst);
	return 0;
}

void spi_unregister_driver(struct spi_driver *drv)
{
	struct spidev *dev;
	struct spi_device *d;

	dev = spi_find_drv(drv);
	if(dev == NULL)
		return;

	while(!list_empty(&dev->spidev)) {
		d = list_first_entry(&dev->spidev, struct spi_device, next);
		list_del(&d->next);
		dev->drv->remove(d);
		kfree(d);
	}
	list_del(&dev->next);
	kfree(dev);
}

int spi_register_board_info(struct spi_board_info *info, size_t nb)
{
	struct spidev *dev;
	struct spi_device *d;
	size_t i;
	int ret = 0;

	for(i = 0; i < nb; ++i) {
		dev = spi_find_info(info);
		if(dev == NULL) {
			ret = -ENODEV;
			break;
		}

		d = kmalloc(sizeof(*d), GFP_KERNEL);
		if(d == NULL) {
			ret = -ENOMEM;
			break;
		}

		INIT_LIST_HEAD(&d->next);
		d->dev.platform_data = (void *)info->platform_data;
		list_add_tail(&d->next, &dev->spidev);
		ret = dev->drv->probe(d);
		if(ret != 0)
			break;
	}

	return ret;
}

