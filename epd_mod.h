#ifndef _EPD_MOD_H_
#define _EPD_MOD_H_

enum epd_type {
	EPD_TYPE_1_44,
	EPD_TYPE_2,
	EPD_TYPE_2_7,
	EPD_TYPE_MAX = EPD_TYPE_2_7,
};

struct epd_platform_data {
	enum epd_type type;
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};

#endif
