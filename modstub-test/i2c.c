#include <linux/module.h>
#include <linux/i2c.h>

#define I2C_ADAPTER_NR 256

struct i2c_adapter __i2c_adapter[I2C_ADAPTER_NR] = {};

struct i2c_client __i2c_client;

struct i2c_adapter *i2c_get_adapter(int nr)
{
	if(nr > I2C_ADAPTER_NR)
		return NULL;

	return &__i2c_adapter[nr];
}

void i2c_put_adapter(struct i2c_adapter *adap)
{
	(void)adap;
}

struct i2c_client * i2c_new_device(struct i2c_adapter *adap,
		struct i2c_board_info const *info)
{
	(void)adap;
	printk("Get i2c device %s\n", info->type);
	return &__i2c_client;
}

void i2c_unregister_device(struct i2c_client *clt)
{
	(void)clt;
	printk("Release i2c device\n");
}
