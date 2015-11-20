#ifndef _EPD_MOD_H_
#define _EPD_MOD_H_

struct epd_platform_data {
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};

#endif
