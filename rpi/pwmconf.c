#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#define _GNU_SOURCE
#include <unistd.h>

#include <sys/mman.h>

#define MEMMAP "/dev/mem"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

/* Physical IOMEM address */
#define BCM2708_PERI_BASE	0x20000000UL

#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000UL)
#define GPIO_SZ (0xb4)
#define PWMCTL_BASE (BCM2708_PERI_BASE + 0x1010a0UL)
#define PWMCTL_SZ (0x8)

enum iobank {
	IOB_GPIO,
	IOB_PWMCTL,
	IOB_NR
};

struct bank_info {
	size_t base;
	size_t len;
	enum iobank id;
};

#define BANK_INFO(i, b, sz)						\
	[i] = {								\
		.base = BCM2708_PERI_BASE + b,				\
		.len = sz,						\
		.id = i,						\
	}

static struct bank_info const bank_info[] = {
	BANK_INFO(IOB_GPIO, 0x200000, 0xb4),
	BANK_INFO(IOB_PWMCTL, 0x1010A0, 0x8),
};

struct rpi_iobank {
	void *map;
	void *base;
	size_t mapsz;
	enum iobank id;
};

struct rpi_iomem {
	struct rpi_iobank bank[IOB_NR];
};

#define RPI_IOBANK_SYNC(io, b) usleep(10)

static void rpi_iomem_cleanup(struct rpi_iomem *io)
{
	size_t i;
	for(i = 0; i < ARRAY_SIZE(io->bank); ++i) {
		if(io->bank[i].map)
			munmap(io->bank[i].map, io->bank[i].mapsz);
	}
}

static int rpi_iomem_init(struct rpi_iomem *io)
{
	void *addr;
	size_t i, pagesz;
	off_t base, off;
	int fd = 0;

	memset(io, 0, sizeof(*io));

	pagesz = sysconf(_SC_PAGE_SIZE);
	if(pagesz < 1) {
		fprintf(stderr, "Cannot get page size");
		goto err;
	}

	fd = open(MEMMAP, O_RDWR | O_SYNC);
	if(fd < 0) {
		perror("Cannot open " MEMMAP);
		goto err;
	}

	for(i = 0; i < IOB_NR; ++i) {
		base = (bank_info[i].base / pagesz) * pagesz;
		off = bank_info[i].base % pagesz;
		addr = mmap(NULL, pagesz,
				PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
		if(addr == MAP_FAILED) {
			perror("Cannot map memio");
			goto close_err;
		}
		io->bank[i].map = addr;
		io->bank[i].base = ((uint8_t *)addr + off);
		io->bank[i].mapsz = bank_info[i].len + off;
	}

	close(fd);
	return 0;

close_err:
	close(fd);
	rpi_iomem_cleanup(io);
err:
	return -1;
}

static int rpi_iomem_read32(struct rpi_iomem const *io, enum iobank bank,
		uint32_t *val, off_t offset, size_t shift, size_t mask)
{
	uint32_t *addr;
	int ret = -1;

	if(bank >= IOB_NR || offset + sizeof(uint32_t) > bank_info[bank].len)
		goto out;

	addr = (uint32_t *)(((uint8_t *)io->bank[bank].base) + offset);
	*val = ((*addr) >> shift) & mask;

	ret = 0;
out:
	return ret;
}

static int rpi_iomem_pwdwrite32(struct rpi_iomem *io, enum iobank bank,
		uint32_t val, off_t offset, size_t shift, size_t mask, uint32_t pwd)
{
	volatile uint32_t *addr;
	int ret = -1;
	uint32_t rst = ~(mask << shift);

	if(bank >= IOB_NR || offset + sizeof(uint32_t) > bank_info[bank].len)
		goto out;

	addr = (uint32_t *)(((uint8_t *)io->bank[bank].base) + offset);
	*addr = (*addr & rst) | ((val & mask) << shift) | pwd;
	ret = RPI_IOBANK_SYNC(io, bank);
out:
	return ret;
}

static int rpi_iomem_write32(struct rpi_iomem *io, enum iobank bank,
		uint32_t val, off_t offset, size_t shift, size_t mask)
{
	return rpi_iomem_pwdwrite32(io, bank, val, offset, shift, mask, 0);
}

#define GPIO_FUNC_INPUT		0
#define GPIO_FUNC_OUTPUT	1
#define GPIO_FUNC_ALT0		4
#define GPIO_FUNC_ALT1		5
#define GPIO_FUNC_ALT2		6
#define GPIO_FUNC_ALT3		7
#define GPIO_FUNC_ALT4		3
#define GPIO_FUNC_ALT5		2

#define GFSEL_GPIO_NRBIT	(3) /* 3 bits per GPIO */
#define GFSEL_REG_SIZE		(sizeof(uint32_t))
#define GFSEL_NR_PER_REG	((GFSEL_REG_SIZE * 8) / GFSEL_GPIO_NRBIT)
#define GFSEL_GPIO_MASK		(7) /* 0b111 */
#define GFSEL_GPIO_SHIFT(n)	(((n) % GFSEL_NR_PER_REG) * GFSEL_GPIO_NRBIT)
#define GFSEL_GPIO_OFFSET(n)	((((n) / GFSEL_NR_PER_REG)) * GFSEL_REG_SIZE)

#define GPIO_FSEL_BANK_SZ	(sizeof(uint32_t) * 8)
#define GPIO_FSEL_GPIO_SZ	(3) /* 3 bits per GPIO */
#define GPIO_FSEL_GPIO_MASK	(7) /* 0b111 */

#define GPIO_FSEL_GET(io, n, v)						\
	rpi_iomem_read32(io, IOB_GPIO, v, GFSEL_GPIO_OFFSET(n),		\
			GFSEL_GPIO_SHIFT(n), GFSEL_GPIO_MASK)

#define GPIO_FSEL_SET(io, n, v)						\
	rpi_iomem_write32(io, IOB_GPIO, v, GFSEL_GPIO_OFFSET(n),	\
			GFSEL_GPIO_SHIFT(n), GFSEL_GPIO_MASK)

static int rpi_iomem_setup_gpio(struct rpi_iomem *io)
{
	GPIO_FSEL_SET(io, 18, GPIO_FUNC_ALT5);
	return RPI_IOBANK_SYNC(io, IOB_GPIO);
}

#define PWMCTL_PWD (0x5a000000)

#define PWMCLK_GND	0
#define PWMCLK_OSC	1
#define PWMCLK_TSTDBG0	2
#define PWMCLK_TSTDBG1	3
#define PWMCLK_PLLA	4
#define PWMCLK_PLLC	5
#define PWMCLK_PLLD	6
#define PWMCLK_HDMI	7

#define PWMCTL_OFFSET	0
#define PWMDIV_OFFSET	4

#define PCTL_MASH_SHIFT (9)
#define PCTL_MASH_MASK (0x1)

#define PCTL_CLK_SHIFT (0)
#define PCTL_CLK_MASK (7) /* 0b111 */

#define PDIV_DIVF_SHIFT (0)
#define PDIV_DIVF_MASK (0xfff)

#define PDIV_DIVI_SHIFT (12)
#define PDIV_DIVI_MASK (0xfff)

#define PWMCTL_CLK_ENABLE(io)						\
	rpi_iomem_pwdwrite32(io, IOB_PWMCTL, 1, 0, 4, 0x1, PWMCTL_PWD)

#define PWMCTL_MASH_GET(io, v)						\
	rpi_iomem_read32(io, IOB_PWMCTL, v, PWMCTL_OFFSET,		\
			PCTL_MASH_SHIFT, PCTL_MASH_MASK)
#define PWMCTL_MASH_SET(io, v)						\
	rpi_iomem_pwdwrite32(io, IOB_PWMCTL, v, PWMCTL_OFFSET,		\
			PCTL_MASH_SHIFT, PCTL_MASH_MASK, PWMCTL_PWD)

#define PWMCTL_CLK_GET(io, v)						\
	rpi_iomem_read32(io, IOB_PWMCTL, v, PWMCTL_OFFSET,		\
			PCTL_CLK_SHIFT, PCTL_CLK_MASK)
#define PWMCTL_CLK_SET(io, v)						\
	rpi_iomem_pwdwrite32(io, IOB_PWMCTL, v, PWMCTL_OFFSET,		\
			PCTL_CLK_SHIFT, PCTL_CLK_MASK, PWMCTL_PWD)

#define PWMCTL_DIVI_GET(io, v)						\
	rpi_iomem_read32(io, IOB_PWMCTL, v, PWMDIV_OFFSET,		\
			PDIV_DIVI_SHIFT, PDIV_DIVI_MASK)
#define PWMCTL_DIVI_SET(io, v)						\
	rpi_iomem_pwdwrite32(io, IOB_PWMCTL, v, PWMDIV_OFFSET,		\
			PDIV_DIVI_SHIFT, PDIV_DIVI_MASK, PWMCTL_PWD)

#define PWMCTL_DIVF_GET(io,v)						\
	rpi_iomem_read32(io, IOB_PWMCTL, v, PWMDIV_OFFSET,		\
			PDIV_DIVF_SHIFT, PDIV_DIVF_MASK)
#define PWMCTL_DIVF_SET(io, v)						\
	rpi_iomem_pwdwrite32(io, IOB_PWMCTL, v, PWMDIV_OFFSET,		\
			PDIV_DIVF_SHIFT, PDIV_DIVF_MASK, PWMCTL_PWD)

static int rpi_iomem_setup_pwm(struct rpi_iomem *io)
{
	int ret;

	ret = rpi_iomem_setup_gpio(io);
	if(ret != 0)
		goto exit;

	ret = PWMCTL_CLK_SET(io, PWMCLK_PLLD); /* 500 Mhz */
	if(ret != 0)
		goto exit;

	ret = PWMCTL_MASH_SET(io, 1);
	if(ret != 0)
		goto exit;

	ret = PWMCTL_DIVI_SET(io, 5); /* 100 MHz */
	if(ret != 0)
		goto exit;

	ret = PWMCTL_DIVF_SET(io, 0); /* 100 MHz */
	if(ret != 0)
		goto exit;

	ret = RPI_IOBANK_SYNC(io, IOB_PWMCTL);

	ret = PWMCTL_CLK_ENABLE(io);
	if(ret != 0)
		goto exit;
exit:
	return ret;
}

static void rpi_iomem_print_setup(struct rpi_iomem const *io)
{
	uint32_t val;

	GPIO_FSEL_GET(io, 18, &val);
	printf("GPIO 18 is 0x%x\n", val);
	PWMCTL_MASH_GET(io, &val);
	printf("PWMCTL_MASH is 0x%x\n", val);
	PWMCTL_CLK_GET(io, &val);
	printf("PWMCTL_CLK is 0x%x\n", val);
	PWMCTL_DIVI_GET(io, &val);
	printf("PWMCTL_DIVI is 0x%x\n", val);
	PWMCTL_DIVF_GET(io, &val);
	printf("PWMCTL_DIVF is 0x%x\n", val);
}

int main(void)
{
	struct rpi_iomem iomem;
	int ret;

	ret = rpi_iomem_init(&iomem);
	if(ret != 0)
		goto exit;

	rpi_iomem_print_setup(&iomem);

	ret = rpi_iomem_setup_pwm(&iomem);
	if(ret != 0) {
		perror("");
		goto exit;
	}

	rpi_iomem_cleanup(&iomem);

	ret = rpi_iomem_init(&iomem);
	if(ret != 0)
		goto exit;

	/* Ensure that new pwm conf registers are really committed in memory */
	rpi_iomem_print_setup(&iomem);

	rpi_iomem_cleanup(&iomem);
exit:
	return ret;
}
