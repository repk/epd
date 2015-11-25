#ifndef _LINUX_STUB_DEVICE_H_
#define _LINUX_STUB_DEVICE_H_

struct device {
	void		*platform_data;
	void		*driver_data;
};

struct device_driver {
	const char		*name;

	struct module		*owner;
	const char		*mod_name;	/* used for built-in modules */

	const struct of_device_id	*of_match_table;

	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*resume) (struct device *dev);
};

static inline void *dev_get_platdata(const struct device *dev)
{
	return dev->platform_data;
}

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

#endif
