#include <linux/module.h>
#include <linux/pwm.h>

struct pwm_device *pwm_get(struct device *dev, const char *con_id)
{
	struct pwm_device *res;

	(void)dev;
	(void)con_id;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if(res == NULL)
		return res;

	res->label = con_id;

	return res;
}

void pwm_free(struct pwm_device *pwm)
{
	kfree(pwm);
}

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	pwm->duty_cycle = duty_ns;
	pwm->period = period_ns;
	return 0;
}

int pwm_enable(struct pwm_device *pwm)
{
	printk("Enable PWM %s with period %u and duty %u\n", pwm->label,
			pwm->period, pwm->duty_cycle);
	return 0;
}

int pwm_disable(struct pwm_device *pwm)
{
	printk("Disable PWM %s\n", pwm->label);
	return 0;
}
