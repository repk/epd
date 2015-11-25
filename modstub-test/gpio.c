#include <linux/module.h>
#include <linux/gpio.h>

#define GPIO_MAX 256

static int gpio_val[GPIO_MAX] = {};


int gpio_get_value(unsigned int gpio)
{
	if(gpio >= GPIO_MAX) {
		printk("Invalid GPIO\n");
		return 0;
	}

	printf("Get GPIO Value %u with %d\n", gpio, gpio_val[gpio]);
	return gpio_val[gpio];
}

void gpio_set_value(unsigned int gpio, int value)
{
	if(gpio >= GPIO_MAX)
		printk("Invalid GPIO\n");

	printf("Set GPIO Value %u with %d\n", gpio, value);
	gpio_val[gpio] = value;
}
