CC?=gcc
EXEC=epd_test
SRC= epd_test.c epd_temp.c epd_mod.c pwm.c spi.c gpio.c i2c.c
INC=include-stub ..
OBJ= $(SRC:.c=.o)

CFLAGS= -O0 -g -DDEBUG=1 -W -Wall -std=c99 $(addprefix -I, $(INC))
LDFLAGS=

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

%.c: ../%.c
	cp $< $@

.PHONY: clean mrproper

clean:
	rm -rf *.o

distclean: clean
	rm -rf $(EXEC)

