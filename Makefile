LDFLAGS = -lkrun
CFLAGS = -O2

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

ifeq ($(CC),)
    CC := gcc
endif

.PHONY: clean

all: krun krun-guest

krun: krun.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

krun-guest: krun-guest.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf krun krun-guest

install:
	install -m 755 krun $(PREFIX)/bin
	install -m 755 krun-guest $(PREFIX)/bin
