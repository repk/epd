#ifndef _LINUX_STUB_I2C_H_
#define _LINUX_STUB_I2C_H_

#define I2C_NAME_SIZE   20

struct i2c_adapter {
};

struct i2c_client {
	unsigned short flags;		/* div., see below		*/
	unsigned short addr;		/* chip address - NOTE: 7bit	*/
					/* addresses are stored in the	*/
					/* _LOWER_ 7 bits		*/
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;	/* the adapter we sit on	*/
	struct device dev;		/* the device structure		*/
};

struct i2c_board_info {
	char		type[I2C_NAME_SIZE];
	unsigned short	flags;
	unsigned short	addr;
	void		*platform_data;
};

struct i2c_adapter *i2c_get_adapter(int nr);
void i2c_put_adapter(struct i2c_adapter *adap);

struct i2c_client * i2c_new_device(struct i2c_adapter *adap,
		struct i2c_board_info const *info);

void i2c_unregister_device(struct i2c_client *);

#endif
