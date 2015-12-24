#ifndef _EPD_G1_H_
#define _EPD_G1_H_

enum g1_screen_type {
	G1_TYPE_1_44,
	G1_TYPE_2,
	G1_TYPE_2_7,
	G1_TYPE_MAX = G1_TYPE_2_7,
};

struct g1_platform_data {
	enum g1_screen_type type;
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};

#endif
