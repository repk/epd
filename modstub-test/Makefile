CC?=gcc
EXEC=epd_test
SRC=									\
	init.c								\
	epd_test.c							\
	epd_temp.c							\
	pwm.c								\
	spi.c								\
	gpio.c								\
	i2c.c								\
	core.c								\
	char_dev.c							\
	drv-core.c							\
	drv-epd_g1.c
INC=include-stub ..
OBJ= $(SRC:.c=.o)

CFLAGS= -O0 -g -DDEBUG=1 -D_BSD_SOURCE -W -Wall -Wno-unused-variable	\
	-Wno-unused-parameter -Wno-cast-qual -std=c99 $(addprefix -I, $(INC))
LDFLAGS=

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

drv-%.c: ../%.c
	cp $< $@

.PHONY: clean mrproper

clean:
	rm -rf *.o

distclean: clean
	rm -rf $(EXEC)

