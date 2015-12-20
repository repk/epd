#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
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

struct epd_frame {
	size_t nrline;
	size_t nrdot;
	unsigned int bytes_per_line;
	u8 data[];
};
#define EPD_DOT_NRBIT 2
#define EPD_DOT_PER_BYTE (8 / EPD_DOT_NRBIT)
#define EPD_DOT_B 3
#define EPD_DOT_W 2
#define EPD_DOT_N 1
#define EPD_SCAN_NRBIT 2
#define EPD_SCAN_PER_BYTE (8 / EPD_SCAN_NRBIT)
#define EPD_SCAN_OFF 0
#define EPD_SCAN_ON 3
#define EPD_DUMMY_LINE ((size_t)(-1))

struct epd_frame_size {
	size_t line;
	size_t col;
};

static struct epd_frame_size const epd_frame_info[] = {
	[EPD_TYPE_1_44] = {
		.line = 96,
		.col = 128,
	},
	[EPD_TYPE_2] = {
		.line = 96,
		.col = 200,
	},
	[EPD_TYPE_2_7] = {
		.line = 176,
		.col = 264,
	},
};

struct epd {
	struct device *dev;
	struct epd_frame *fold;
	struct epd_frame *fnew;
	struct spi_device *spi;
	struct i2c_client *therm;
	struct pwm_device *pwm;
	struct mutex lock;
	enum epd_type type;
	unsigned long stage_time;
	unsigned int id;
	int gpio_panel_on;
	int gpio_reset;
	int gpio_border;
	int gpio_busy;
	int gpio_discharge;
};
#define EPD_DEVT(e) MKDEV(epd_major, e->id + 1)

#define EPD_CTL_CLEAR 'C'
#define EPD_CTL_BLACK 'B'
#define EPD_CTL_WRITE 'W'

#define EPD_MAX_DEVICES 15
static int epd_major;
static struct cdev epd_cdev;
static struct class *epddev_class;
/* TODO support a list of epddev for multiple screen */
static DEFINE_MUTEX(epddev_lock);
static struct epd *epddev;

/* TODO support a list of epddev for multiple screen */
static int epd_device_add(struct epd *epd)
{
	int ret = 0;

	mutex_lock(&epddev_lock);
	if(epddev != NULL) {
		ERR("Too much screen\n");
		ret = -ENODEV;
		goto out;
	}
	epddev = epd;
out:
	mutex_unlock(&epddev_lock);
	return ret;
}

static struct epd *epd_device_get(unsigned int id)
{
	struct epd *epd = ERR_PTR(-ENXIO);

	mutex_lock(&epddev_lock);
	if(epddev != NULL && epddev->id == id)
		epd = epddev;
	mutex_unlock(&epddev_lock);
	return epd;
}

static void epd_device_remove(struct epd *epd)
{
	mutex_lock(&epddev_lock);
	if(epddev == epd)
		epddev = NULL;
	mutex_unlock(&epddev_lock);
}

static void epd_frame_cleanup(struct epd_frame *frame)
{
	if(frame)
		kfree(frame);
}

static struct epd_frame *epd_frame_create(size_t line, size_t col)
{
	struct epd_frame *f;
	unsigned int bytes_per_line = DIV_ROUND_UP(col, 8);

	f = kmalloc(sizeof(*f) + line * bytes_per_line, GFP_KERNEL);
	if(f == NULL)
		return NULL;

	f->nrline = line;
	f->nrdot = col;
	f->bytes_per_line = bytes_per_line;

	return f;
}

static void epd_frame_black(struct epd_frame *frame)
{
	size_t i, j;

	for(i = 0; i < frame->nrline; ++i)
		for(j = 0; j < frame->bytes_per_line; ++j)
			frame->data[i * frame->bytes_per_line + j] = 0xff;
}

static void epd_frame_white(struct epd_frame *frame)
{
	size_t i, j;

	for(i = 0; i < frame->nrline; ++i)
		for(j = 0; j < frame->bytes_per_line; ++j)
			frame->data[i * frame->bytes_per_line + j] = 0x00;
}

static void epd_destroy(struct epd *epd)
{
	if(epd == NULL)
		return;

	epd_frame_cleanup(epd->fold);
	epd_frame_cleanup(epd->fnew);
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
	struct epd *epd = NULL;
	struct epd_frame_size const *framesz;
	int err;

	/**
	 * TODO: use devmanagement devm_kzalloc()
	 */
	epd = kzalloc(sizeof(*epd), GFP_KERNEL);
	if(epd == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	if(pdata->type > EPD_TYPE_MAX) {
		err = -EINVAL;
		goto fail;
	}

	epd->type = pdata->type;
	framesz = &epd_frame_info[epd->type];

	epd->gpio_panel_on = pdata->gpio_panel_on;
	epd->gpio_reset = pdata->gpio_reset;
	epd->gpio_border = pdata->gpio_border;
	epd->gpio_busy = pdata->gpio_busy;
	epd->gpio_discharge = pdata->gpio_discharge;

	err = epd_prepare_gpios(epd);
	if(err < 0)
		goto fail;

	epd->fold = epd_frame_create(framesz->line, framesz->col);
	if(epd->fold == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	epd->fnew = epd_frame_create(framesz->line, framesz->col);
	if(epd->fnew == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	epd_frame_black(epd->fold);
	epd_frame_white(epd->fnew);

	epd->id = 0;
	mutex_init(&epd->lock);

	return epd;

fail:
	epd_destroy(epd);

	return ERR_PTR(err);
}

static void epd_update_frame(struct epd *epd)
{
	struct epd_frame *f = epd->fold;

	epd->fold = epd->fnew;
	epd->fnew = f;
	/* update new buffer for read() to be ok */
	memcpy(epd->fnew->data, epd->fold->data,
			epd->fnew->nrline * epd->fnew->bytes_per_line);
}

static void epd_compute_stage_time(struct epd *epd)
{
	unsigned long stage_time = 0;
	int temp;

	switch(epd->type) {
	case EPD_TYPE_1_44:
	case EPD_TYPE_2:
		stage_time = 480;
		break;
	case EPD_TYPE_2_7:
		stage_time = 630;
		break;
	}

	temp = epd_therm_get_temp(epd->therm);
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

	epd->stage_time = DIV_ROUND_UP(stage_time, 10);
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

enum epd_stage {
	EPD_STAGE_COMPENSATE,
	EPD_STAGE_WHITE,
	EPD_STAGE_INVERSE,
	EPD_STAGE_NORMAL,
	EPD_STAGE_POWEROFF,
};

#define EPD_ODD_BYTE(dot) (dot)
#define EPD_EVEN_BYTE(dot)						\
	(((((dot) >> 6) & 0x3) << 0) |					\
	 ((((dot) >> 4) & 0x3) << 2) |					\
	 ((((dot) >> 2) & 0x3) << 4) |					\
	 ((((dot) >> 0) & 0x3) << 6))

static int fill_line(struct epd_frame *frame, enum epd_stage stage,
		size_t line, u8 *data, size_t len)
{
	u8 *ptr, *end;
	size_t lbyte, dotnr, scannr, i;
	int ret = 0;
	u8 dot = 0;

	dotnr = frame->nrdot / EPD_DOT_PER_BYTE;
	scannr = frame->nrline / EPD_SCAN_PER_BYTE;
	lbyte = frame->bytes_per_line;

	/*
	 * Some length checking
	 */
	if((len < dotnr + scannr) || (dotnr != 2 * lbyte)) {
		ret = -EINVAL;
		goto out;
	}

	ptr = data;
	end = data + len;

	/* odd dots (263, ..., 3, 1) */
	for(i = lbyte; i > 0; --i, ++ptr) {
		if(line != EPD_DUMMY_LINE)
			dot = frame->data[line * lbyte + i - 1] & 0xaa;
		switch(stage) {
		case EPD_STAGE_COMPENSATE:
			*ptr = EPD_ODD_BYTE(~(dot >> 1));
			break;
		case EPD_STAGE_WHITE:
			*ptr = EPD_ODD_BYTE(dot ^ 0xaa);
			break;
		case EPD_STAGE_INVERSE:
			*ptr = EPD_ODD_BYTE(~dot);
			break;
		case EPD_STAGE_NORMAL:
			*ptr = EPD_ODD_BYTE((dot >> 1) | 0xaa);
			break;
		case EPD_STAGE_POWEROFF:
			*ptr = 0x55;
			break;
		}
	}

	/* Scan line */
	for(i = 0; i < scannr; ++i, ++ptr) {
		if(i == line / EPD_SCAN_PER_BYTE) {
			*ptr = 0xc0 >> (EPD_SCAN_NRBIT *
					(line % EPD_SCAN_PER_BYTE));
		} else {
			*ptr = EPD_SCAN_OFF;
		}
	}

	/* even dots (0, 2, ..., 262) */
	for(i = 0; i < lbyte; ++i, ++ptr) {
		if(line != EPD_DUMMY_LINE)
			dot = frame->data[line * lbyte + i] & 0x55;
		switch(stage) {
		case EPD_STAGE_COMPENSATE:
			*ptr = EPD_EVEN_BYTE(~dot);
			break;
		case EPD_STAGE_WHITE:
			*ptr = EPD_EVEN_BYTE((dot ^ 0x55) << 1);
			break;
		case EPD_STAGE_INVERSE:
			*ptr = EPD_EVEN_BYTE((dot + 0x55) ^ 0xaa);
			break;
		case EPD_STAGE_NORMAL:
			*ptr = EPD_EVEN_BYTE(dot | 0xaa);
			break;
		case EPD_STAGE_POWEROFF:
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

static int draw_line(struct epd *epd, enum epd_stage stage, size_t line)
{
	struct epd_frame *f;
	struct epd_frame_size const *fsz;
	u8 *data = NULL;
	size_t dotnr, scannr;
	int ret, filler = 0;

	DBG("Send line %zu\n", line);

	switch(epd->type) {
	case EPD_TYPE_1_44:
		filler = 0;
		ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_1_44);
		if(ret < 0)
			goto out;
		break;
	case EPD_TYPE_2:
		filler = 1;
		ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_2);
		if(ret < 0)
			goto out;
		break;
	case EPD_TYPE_2_7:
		filler = 1;
		ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_2_7);
		if(ret < 0)
			goto out;
		break;
	}

	if(stage == EPD_STAGE_COMPENSATE || stage == EPD_STAGE_WHITE)
		f = epd->fold;
	else
		f = epd->fnew;

	fsz = &epd_frame_info[epd->type];
	dotnr = fsz->col / EPD_DOT_PER_BYTE;
	scannr = fsz->line / EPD_SCAN_PER_BYTE;

	data = kmalloc(dotnr + scannr + filler, GFP_KERNEL);
	if(data == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = fill_line(f, stage, line, data, dotnr + scannr + filler);
	if(ret)
		goto out;

	ret = spi_send_data(epd->spi, epd->gpio_busy, data,
			dotnr + scannr + filler);
	if(ret)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_OUTPUT_ENABLE);

out:
	if(data)
		kfree(data);

	return ret;
}

static int poweroff_stage(struct epd *epd)
{
	size_t i;
	int ret = 0;

	for(i = 0; i < epd_frame_info[epd->type].line; ++i) {
		ret = draw_line(epd, EPD_STAGE_POWEROFF, i);
		if(ret < 0)
			goto out;
	}

	ret = draw_line(epd, EPD_STAGE_POWEROFF, EPD_DUMMY_LINE);
out:
	return ret;
}

static int draw_stage(struct epd *epd, enum epd_stage stage)
{
	size_t i;
	int ret = 0;

	for(i = 0; i < epd_frame_info[epd->type].line; ++i) {
		ret = draw_line(epd, stage, i);
		if(ret < 0)
			goto out;
	}
out:
	return ret;
}

static int repeat_stage(struct epd *epd, enum epd_stage stage)
{
	unsigned long timeout;
	int ret;

	timeout = jiffies + msecs_to_jiffies(epd->stage_time);
	do {
		ret = draw_stage(epd, stage);
		if(ret < 0)
			goto out;
	} while(time_before(jiffies, timeout));

out:
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

static int power_off(struct epd *epd)
{
	int ret;

	ret = poweroff_stage(epd);
	if(ret < 0)
		goto out;

	mdelay(25);
	gpio_set_value(epd->gpio_border, 0);
	mdelay(250);
	gpio_set_value(epd->gpio_border, 1);

	ret = spi_send_cmd(epd->spi, SPI_CMD_LATCH_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_OUTPUT_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VCOM_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VNEG_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_1);
	if(ret < 0)
		goto out;
	mdelay(120);

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VPOS_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_OSC_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_2);
	if(ret < 0)
		goto out;
	mdelay(40);

	ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_3);
	if(ret < 0)
		goto out;
	mdelay(40);

	ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_DISCHARGE_0);
	if(ret < 0)
		goto out;

	gpio_set_value(epd->gpio_border, 0);
	gpio_set_value(epd->gpio_reset, 0);
	gpio_set_value(epd->gpio_panel_on, 0);
	gpio_set_value(epd->gpio_discharge, 1);
	mdelay(150);
	gpio_set_value(epd->gpio_discharge, 0);
out:
	return ret;
}

static int init_display(struct epd *epd)
{
	int ret = 0;

	while(gpio_get_value(epd->gpio_busy))
		cpu_relax();

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHANSEL_2_7);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_DCFREQ);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_OSC_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_ADC_DISABLE);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_VCOM_LVL);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_GATE_SRC_LVL_2_7);
	if(ret < 0)
		goto out;
	mdelay(5);

	ret = spi_send_cmd(epd->spi, SPI_CMD_LATCH_ON);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_LATCH_OFF);
	if(ret < 0)
		goto out;

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VPOS_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	pwm_disable(epd->pwm);

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VNEG_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	ret = spi_send_cmd(epd->spi, SPI_CMD_CHARGEPUMP_VCOM_ON);
	if(ret < 0)
		goto out;
	mdelay(30);

	ret = spi_send_cmd(epd->spi, SPI_CMD_OUTPUT_DISABLE);
	if(ret < 0)
		goto out;
out:
	return ret;
}

static int epd_draw_frame(struct epd *epd)
{
	int ret;

	DBG("Power on display\n");
	ret = power_on(epd);
	if(ret < 0)
		goto out;

	DBG("Init display\n");
	ret = init_display(epd);
	if(ret < 0)
		goto out;

	epd_compute_stage_time(epd);
	DBG("Stage time : %lu\n", epd->stage_time);

	DBG("Draw compensate stage\n");
	ret = repeat_stage(epd, EPD_STAGE_COMPENSATE);
	if(ret < 0)
		goto out;

	DBG("Draw white stage\n");
	ret = repeat_stage(epd, EPD_STAGE_WHITE);
	if(ret < 0)
		goto out;

	DBG("Draw inverse stage\n");
	ret = repeat_stage(epd, EPD_STAGE_INVERSE);
	if(ret < 0)
		goto out;

	DBG("Draw normal stage\n");
	ret = repeat_stage(epd, EPD_STAGE_NORMAL);
	if(ret < 0)
		goto out;

	epd_update_frame(epd);

	DBG("Power off display\n");
	ret = power_off(epd);
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
	 * TODO Get type from DT
	 */
	pdata->type = EPD_TYPE_2_7;

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
	struct device *dev;
	struct epd_platform_data *pdata, data;
	int ret = 0;

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
	if(IS_ERR(epd)) {
		ret = PTR_ERR(epd);
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

	/* Clear the screen */
	epd_draw_frame(epd);

	ret = epd_device_add(epd);
	if(ret < 0)
		goto fail;

	dev = device_create(epddev_class, &spi->dev, EPD_DEVT(epd), epd,
			"epd%u", epd->id);
	ret = PTR_ERR_OR_ZERO(dev);
	if(ret < 0)
		goto fail;
	epd->dev = dev;

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

	epd_device_remove(epd);
	if(epd->dev != NULL)
		device_destroy(epddev_class, EPD_DEVT(epd));
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

static ssize_t epd_fb_read(struct file *f, char __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	size_t bufsz, sz;
	ssize_t ret = 0;

	epd = f->private_data;
	bufsz = epd->fnew->nrline * epd->fnew->bytes_per_line;
	if(*off > bufsz)
		goto out;

	sz = min_t(size_t, len, bufsz - *off);
	mutex_lock(&epd->lock);
	ret = copy_to_user(buf, epd->fnew->data + *off, sz);
	mutex_unlock(&epd->lock);
	if(ret < 0)
		goto out;
	ret = sz - ret;
	*off += ret;
out:
	return ret;
}

static ssize_t epd_fb_write(struct file *f, char const __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	size_t bufsz;
	long missing = 0;
	int ret = 0;

	epd = f->private_data;
	bufsz = epd->fnew->nrline * epd->fnew->bytes_per_line;

	if(len + *off > bufsz) {
		ret = -EMSGSIZE;
		goto out;
	}

	mutex_lock(&epd->lock);
	missing = copy_from_user(epd->fnew->data + *off, buf, len);
	if(missing != 0) {
		ret = -EFAULT;
		goto unlock;
	}
	*off += len;
	ret = len;
unlock:
	mutex_unlock(&epd->lock);
out:
	return ret;
}

static int epd_fb_open(struct inode *i, struct file *f)
{
	struct epd *epd;
	int ret;

	/*
	 * If realease cannot be call here we are safe, otherway we have to be
	 * sure epddev is not being destroy here.
	 * TODO Maybe use a refcounter in epd, held by epd_device_get() release
	 * by epd_device_put()
	 */
	epd = epd_device_get(iminor(i) - 1);
	ret = PTR_ERR_OR_ZERO(epd);
	if(ret < 0)
		goto out;
	f->private_data = epd;
out:
	return ret;
}

static int epd_fb_release(struct inode *i, struct file *f)
{
	f->private_data = NULL;
	return 0;
}

static struct file_operations const epd_fb_ops = {
	.owner = THIS_MODULE,
	.read = epd_fb_read,
	.write = epd_fb_write,
	.open = epd_fb_open,
	.release = epd_fb_release,
	.llseek = default_llseek,
};

static ssize_t epd_ctl_write(struct file *f, char const __user *buf,
		size_t len, loff_t *off)
{
	struct epd *epd;
	char *msg;
	int ret = -EINVAL;
	unsigned int eid;
	u8 cmd;

	if(len < 2)
		goto out;

	msg = kmalloc((len + 1) * sizeof(*msg), GFP_KERNEL);
	if(msg == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if(copy_from_user(msg, buf, len)) {
		ret = -EFAULT;
		kfree(msg);
		goto out;
	}
	msg[len] = '\0';

	/* Get cmd and epd id */
	ret = sscanf(msg, "%c%u", &cmd, &eid);
	kfree(msg);
	if(ret != 2) {
		ret = -EINVAL;
		goto out;
	}

	epd = epd_device_get(eid);
	ret = PTR_ERR_OR_ZERO(epd);
	if(ret < 0)
		goto out;

	ret = len;
	mutex_lock(&epd->lock);
	switch(cmd) {
	case 'C':
		epd_frame_white(epd->fnew);
		epd_draw_frame(epd);
		break;
	case 'B':
		epd_frame_black(epd->fnew);
		epd_draw_frame(epd);
		break;
	case 'W':
		epd_draw_frame(epd);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&epd->lock);
out:
	return ret;
}

static struct file_operations const epd_ctl_ops = {
	.owner = THIS_MODULE,
	.write = epd_ctl_write,
	.open = nonseekable_open,
	.llseek = no_llseek,
};

static int epd_open(struct inode *i, struct file *f)
{
	struct file_operations const *fops = &epd_ctl_ops;
	int minor = iminor(i);
	int ret = 0;

	/* Mux fops: minor = 0 -> controller / minor != 0 -> framebuffer */
	if(minor != 0)
		fops = &epd_fb_ops;

	replace_fops(f, fops_get(fops));
	if(f->f_op->open)
		ret = f->f_op->open(i, f);
	return ret;
}

/* File operations mux between controller and framebuffers */
static struct file_operations const epd_ops = {
	.owner = THIS_MODULE,
	.open = epd_open,
	.llseek = noop_llseek,
};

static int __init epd_init(void)
{
	struct device *dev;
	dev_t dev_id;
	int ret;

	DBG("Init driver\n");

	/* Alloc 1 char for each screen and one for mux controller */
	ret = alloc_chrdev_region(&dev_id, 0, EPD_MAX_DEVICES + 1, "epd");
	if(ret < 0) {
		ERR("Cannot alloc char dev major number\n");
		goto err;
	}
	epd_major = MAJOR(dev_id);

	epddev_class = class_create(THIS_MODULE, "epd");
	if(IS_ERR(epddev_class)) {
		ERR("Cannot create driver class\n");
		goto err_chrdev;
	}

	cdev_init(&epd_cdev, &epd_ops);
	ret = cdev_add(&epd_cdev, dev_id, EPD_MAX_DEVICES + 1);
	if(ret < 0) {
		ERR("Cannot add char dev\n");
		goto err_class;
	}

	dev = device_create(epddev_class, NULL, dev_id, NULL, "epdctl");
	ret = PTR_ERR_OR_ZERO(dev);
	if(ret < 0)
		goto err_cdev;


	ret = spi_register_driver(&epd_driver);
	if(ret < 0) {
		ERR("Cannot register spi driver\n");
		goto err_destroy;
	}

	pr_info("epd: EM027AS012 based epaper display driver\n");

	return 0;

err_destroy:
	device_destroy(epddev_class, dev_id);
err_cdev:
	cdev_del(&epd_cdev);
err_class:
	class_destroy(epddev_class);
err_chrdev:
	unregister_chrdev_region(dev_id, EPD_MAX_DEVICES + 1);
err:
	return ret;
}

module_init(epd_init);

static void __exit epd_exit(void)
{
	dev_t dev_id;

	DBG("Cleanup driver\n");

	dev_id = MKDEV(epd_major, 0);

	spi_unregister_driver(&epd_driver);
	device_destroy(epddev_class, dev_id);
	cdev_del(&epd_cdev);
	class_destroy(epddev_class);
	unregister_chrdev_region(dev_id, EPD_MAX_DEVICES + 1);
}

module_exit(epd_exit);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("EM027AS012 based epaper display driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("spi:epd");
