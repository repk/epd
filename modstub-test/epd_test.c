#include <stdlib.h>
#include <stdio.h>

#include <linux/module.h>
#include <linux/spi/spi.h>

#include "../epd_mod.h"

struct epd_platform_data pdata = {
	.type = EPD_TYPE_2_7,
	.gpio_panel_on = 1,
	.gpio_reset = 2,
	.gpio_border = 3,
	.gpio_busy = 4,
	.gpio_discharge = 5,
};

struct spi_device spi_dev = {
	.dev = {
		.platform_data = &pdata,
	}
};

int main(void)
{
	_spi_driver->probe(&spi_dev);
	_spi_driver->remove(&spi_dev);
	return 0;
}
