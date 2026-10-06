CC ?= gcc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../include

all: edu_cli

edu_cli: edu_cli.c ../include/edu_ioctl.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ edu_cli.c

clean:
	rm -f edu_cli

.PHONY: all clean

