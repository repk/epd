#ifndef _LINUX_STUB_GPIO_H_
#define _LINUX_STUB_GPIO_H_

#include <linux/stddef.h>

static inline bool gpio_is_valid(int number)
{
	return true;
}

static inline int gpio_direction_output(unsigned int gpio, int value)
{
	(void)gpio;
	(void)value;
	return 0;
}

static inline int gpio_direction_input(unsigned int gpio)
{
	(void)gpio;
	return 0;
}

int gpio_get_value(unsigned int gpio);
void gpio_set_value(unsigned int gpio, int value);

#endif
