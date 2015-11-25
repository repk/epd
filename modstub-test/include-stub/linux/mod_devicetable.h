#ifndef _LINUX_STUB_MOD_DEVICE_TABLE_H_
#define _LINUX_STUB_MOD_DEVICE_TABLE_H_

/*
 * Struct used for matching a device
 */
struct of_device_id {
	char	name[32];
	char	type[32];
	char	compatible[128];
	const void *data;
};

#endif
