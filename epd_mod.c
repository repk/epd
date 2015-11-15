#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>

#ifdef DEBUG
#define DBG(...) printk("epd: "__VA_ARGS__)
#else
#define DBG(...)
#endif

#define LM75_ADDR 0x49

struct epd {
	struct spi_device *spi;
	struct i2c_client *therm;
};

static int setup_thermal(struct epd *epd)
{
	struct i2c_adapter *adapt;
	struct i2c_board_info info = {
		.type = "epd-therm",
		.addr = LM75_ADDR,
	};

	adapt = i2c_get_adapter(0);
	if(adapt == NULL) {
		DBG("Cannot get i2c adapter\n");
		return -ENODEV;
	}

	epd->therm = i2c_new_device(adapt, &info);
	if(epd->therm == NULL) {
		DBG("Cannot create i2c new device\n");
		i2c_put_adapter(adapt);
		return -ENODEV;
	}

	return 0;
}

static void cleanup_thermal(struct epd *epd)
{
	if(epd->therm) {
		i2c_unregister_device(epd->therm);
		i2c_put_adapter(epd->therm->adapter);
	}
}

static int epd_probe(struct spi_device *spi)
{
	struct epd *epd;
	int ret = 0;

	DBG("Call epd_probe()\n");

	/**
	 * TODO: use devmanagement devm_kzalloc()
	 */
	epd = kzalloc(sizeof(*epd), GFP_KERNEL);
	if(epd == NULL)
		return -ENOMEM;

	ret = spi_setup(spi);
	if(ret < 0)
		goto fail;

	epd->spi = spi;
	spi_set_drvdata(spi, epd);

	ret = setup_thermal(epd);
	if(ret < 0)
		goto fail;

	return 0;

fail:
	kfree(epd);
	return ret;
}

static int epd_remove(struct spi_device *spi)
{
	struct epd *epd = spi_get_drvdata(spi);
	DBG("Call epd_remove()\n");

	cleanup_thermal(epd);
	if(epd)
		kfree(epd);

	return 0;
}

/**
 * TODO support pm suspend/resume
 */
static struct spi_driver epd_driver = {
	.driver = {
		.name = "epd",
		.owner = THIS_MODULE,
	},
	.probe = epd_probe,
	.remove = epd_remove,
};

module_spi_driver(epd_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("EM027AS012 based epaper display driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("spi:epd");
