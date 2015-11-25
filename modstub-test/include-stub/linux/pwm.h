#ifndef _LINUX_STUB_PWM_H_
#define _LINUX_STUB_PWM_H_

/**
 * enum pwm_polarity - polarity of a PWM signal
 * @PWM_POLARITY_NORMAL: a high signal for the duration of the duty-
 * cycle, followed by a low signal for the remainder of the pulse
 * period
 * @PWM_POLARITY_INVERSED: a low signal for the duration of the duty-
 * cycle, followed by a high signal for the remainder of the pulse
 * period
 */
enum pwm_polarity {
	PWM_POLARITY_NORMAL,
	PWM_POLARITY_INVERSED,
};

enum {
	PWMF_REQUESTED = 1 << 0,
	PWMF_ENABLED = 1 << 1,
	PWMF_EXPORTED = 1 << 2,
};

/**
 * struct pwm_device - PWM channel object
 * @label: name of the PWM device
 * @flags: flags associated with the PWM device
 * @hwpwm: per-chip relative index of the PWM device
 * @pwm: global index of the PWM device
 * @chip: PWM chip providing this PWM device
 * @chip_data: chip-private data associated with the PWM device
 * @period: period of the PWM signal (in nanoseconds)
 * @duty_cycle: duty cycle of the PWM signal (in nanoseconds)
 * @polarity: polarity of the PWM signal
 */
struct pwm_device {
	const char *label;
	unsigned long flags;
	unsigned int hwpwm;
	unsigned int pwm;
	struct pwm_chip *chip;
	void *chip_data;

	unsigned int period;
	unsigned int duty_cycle;
	enum pwm_polarity polarity;
};

struct pwm_device *pwm_get(struct device *dev, const char *con_id);
void pwm_free(struct pwm_device *pwm);
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns);
int pwm_enable(struct pwm_device *pwm);
int pwm_disable(struct pwm_device *pwm);

#endif
