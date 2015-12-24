#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "epd.h"
#include "epd_g1.h"
#include "epd_therm.h"

#ifdef DEBUG
#define DBG(...) printk("epd: "__VA_ARGS__)
#else
#define DBG(...)
#endif

#define ERR(...) pr_err("epd: "__VA_ARGS__)

#define DRIVER_NAME "prvdsp,g1-epd"
#define DRIVER_DESC "EM027AS012 based COG G1 epaper driver from Pervasive Display"

#define LM75_ADDR 0x49
#define PWM_PERIOD 5000 /* 200 KHz */
#define PWM_DUTY_PERCENT 50
#define PWM_DUTY (PWM_PERIOD * PWM_DUTY_PERCENT / 100)

#define G1_DOT_NRBIT 2
#define G1_DOT_PER_BYTE (8 / G1_DOT_NRBIT)
#define G1_DOT_B 3
#define G1_DOT_W 2
#define G1_DOT_N 1
#define G1_SCAN_NRBIT 2
#define G1_SCAN_PER_BYTE (8 / G1_SCAN_NRBIT)
#define G1_SCAN_OFF 0
#define G1_SCAN_ON 3
#define G1_DUMMY_LINE ((size_t)(-1))

struct g1 {
	struct epd *epd;
	struct spi_device *spi;
	struct i2c_client *therm;
	struct pwm_device *pwm;
	struct epd_driver drv;
	enum g1_screen_type type;
	unsigned long stage_time;
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};
#define g1_from_epd_drv(drv) (container_of(drv, struct g1, drv))

static struct epd_frame_size const g1_frame_info[] = {
	[G1_TYPE_1_44] = {
		.line = 96,
		.col = 128,
	},
	[G1_TYPE_2] = {
		.line = 96,
		.col = 200,
	},
	[G1_TYPE_2_7] = {
		.line = 176,
		.col = 264,
	},
};

enum g1_stage {
	G1_STAGE_COMPENSATE,
	G1_STAGE_WHITE,
	G1_STAGE_INVERSE,
	G1_STAGE_NORMAL,
	G1_STAGE_POWEROFF,
};

static void g1_compute_stage_time(struct g1 *g1)
{
	unsigned long stage_time = 0;
	int temp;

	switch(g1->type) {
	case G1_TYPE_1_44:
	case G1_TYPE_2:
		stage_time = 480;
		break;
	case G1_TYPE_2_7:
		stage_time = 630;
		break;
	}

	temp = epd_therm_get_temp(g1->therm);
	if(temp <= -10000)
		stage_time *= 170;
	else if(temp <= -5000)
		stage_time *= 120;
	else if(temp <= 5000)
		stage_time *= 80;
	else if(temp <= 10000)
		stage_time *= 40;
	else if(temp <= 15000)
		stage_time *= 30;
	else if(temp <= 20000)
		stage_time *= 20;
	else if(temp <= 40000)
		stage_time *= 10;
	else
		stage_time *= 7;

	g1->stage_time = DIV_ROUND_UP(stage_time, 10);
}

static int g1_init_pwm(struct g1 *g1)
{
	int err;
	/**
	 * TODO use platform data to get pwm channel id for pwm_request
	 * (see max8997_haptic.c)
	 * TODO use devm_pwm_get()
	 */
	g1->pwm = pwm_get(&g1->spi->dev, NULL);
	err = PTR_ERR_OR_ZERO(g1->pwm);
	if(err < 0) {
		ERR("Cannot get pwm %d\n", err);
		goto err;
	}

	err = pwm_config(g1->pwm, PWM_DUTY, PWM_PERIOD);
	if(err < 0) {
		ERR("Cannot configure pwm %d\n", err);
		goto freepwm;
	}

	return 0;

freepwm:
	pwm_free(g1->pwm);
err:
	g1->pwm = NULL;
	return err;
}

static int g1_cleanup_pwm(struct g1 *g1)
{
	if(g1->pwm == NULL)
		return 0;

	pwm_disable(g1->pwm);

	pwm_free(g1->pwm);
	g1->pwm = NULL;
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

/* Null terminated SPI command data string initialization */
#define SPI_CMD_ENTRY(e, rid, rdata)					\
	[e] = {								\
		.regdata = rdata,					\
		.regdata_sz = ARRAY_SIZE(rdata) - 1,			\
		.regid = rid,						\
	}

static struct spi_cmd const __spi_cmd[] = {
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_1_44, SPI_REGIDX_CHANSEL,
			"\x00\x00\x00\x00\x00\x0f\xff\x00"),
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_2, SPI_REGIDX_CHANSEL,
			"\x00\x00\x00\x00\x01\xff\xe0\x00"),
	SPI_CMD_ENTRY(SPI_CMD_CHANSEL_2_7, SPI_REGIDX_CHANSEL,
			"\x00\x00\x00\x7f\xff\xfe\x00\x00"),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_OFF, SPI_REGIDX_OUTPUT, "\x05"),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_DISABLE, SPI_REGIDX_OUTPUT, "\x24"),
	SPI_CMD_ENTRY(SPI_CMD_OUTPUT_ENABLE, SPI_REGIDX_OUTPUT, "\x2f"),
	SPI_CMD_ENTRY(SPI_CMD_LATCH_OFF, SPI_REGIDX_LATCH, "\x00"),
	SPI_CMD_ENTRY(SPI_CMD_LATCH_ON, SPI_REGIDX_LATCH, "\x01"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_1_44, SPI_REGIDX_GATE_SRC_LVL,
			"\x03"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_2, SPI_REGIDX_GATE_SRC_LVL,
			"\x03"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_2_7, SPI_REGIDX_GATE_SRC_LVL,
			"\x00"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_0, SPI_REGIDX_GATE_SRC_LVL,
			"\x00"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_1, SPI_REGIDX_GATE_SRC_LVL,
			"\x0c"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_2, SPI_REGIDX_GATE_SRC_LVL,
			"\x50"),
	SPI_CMD_ENTRY(SPI_CMD_GATE_SRC_LVL_DISCHARGE_3, SPI_REGIDX_GATE_SRC_LVL,
			"\xa0"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VPOS_ON, SPI_REGIDX_CHARGEPUMP,
			"\x01"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VPOS_OFF, SPI_REGIDX_CHARGEPUMP,
			"\x00"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VNEG_ON, SPI_REGIDX_CHARGEPUMP,
			"\x03"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VNEG_OFF, SPI_REGIDX_CHARGEPUMP,
			"\x02"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VCOM_ON, SPI_REGIDX_CHARGEPUMP,
			"\x0f"),
	SPI_CMD_ENTRY(SPI_CMD_CHARGEPUMP_VCOM_OFF, SPI_REGIDX_CHARGEPUMP,
			"\x0e"),
	SPI_CMD_ENTRY(SPI_CMD_DCFREQ, SPI_REGIDX_DCFREQ, "\xff"),
	SPI_CMD_ENTRY(SPI_CMD_OSC_ON, SPI_REGIDX_OSC, "\x9d"),
	SPI_CMD_ENTRY(SPI_CMD_OSC_OFF, SPI_REGIDX_OSC, "\x0d"),
	SPI_CMD_ENTRY(SPI_CMD_ADC_DISABLE, SPI_REGIDX_ADC, "\x00"),
	SPI_CMD_ENTRY(SPI_CMD_VCOM_LVL, SPI_REGIDX_VCOM, "\xd0\x00"),
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
		u8 const *data, size_t len)
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

#define G1_ODD_BYTE(dot) (dot)
#define G1_EVEN_BYTE(dot)						\
	(((((dot) >> 6) & 0x3) << 0) |					\
	 ((((dot) >> 4) & 0x3) << 2) |					\
	 ((((dot) >> 2) & 0x3) << 4) |					\
	 ((((dot) >> 0) & 0x3) << 6))

static int fill_line(struct epd_frame *frame, enum g1_stage stage,
		size_t line, u8 *data, size_t len)
{
	u8 *ptr, *end;
	size_t lbyte, dotnr, scannr, i;
	int ret = 0;
	u8 dot = 0;

	dotnr = frame->nrdot / G1_DOT_PER_BYTE;
	scannr = frame->nrline / G1_SCAN_PER_BYTE;
	lbyte = frame->bytes_per_line;

	/* Some length checking */
	if((len < dotnr + scannr) || (dotnr != 2 * lbyte)) {
		ret = -EINVAL;
		goto out;
	}

	ptr = data;
	end = data + len;

	/* odd dots (263, ..., 3, 1) */
	for(i = lbyte; i > 0; --i, ++ptr) {
		if(line != G1_DUMMY_LINE)
			dot = frame->data[line * lbyte + i - 1] & 0xaa;
		switch(stage) {
		case G1_STAGE_COMPENSATE:
			*ptr = G1_ODD_BYTE(~(dot >> 1));
			break;
		case G1_STAGE_WHITE:
			*ptr = G1_ODD_BYTE(dot ^ 0xaa);
			break;
		case G1_STAGE_INVERSE:
			*ptr = G1_ODD_BYTE(~dot);
			break;
		case G1_STAGE_NORMAL:
			*ptr = G1_ODD_BYTE((dot >> 1) | 0xaa);
			break;
		case G1_STAGE_POWEROFF:
			*ptr = 0x55;
			break;
		}
	}

	/* Scan line */
	for(i = 0; i < scannr; ++i, ++ptr) {
		if(i == line / G1_SCAN_PER_BYTE) {
			*ptr = 0xc0 >> (G1_SCAN_NRBIT *
					(line % G1_SCAN_PER_BYTE));
		} else {
			*ptr = G1_SCAN_OFF;
		}
	}

	/* even dots (0, 2, ..., 262) */
	for(i = 0; i < lbyte; ++i, ++ptr) {
		if(line != G1_DUMMY_LINE)
			dot = frame->data[line * lbyte + i] & 0x55;
		switch(stage) {
		case G1_STAGE_COMPENSATE:
			*ptr = G1_EVEN_BYTE(~dot);
			break;
		case G1_STAGE_WHITE:
			*ptr = G1_EVEN_BYTE((dot ^ 0x55) << 1);
			break;
		case G1_STAGE_INVERSE:
			*ptr = G1_EVEN_BYTE((dot + 0x55) ^ 0xaa);
			break;
		case G1_STAGE_NORMAL:
			*ptr = G1_EVEN_BYTE(dot | 0xaa);
			break;
		case G1_STAGE_POWEROFF:
			*ptr = 0x55;
			break;
		}
	}

	/* filler */
	while(ptr < end)
		*ptr++ = 0;

out:
	return ret;
}

static int g1_draw_line(struct g1 *g1, enum g1_stage stage, size_t line)
{
	struct epd_frame_size const *fsz;
	struct epd_frame *f;
	u8 *data = NULL;
	size_t dotnr, scannr;
	int ret, filler = 0;

	switch(g1->type) {
	case G1_TYPE_1_44:
		filler = 0;
		ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_1_44);
		if(ret < 0)
			goto out;
		break;
	case G1_TYPE_2:
		filler = 1;
		ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_2);
		if(ret < 0)
			goto out;
		break;
	case G1_TYPE_2_7:
		filler = 1;
		ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_2_7);
		if(ret < 0)
			goto out;
		break;
	}

	if(stage == G1_STAGE_COMPENSATE || stage == G1_STAGE_WHITE)
		f = epd_get_cur_fb(g1->epd);
	else
		f = epd_get_alt_fb(g1->epd);

	fsz = &g1_frame_info[g1->type];
	dotnr = fsz->col / G1_DOT_PER_BYTE;
	scannr = fsz->line / G1_SCAN_PER_BYTE;

	data = kmalloc(dotnr + scannr + filler, GFP_KERNEL);
	if(data == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = fill_line(f, stage, line, data, dotnr + scannr + filler);
	if(ret)
		goto out;

	ret = spi_send_data(g1->spi, g1->gpio_busy, data,
			dotnr + scannr + filler);
	if(ret)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_OUTPUT_ENABLE);

out:
	if(data)
		kfree(data);

	return ret;
}

static int g1_poweroff_stage(struct g1 *g1)
{
	size_t i;
	int ret = 0;

	for(i = 0; i < g1_frame_info[g1->type].line; ++i) {
		ret = g1_draw_line(g1, G1_STAGE_POWEROFF, i);
		if(ret < 0)
			goto out;
	}

	ret = g1_draw_line(g1, G1_STAGE_POWEROFF, G1_DUMMY_LINE);
out:
	return ret;
}

static int g1_draw_stage(struct g1 *g1, enum g1_stage stage)
{
	size_t i;
	int ret = 0;

	for(i = 0; i < g1_frame_info[g1->type].line; ++i) {
		ret = g1_draw_line(g1, stage, i);
		if(ret < 0)
			goto out;
	}
out:
	return ret;
}

static int g1_repeat_stage(struct g1 *g1, enum g1_stage stage)
{
	unsigned long timeout;
	int ret;

	timeout = jiffies + msecs_to_jiffies(g1->stage_time);
	do {
		ret = g1_draw_stage(g1, stage);
		if(ret < 0)
			goto out;
	} while(time_before(jiffies, timeout));

out:
	return ret;
}

static int g1_power_on(struct g1 *g1)
{
	int ret;

	/* XXX Maybe reset all gpio here */

	ret = pwm_enable(g1->pwm);
	if(ret < 0) {
		goto out;
	}
	gpio_set_value(g1->gpio_panel_on, 1);
	mdelay(10);

	/* TODO /CS is already set to 1 */

	gpio_set_value(g1->gpio_border, 1);
	gpio_set_value(g1->gpio_reset, 1);
	mdelay(5);
	gpio_set_value(g1->gpio_reset, 0);
	mdelay(5);
	gpio_set_value(g1->gpio_reset, 1);
	mdelay(5);
out:
	return ret;
}

static int g1_power_off(struct g1 *g1)
{
	int ret;

	ret = g1_poweroff_stage(g1);
	if(ret < 0)
		goto out;

	mdelay(25);
	gpio_set_value(g1->gpio_border, 0);
	mdelay(250);
	gpio_set_value(g1->gpio_border, 1);

	ret = spi_send_cmd(g1->spi, SPI_CMD_LATCH_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_OUTPUT_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VCOM_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VNEG_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_1);
	if(ret < 0)
		goto out;
	mdelay(120);

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VPOS_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_OSC_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_2);
	if(ret < 0)
		goto out;
	mdelay(40);

	ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_3);
	if(ret < 0)
		goto out;
	mdelay(40);

	ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_0);
	if(ret < 0)
		goto out;

	gpio_set_value(g1->gpio_border, 0);
	gpio_set_value(g1->gpio_reset, 0);
	gpio_set_value(g1->gpio_panel_on, 0);
	gpio_set_value(g1->gpio_discharge, 1);
	mdelay(150);
	gpio_set_value(g1->gpio_discharge, 0);
out:
	return ret;
}

static int g1_init_display(struct g1 *g1)
{
	int ret = 0;

	while(gpio_get_value(g1->gpio_busy))
		cpu_relax();

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHANSEL_2_7);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_DCFREQ);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_OSC_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_ADC_DISABLE);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_VCOM_LVL);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_GATE_SRC_LVL_2_7);
	if(ret < 0)
		goto out;
	mdelay(5);

	ret = spi_send_cmd(g1->spi, SPI_CMD_LATCH_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_LATCH_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VPOS_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	pwm_disable(g1->pwm);

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VNEG_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	ret = spi_send_cmd(g1->spi, SPI_CMD_CHARGEPUMP_VCOM_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	ret = spi_send_cmd(g1->spi, SPI_CMD_OUTPUT_DISABLE);
	if(ret < 0)
		goto out;
out:
	return ret;
}

static int g1_draw_frame(struct epd_driver *drv)
{
	struct g1 *g1 = g1_from_epd_drv(drv);
	int ret;

	DBG("Power on display\n");
	ret = g1_power_on(g1);
	if(ret < 0)
		goto out;

	DBG("Init display\n");
	ret = g1_init_display(g1);
	if(ret < 0)
		goto out;

	g1_compute_stage_time(g1);
	DBG("Stage time : %lu\n", g1->stage_time);

	DBG("Draw compensate stage\n");
	ret = g1_repeat_stage(g1, G1_STAGE_COMPENSATE);
	if(ret < 0)
		goto out;

	DBG("Draw white stage\n");
	ret = g1_repeat_stage(g1, G1_STAGE_WHITE);
	if(ret < 0)
		goto out;

	DBG("Draw inverse stage\n");
	ret = g1_repeat_stage(g1, G1_STAGE_INVERSE);
	if(ret < 0)
		goto out;

	DBG("Draw normal stage\n");
	ret = g1_repeat_stage(g1, G1_STAGE_NORMAL);
	if(ret < 0)
		goto out;

	DBG("Power off display\n");
	ret = g1_power_off(g1);
out:
	return ret;
}

static struct epd_driver const g1_drv = {
	.name = "g1-epd",
	.desc = DRIVER_DESC,
	.ops = {
		.draw_frame = g1_draw_frame,
	},
};

static int g1_setup_thermal(struct g1 *g1)
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

	g1->therm = i2c_new_device(adapt, &info);
	if(g1->therm == NULL) {
		ERR("Cannot create i2c new device\n");
		i2c_put_adapter(adapt);
		return -ENODEV;
	}

	return 0;
}

static void g1_cleanup_thermal(struct g1 *g1)
{
	if(g1->therm == NULL)
		return;

	i2c_unregister_device(g1->therm);
	i2c_put_adapter(g1->therm->adapter);
}

static int g1_prepare_gpios(struct g1 *g1)
{
	int ret = -EINVAL;

	if(!gpio_is_valid(g1->gpio_panel_on))
		goto out;
	gpio_direction_output(g1->gpio_panel_on, 0);

	if(!gpio_is_valid(g1->gpio_reset))
		goto out;
	gpio_direction_output(g1->gpio_reset, 0);

	if(!gpio_is_valid(g1->gpio_border))
		goto out;
	gpio_direction_output(g1->gpio_border, 0);

	if(!gpio_is_valid(g1->gpio_busy))
		goto out;
	gpio_direction_input(g1->gpio_busy);

	if(!gpio_is_valid(g1->gpio_discharge))
		goto out;
	gpio_direction_output(g1->gpio_discharge, 0);

	ret = 0;
out:
	return ret;
}

static void g1_destroy(struct g1 *g1)
{
	if(g1 == NULL)
		return;
	if(g1->epd)
		epd_put(g1->epd);
	if(g1->pwm)
		g1_cleanup_pwm(g1);
	if(g1->therm)
		g1_cleanup_thermal(g1);
	kfree(g1);
}

static struct g1 *g1_create(struct spi_device *spi,
		struct g1_platform_data *pdata)
{
	struct g1 *g1 = NULL;
	struct epd *epd = NULL;
	struct epd_frame_size const *framesz;
	int err;

	/**
	 * TODO: use devmanagement devm_kzalloc()
	 */
	g1 = kzalloc(sizeof(*g1), GFP_KERNEL);
	if(g1 == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	if(pdata->type > G1_TYPE_MAX) {
		err = -EINVAL;
		goto fail;
	}

	framesz = &g1_frame_info[pdata->type];

	g1->type = pdata->type;
	g1->gpio_panel_on = pdata->gpio_panel_on;
	g1->gpio_reset = pdata->gpio_reset;
	g1->gpio_border = pdata->gpio_border;
	g1->gpio_busy = pdata->gpio_busy;
	g1->gpio_discharge = pdata->gpio_discharge;
	g1->spi = spi;
	g1->drv = g1_drv;
	g1->drv.framesz = framesz;

	err = g1_prepare_gpios(g1);
	if(err < 0)
		goto fail;

	err = g1_setup_thermal(g1);
	if(err < 0)
		goto fail;

	err = g1_init_pwm(g1);
	if(err < 0)
		goto fail;

	epd = epd_create(&spi->dev, &g1->drv);
	err = PTR_ERR_OR_ZERO(epd);
	if(err < 0)
		goto fail;

	g1->epd = epd;
	return g1;

fail:
	g1_destroy(g1);
	return ERR_PTR(err);
}

#ifdef CONFIG_OF
static const struct of_device_id g1_dt_ids[] = {
	{
		.compatible = DRIVER_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, g1_dt_ids);

static int g1_probe_dt(struct device *dev, struct g1_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
	struct of_device_id const *match;
	int ret = 0;

	/* Check that device tree node is ok */
	if(node == NULL) {
		ERR("Device does not have associated device tree data\n");
		ret = -EINVAL;
		goto out;
	}
	match = of_match_device(g1_dt_ids, dev);
	if(match == NULL) {
		ERR("Unknown device model\n");
		ret = -EINVAL;
		goto out;
	}

	/* TODO Get type from DT */
	pdata->type = G1_TYPE_2_7;

	/* Get gpio for panel_on */
	pdata->gpio_panel_on = of_get_named_gpio(node, "panel_on-gpios", 0);
	if(pdata->gpio_panel_on < 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Get gpio for /reset */
	pdata->gpio_reset = of_get_named_gpio(node, "reset-gpios", 0);
	if(pdata->gpio_reset < 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Get gpio for border */
	pdata->gpio_border = of_get_named_gpio(node, "border-gpios", 0);
	if(pdata->gpio_border < 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Get gpio for busy */
	pdata->gpio_busy = of_get_named_gpio(node, "busy-gpios", 0);
	if(pdata->gpio_busy < 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Get gpio for External Discharge */
	pdata->gpio_discharge = of_get_named_gpio(node, "discharge-gpios", 0);
	if(pdata->gpio_discharge < 0) {
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}
#else
static int g1_probe_dt(struct device *dev, struct g1_platform_data *pdata)
{
	return -EINVAL;
}
#endif

static int g1_probe(struct spi_device *spi)
{
	struct g1 *g1;
	struct g1_platform_data *pdata, data;
	int ret = 0;

	DBG("Call g1_probe()\n");

	ret = spi_setup(spi);
	if(ret < 0) {
		ERR("Fail to setup spi\n");
		goto out;
	}

	/*
	 * Get platform data in order to get all gpios config for border,
	 * /reset, ....
	 * This can be fetched from dt
	 */
	pdata = dev_get_platdata(&spi->dev);
	if(!pdata) {
		pdata = &data;
		ret = g1_probe_dt(&spi->dev, pdata);
		if(ret < 0) {
			ERR("Fail to get platform data\n");
			goto out;
		}
	}

	g1 = g1_create(spi, pdata);
	ret = PTR_ERR_OR_ZERO(g1);
	if(ret < 0) {
		ERR("Fail to create COG-G1\n");
		goto out;
	}

	spi_set_drvdata(spi, g1);
out:
	return ret;
}

static int g1_remove(struct spi_device *spi)
{
	struct g1 *g1 = spi_get_drvdata(spi);
	DBG("Call g1_remove()\n");

	g1_destroy(g1);
	return 0;
}

/**
 * TODO support pm suspend/resume
 */
static struct spi_driver g1_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(g1_dt_ids),
		.owner = THIS_MODULE,
	},
	.probe = g1_probe,
	.remove = g1_remove,
};

module_spi_driver(g1_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("spi:" DRIVER_NAME);
