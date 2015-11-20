#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "epd_mod.h"
#include "epd_therm.h"

#ifdef DEBUG
#define DBG(...) printk("epd: "__VA_ARGS__)
#else
#define DBG(...)
#endif

#define ERR(...) pr_err("epd: "__VA_ARGS__)

#define LM75_ADDR 0x49
#define PWM_PERIOD 5000 /* 200 KHz */
#define PWM_DUTY_PERCENT 50
#define PWM_DUTY (PWM_PERIOD * PWM_DUTY_PERCENT / 100)

struct epd {
	struct spi_device *spi;
	struct i2c_client *therm;
	struct pwm_device *pwm;
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};

static void epd_destroy(struct epd *epd)
{
	if(epd)
		kfree(epd);
}

static int epd_prepare_gpios(struct epd *epd)
{
	int ret = -EINVAL;

	if(!gpio_is_valid(epd->gpio_panel_on))
		goto out;
	gpio_direction_output(epd->gpio_panel_on, 1);

	if(!gpio_is_valid(epd->gpio_reset))
		goto out;
	gpio_direction_output(epd->gpio_reset, 1);

	if(!gpio_is_valid(epd->gpio_border))
		goto out;
	gpio_direction_output(epd->gpio_border, 1);

	if(!gpio_is_valid(epd->gpio_busy))
		goto out;
	gpio_direction_input(epd->gpio_busy);

	if(!gpio_is_valid(epd->gpio_discharge))
		goto out;
	gpio_direction_output(epd->gpio_discharge, 1);

	ret = 0;
out:
	return ret;
}

static struct epd *epd_create(struct epd_platform_data *pdata)
{
	struct epd *epd;
	int err;

	/**
	 * TODO: use devmanagement devm_kzalloc()
	 */
	epd = kzalloc(sizeof(*epd), GFP_KERNEL);
	if(epd == NULL)
		goto out;

	epd->gpio_panel_on = pdata->gpio_panel_on;
	epd->gpio_reset = pdata->gpio_reset;
	epd->gpio_border = pdata->gpio_border;
	epd->gpio_busy = pdata->gpio_busy;
	epd->gpio_discharge = pdata->gpio_discharge;

	err = epd_prepare_gpios(epd);
	if(err < 0) {
		kfree(epd);
		return NULL;
	}

out:
	return epd;
}

static int init_pwm(struct epd *epd)
{
	int err;
	/**
	 * TODO use platform data to get pwm channel id for pwm_request
	 * (see max8997_haptic.c)
	 * TODO use devm_pwm_get()
	 */
	epd->pwm = pwm_get(&epd->spi->dev, NULL);
	if(IS_ERR(epd->pwm)) {
		err = PTR_ERR(epd->pwm);
		ERR("Cannot get pwm %d\n", err);
		goto err;
	}

	err = pwm_config(epd->pwm, PWM_DUTY, PWM_PERIOD);
	if(err < 0) {
		ERR("Cannot configure pwm %d\n", err);
		goto freepwm;
	}

	return 0;

freepwm:
	pwm_free(epd->pwm);
err:
	epd->pwm = NULL;
	return err;
}

static int cleanup_pwm(struct epd *epd)
{
	if(epd->pwm == NULL)
		return 0;

	pwm_disable(epd->pwm);

	pwm_free(epd->pwm);
	epd->pwm = NULL;
	return 0;
}

#define SPI_REG_HEADER 0x70
#define SPI_DATA_HEADER 0x72

/* Register indexes */
#define SPI_REGIDX_CHANSEL	0x01
#define SPI_REGIDX_OUTPUT	0x02
#define SPI_REGIDX_LATCH	0x03
#define SPI_REGIDX_GATE_SRC_LVL	0x04
#define SPI_REGIDX_CHARGEPUMP	0x05
#define SPI_REGIDX_DCFREQ	0x06
#define SPI_REGIDX_OSC		0x07
#define SPI_REGIDX_ADC		0x08
#define SPI_REGIDX_VCOM		0x09
#define SPI_REGIDX_DATA		0x0a

/* Register data */
#define SPI_REGDATA_INIT(n, ...)					\
	static char const __spi_regdata_ ## n[] = {__VA_ARGS__}

#define SPI_REGDATA(n) __spi_regdata_ ## n
#define SPI_REGDATA_SZ(n) ARRAY_SIZE(__spi_regdata_ ## n)

SPI_REGDATA_INIT(CHAN_1_44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x00);
SPI_REGDATA_INIT(CHAN_2, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xe0, 0x00);
SPI_REGDATA_INIT(CHAN_2_7, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfe, 0x00, 0x00);
SPI_REGDATA_INIT(OUTPUT_OFF, 0x05);
SPI_REGDATA_INIT(OUTPUT_DISABLE, 0x24);
SPI_REGDATA_INIT(OUTPUT_ENABLE, 0x2f);
SPI_REGDATA_INIT(LATCH_OFF, 0x00);
SPI_REGDATA_INIT(LATCH_ON, 0x01);
SPI_REGDATA_INIT(GATE_SRC_LVL_1_44, 0x03);
SPI_REGDATA_INIT(GATE_SRC_LVL_2, 0x03);
SPI_REGDATA_INIT(GATE_SRC_LVL_2_7, 0x00);
SPI_REGDATA_INIT(GATE_SRC_LVL_DISCHARGE_0, 0x00);
SPI_REGDATA_INIT(GATE_SRC_LVL_DISCHARGE_1, 0x0c);
SPI_REGDATA_INIT(GATE_SRC_LVL_DISCHARGE_2, 0x50);
SPI_REGDATA_INIT(GATE_SRC_LVL_DISCHARGE_3, 0xa0);
SPI_REGDATA_INIT(CHARGEPUMP_VPOS_OFF, 0x00);
SPI_REGDATA_INIT(CHARGEPUMP_VPOS_ON, 0x01);
SPI_REGDATA_INIT(CHARGEPUMP_VNEG_OFF, 0x02);
SPI_REGDATA_INIT(CHARGEPUMP_VNEG_ON, 0x03);
SPI_REGDATA_INIT(CHARGEPUMP_VCOM_ON, 0x0f);
SPI_REGDATA_INIT(CHARGEPUMP_VCOM_OFF, 0x0e);
SPI_REGDATA_INIT(DCFREQ, 0xff);
SPI_REGDATA_INIT(OSC_ON, 0x9d);
SPI_REGDATA_INIT(OSC_OFF, 0x0d);
SPI_REGDATA_INIT(ADC_DISABLE, 0x00);
SPI_REGDATA_INIT(VCOM_LVL, 0xD0, 0x00);

enum spi_cmd_id {
	SPI_CMD_CHANSEL_1_44,
	SPI_CMD_CHANSEL_2,
	SPI_CMD_CHANSEL_2_7,
	SPI_CMD_OUTPUT_OFF,
	SPI_CMD_OUTPUT_DISABLE,
	SPI_CMD_OUTPUT_ENABLE,
	SPI_CMD_LATCH_OFF,
	SPI_CMD_LATCH_ON,
	SPI_CMD_GATE_SRC_LVL_1_44,
	SPI_CMD_GATE_SRC_LVL_2,
	SPI_CMD_GATE_SRC_LVL_2_7,
	SPI_CMD_GATE_SRC_LVL_DISCHARGE_0,
	SPI_CMD_GATE_SRC_LVL_DISCHARGE_1,
	SPI_CMD_GATE_SRC_LVL_DISCHARGE_2,
	SPI_CMD_GATE_SRC_LVL_DISCHARGE_3,
	SPI_CMD_CHARGEPUMP_VPOS_ON,
	SPI_CMD_CHARGEPUMP_VPOS_OFF,
	SPI_CMD_CHARGEPUMP_VNEG_ON,
	SPI_CMD_CHARGEPUMP_VNEG_OFF,
	SPI_CMD_CHARGEPUMP_VCOM_ON,
	SPI_CMD_CHARGEPUMP_VCOM_OFF,
	SPI_CMD_DCFREQ,
	SPI_CMD_OSC_ON,
	SPI_CMD_OSC_OFF,
	SPI_CMD_ADC_DISABLE,
	SPI_CMD_VCOM_LVL,
	SPI_CMD_NR,
};

struct spi_cmd {
	char const *regdata;
	size_t regdata_sz;
	char regid;
};

#define SPI_CMD_ENTRY(e, rid, rdata)					\
	[e] = {								\
		.regdata = SPI_REGDATA(rdata),				\
		.regdata_sz = SPI_REGDATA_SZ(rdata),			\
		.regid = rid,						\
	}

static struct spi_cmd const __spi_cmd[] = {
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_1_44, SPI_REGIDX_CHANSEL, CHAN_1_44),
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_2, SPI_REGIDX_CHANSEL, CHAN_2),
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_2_7, SPI_REGIDX_CHANSEL, CHAN_2_7),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_OFF, SPI_REGIDX_OUTPUT, OUTPUT_OFF),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_DISABLE, SPI_REGIDX_OUTPUT,
			OUTPUT_DISABLE),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_ENABLE, SPI_REGIDX_OUTPUT, OUTPUT_ENABLE),
	SPI_CMD_ENTRY(SPI_CMD_LATCH_OFF, SPI_REGIDX_LATCH, LATCH_OFF),
	SPI_CMD_ENTRY(SPI_CMD_LATCH_ON, SPI_REGIDX_LATCH, LATCH_ON),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_1_44, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_1_44),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_2, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_2),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_2_7, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_2_7),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_0, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_DISCHARGE_0),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_1, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_DISCHARGE_1),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_2, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_DISCHARGE_2),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_3, SPI_REGIDX_GATE_SRC_LVL,
			GATE_SRC_LVL_DISCHARGE_3),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VPOS_ON, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VPOS_ON),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VPOS_OFF, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VPOS_OFF),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VNEG_ON, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VNEG_ON),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VNEG_OFF, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VNEG_OFF),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VCOM_ON, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VCOM_ON),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VCOM_OFF, SPI_REGIDX_CHARGEPUMP,
			CHARGEPUMP_VCOM_OFF),
	SPI_CMD_ENTRY(SPI_CMD_DCFREQ, SPI_REGIDX_DCFREQ, DCFREQ),
	SPI_CMD_ENTRY(SPI_CMD_OSC_ON, SPI_REGIDX_OSC, OSC_ON),
	SPI_CMD_ENTRY(SPI_CMD_OSC_OFF, SPI_REGIDX_OSC, OSC_OFF),
	SPI_CMD_ENTRY(SPI_CMD_ADC_DISABLE, SPI_REGIDX_ADC, ADC_DISABLE),
	SPI_CMD_ENTRY(SPI_CMD_VCOM_LVL, SPI_REGIDX_VCOM, VCOM_LVL),
};

static int __spi_send_cmd(struct spi_device *spi, u8 idx, char const *data,
		size_t len)
{
	static char _spi_reg_hdr[] = {SPI_REG_HEADER};
	static char _spi_data_hdr[] = {SPI_DATA_HEADER};
	struct spi_transfer tx[] = {
		{
			.tx_buf = _spi_reg_hdr,
			.len = 1,
		},
		{
			.tx_buf = &idx,
			.len = 1,
			.cs_change = 1,
		},
		{
			.tx_buf = _spi_data_hdr,
			.len = 1,
		},
		{
			.tx_buf = data,
			.len = len,
		},
	};

	return spi_sync_transfer(spi, tx, ARRAY_SIZE(tx));
}

static int spi_send_cmd(struct spi_device *spi, enum spi_cmd_id cid)
{
	struct spi_cmd const *c;

	if(cid >= SPI_CMD_NR)
		return -EINVAL;

	c = &__spi_cmd[cid];

	return __spi_send_cmd(spi, c->regid, c->regdata, c->regdata_sz);
}

static int spi_send_data(struct spi_device *spi, unsigned int gpio_busy,
		char const *data, size_t len)
{
	static char _spi_reg_hdr[] = {SPI_REG_HEADER};
	static char _spi_data_hdr[] = {SPI_DATA_HEADER};
	static char _spi_regidx_data[] = {SPI_REGIDX_DATA};
	struct spi_transfer tx[] = {
		{
			.tx_buf = _spi_reg_hdr,
			.len = 1,
		},
		{
			.tx_buf = _spi_regidx_data,
			.len = 1,
			.cs_change = 1,
		},
		{
			.tx_buf = _spi_data_hdr,
			.len = 1,
			.cs_change = 1,
		},
	};
	size_t i;
	int ret;

	ret = spi_sync_transfer(spi, tx, ARRAY_SIZE(tx));
	if(ret < 0)
		return ret;

	tx[0].len = 1;
	tx[0].cs_change = 1;

	for(i = 0; i < len; ++i) {
		tx[0].tx_buf = &data[i];
		if(i + 1 == len)
			tx[0].cs_change = 0;

		ret = spi_sync_transfer(spi, tx, 1);
		if(ret < 0)
			break;

		while(gpio_get_value(gpio_busy))
			cpu_relax();
	}

	return ret;
}

static int power_on(struct epd *epd)
{
	int ret;

	/* XXX Maybe reset all gpio here */

	ret = pwm_enable(epd->pwm);
	if(ret < 0) {
		goto out;
	}

	gpio_set_value(epd->gpio_panel_on, 1);
	mdelay(10);

	/* TODO /CS is already set to 1 */

	gpio_set_value(epd->gpio_border, 1);
	gpio_set_value(epd->gpio_reset, 1);
	mdelay(5);
	gpio_set_value(epd->gpio_reset, 0);
	mdelay(5);
	gpio_set_value(epd->gpio_reset, 1);
	mdelay(5);
out:
	return ret;
}

static int setup_thermal(struct epd *epd)
{
	struct i2c_adapter *adapt;
	struct i2c_board_info info = {
		.type = "epd-therm",
		.addr = LM75_ADDR,
	};

	adapt = i2c_get_adapter(0);
	if(adapt == NULL) {
		ERR("Cannot get i2c adapter\n");
		return -ENODEV;
	}

	epd->therm = i2c_new_device(adapt, &info);
	if(epd->therm == NULL) {
		ERR("Cannot create i2c new device\n");
		i2c_put_adapter(adapt);
		return -ENODEV;
	}

	return 0;
}

static void cleanup_thermal(struct epd *epd)
{
	if(epd->therm == NULL)
		return;

	i2c_unregister_device(epd->therm);
	i2c_put_adapter(epd->therm->adapter);
}

#ifdef CONFIG_OF
static const struct of_device_id epd_dt_ids[] = {
	{
		.compatible = "epd",
	},
	{},
};
MODULE_DEVICE_TABLE(of, epd_dt_ids);

static int probe_dt(struct device *dev, struct epd_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
	struct of_device_id const *match;
	int ret = 0;

	/*
	 * Check device tree node is ok
	 */
	if(node == NULL) {
		ERR("Device does not have associated device tree data\n");
		ret = -EINVAL;
		goto out;
	}
	match = of_match_device(epd_dt_ids, dev);
	if(match == NULL) {
		ERR("Unknown device model\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Get gpio for panel_on
	 */
	pdata->gpio_panel_on = of_get_named_gpio(node, "panel_on-gpios", 0);
	if(pdata->gpio_panel_on < 0) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Get gpio for /reset
	 */
	pdata->gpio_reset = of_get_named_gpio(node, "reset-gpios", 0);
	if(pdata->gpio_reset < 0) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Get gpio for border
	 */
	pdata->gpio_border = of_get_named_gpio(node, "border-gpios", 0);
	if(pdata->gpio_border < 0) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Get gpio for busy
	 */
	pdata->gpio_busy = of_get_named_gpio(node, "busy-gpios", 0);
	if(pdata->gpio_busy < 0) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Get gpio for External Discharge
	 */
	pdata->gpio_discharge = of_get_named_gpio(node, "discharge-gpios", 0);
	if(pdata->gpio_discharge < 0) {
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}
#else
static int probe_dt(struct device *dev, struct epd_platform_data *pdata)
{
	return -EINVAL;
}
#endif

static int epd_probe(struct spi_device *spi)
{
	struct epd *epd;
	struct epd_platform_data *pdata, data;
	int ret = 0;
	int temp;

	DBG("Call epd_probe()\n");

	/*
	 * Get platform data in order to get all gpios config for border,
	 * /reset, ....
	 * This can be fetched from dt
	 */
	pdata = dev_get_platdata(&spi->dev);
	if(!pdata) {
		pdata = &data;
		ret = probe_dt(&spi->dev, pdata);
		if(ret < 0) {
			ERR("Fail to get platform data\n");
			goto out;
		}
	}

	epd = epd_create(pdata);
	if(epd == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = spi_setup(spi);
	if(ret < 0) {
		ERR("Fail to setup spi\n");
		goto fail;
	}

	epd->spi = spi;
	spi_set_drvdata(spi, epd);

	ret = setup_thermal(epd);
	if(ret < 0)
		goto fail;

	ret = init_pwm(epd);
	if(ret < 0)
		goto fail;

	/**
	 * TODO Remove all below
	 */
	temp = epd_therm_get_temp(epd->therm);
	printk("Temp is %d\n", temp);

	return 0;

fail:
	cleanup_thermal(epd);
	epd_destroy(epd);
out:
	return ret;
}

static int epd_remove(struct spi_device *spi)
{
	struct epd *epd = spi_get_drvdata(spi);
	DBG("Call epd_remove()\n");

	cleanup_pwm(epd);
	cleanup_thermal(epd);
	epd_destroy(epd);

	return 0;
}

/**
 * TODO support pm suspend/resume
 */
static struct spi_driver epd_driver = {
	.driver = {
		.name = "epd",
		.of_match_table = of_match_ptr(epd_dt_ids),
		.owner = THIS_MODULE,
	},
	.probe = epd_probe,
	.remove = epd_remove,
};

module_spi_driver(epd_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("EM027AS012 based epaper display driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("spi:epd");
