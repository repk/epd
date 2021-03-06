CC := $(CROSS_COMPILE)gcc
USER_CFLAGS := -W -Wall -std=c99
USER_LDFLAGS :=

SRC_EPD := core.c
SRC_G1 := epd_g1.c
SRC_EPD_THERM := epd_therm_i2c.c
SRC := $(SRC_EPD_THERM) $(SRC_EPD) $(SRC_G1)
INC := epd.h epd_therm.h epd_g1.h
DTOVERLAY := rpi/rpi-epd-overlay.dts
PWMCONFSRC := rpi/pwmconf.c

BUILDDIR := build
KBUILD := $(BUILDDIR)/epd
KOBJ_EPD := $(SRC_EPD:%.c=%.o)
KOBJ_G1 := $(SRC_G1:%.c=%.o)
KOBJ_EPD_THERM := $(SRC_EPD_THERM:%.c=%.o)
KOBJ := $(SRC:%.c=%.o)

ifeq ($(DEBUG), 1)
KFLAGS := -DDEBUG=1
endif

# We are called from the kernel (this makefile call the kernel's one which
# call's this one. So if KERNELRELEASE is defined we are at the second called to
# this makefile
ifneq ($(KERNELRELEASE),)
	obj-m := epd.o epd-g1.o epd-therm.o
	epd-y := $(KOBJ_EPD)
	epd-g1-y := $(KOBJ_G1)
	epd-therm-y := $(KOBJ_EPD_THERM)
	ccflags-y := $(KFLAGS)
# Here we are at the first call
else
KERNDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DTC := $(KERNDIR)/scripts/dtc/dtc

default: $(SRC) $(INC) Makefile | builddir
	cp $(SRC) Makefile $(INC) $(KBUILD)/
	$(MAKE) -C $(KERNDIR) M=$(PWD)/$(KBUILD) modules
	rm $(SRC:%=$(KBUILD)/%) $(INC:%=$(KBUILD)/%) $(KBUILD)/Makefile

rpi-pwmconf: $(PWMCONFSRC:%.c=$(KBUILD)/%.o)
	$(CC) $(USER_LDFLAGS) $(USER_CFLAGS) -o $(KBUILD)/rpi/pwmconf $<

$(KBUILD)/%.o: %.c | builddir
	$(CC) -c $(USER_CFLAGS) -o $@ $<

rpi-devicetree: $(DTOVERLAY:%.dts=$(KBUILD)/%.dtb)

$(KBUILD)/%.dtb: %.dts | builddir
	$(DTC) -@ -I dts -O dtb -b 0 -o $@ $<

builddir:
	mkdir -p $(KBUILD)
	mkdir -p $(dir $(DTOVERLAY:%.dts=$(KBUILD)/%.dtb))

clean:
	rm -f $(KOBJ:%=$(KBUILD)/%)
	rm -f $(DTOVERLAY:%.dts=$(KBUILD)/%.dtb)
	rm -f $(PWMCONFSRC:%.c=$(KBUILD)/%.o)

distclean:
	rm -rf $(BUILDDIR)
endif
