#include <linux/module.h>
#include <linux/i2c.h>

#include "epd_therm.h"


int epd_therm_get_temp(struct i2c_client *client){
	(void)client;
	return 45000;
}
