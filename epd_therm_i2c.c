#include <linux/module.h>
#include <linux/i2c.h>

#ifdef DEBUG
#define DBG(...) pr_err("epd_therm: "__VA_ARGS__)
#else
#define DBG(...)
#endif

static int epdt_probe(struct i2c_client *client,
	struct i2c_device_id const *id)
{
	DBG("Call epd_therm_probe()\n");
	return 0;
}

static int epdt_remove(struct i2c_client *client)
{
	DBG("Call epd_therm_remove()\n");
	return 0;
}

static struct i2c_device_id const epd_therm_id[] = {
	{"epd-therm", 0},
	{ },
};

static struct i2c_driver epd_therm_driver = {
	.driver = {
		.name = "epd-therm",
		.owner = THIS_MODULE,
	},
	.probe = epdt_probe,
	.remove = epdt_remove,
	.id_table = epd_therm_id,
};

module_i2c_driver(epd_therm_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("EM027AS012 based epaper display driver for internal thermal sensor");
MODULE_LICENSE("Dual BSD/GPL");
