#ifndef _LINUX_STUB_SPI_H_
#define _LINUX_STUB_SPI_H_

#include <linux/init.h>

#define SPI_NAME_SIZE 32

struct spi_device {
	struct device		dev;
	struct list_head next;
};

struct spi_driver {
	int			(*probe)(struct spi_device *spi);
	int			(*remove)(struct spi_device *spi);
	void			(*shutdown)(struct spi_device *spi);
	struct device_driver	driver;
};

struct spi_transfer {
	/* it's ok if tx_buf == rx_buf (right?)
	 * for MicroWire, one buffer must be null
	 * buffers must work with dma_*map_single() calls, unless
	 *   spi_message.is_dma_mapped reports a pre-existing mapping
	 */
	const void	*tx_buf;
	void		*rx_buf;
	unsigned	len;

	unsigned	cs_change:1;
	unsigned	tx_nbits:3;
	unsigned	rx_nbits:3;
#define	SPI_NBITS_SINGLE	0x01 /* 1bit transfer */
#define	SPI_NBITS_DUAL		0x02 /* 2bits transfer */
#define	SPI_NBITS_QUAD		0x04 /* 4bits transfer */
	u8		bits_per_word;
	u16		delay_usecs;
	u32		speed_hz;
};

struct spi_board_info {
	void const *platform_data;
	char modalias[SPI_NAME_SIZE];
};

int spi_sync_transfer(struct spi_device *spi, struct spi_transfer *xfers,
		unsigned int num_xfers);

static inline void spi_set_drvdata(struct spi_device *spi, void *data)
{
	dev_set_drvdata(&spi->dev, data);
}

static inline void *spi_get_drvdata(struct spi_device *spi)
{
	return dev_get_drvdata(&spi->dev);
}

int spi_setup(struct spi_device *spi);

int spi_register_driver(struct spi_driver *drv);
void spi_unregister_driver(struct spi_driver *drv);

int spi_register_board_info(struct spi_board_info *info, size_t nb);

#define module_spi_driver(drv)						\
	static int __ ## drv ## _init(void)				\
	{								\
		return spi_register_driver(&drv);			\
	}								\
	module_init(__ ## drv ## _init);				\
	static void __ ## drv ## _exit(void)				\
	{								\
		spi_unregister_driver(&drv);				\
	}								\
	module_exit(__ ## drv ## _exit);

#endif
