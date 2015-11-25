#include <linux/module.h>
#include <linux/spi/spi.h>

int spi_setup(struct spi_device *spi)
{
	(void)spi;
	return 0;
}

int spi_sync_transfer(struct spi_device *spi, struct spi_transfer *xfers,
		unsigned int num_xfers)
{
	unsigned int i, j;

	(void)spi;

	printk("SPI TRANSFER BEGIN \n");
	for(i = 0; i < num_xfers; ++i) {
		for(j = 0; j < xfers[i].len; ++j)
			printk("0x%02x ", ((u8 *)xfers[i].tx_buf)[j]);

		if(xfers[i].cs_change)
			printk(" -- ");
	}
	printk("\nSPI TRANSFER END \n");

	return 0;
}
