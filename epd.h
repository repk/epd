#ifndef _EPD_H_
#define _EPD_H_

struct epd_driver;

struct epd_frame_size {
	size_t line;
	size_t col;
};

struct epd_frame {
	size_t nrline;
	size_t nrdot;
	unsigned int bytes_per_line;
	u8 data[];
};

struct epd_ops {
	int (*draw_frame)(struct epd_driver *drv);
};

struct epd_driver {
	char const *name;
	char const *desc;
	struct epd_frame_size const *framesz;
	struct epd_ops ops;
};

struct epd;

/**
 * epd_get_cur_fb - Get current/displayed framebuffer
 * @epd: epaper display driver to get frambuffer from
 */
struct epd_frame *epd_get_cur_fb(struct epd *epd);

/**
 * epd_get_alt_fb - Get alternative framebuffer.
 * @epd: epaper display driver to get framebuffer form
 *
 * Return the temporary alternative framebuffer that will be the one
 * displayed at next screen update.
 */
struct epd_frame *epd_get_alt_fb(struct epd *epd);

/**
 * epd_create - Create a new epaper display driver
 * @dev: Parent device
 * @drv: epaper display driver description
 */
struct epd *epd_create(struct device *dev, struct epd_driver *drv);

/**
 * Release a epaper display driver
 * @epd: epaper display driver to release
 */
void epd_put(struct epd *epd);

#endif
