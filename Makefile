#
# Greybus Simulator
#
# Copyright 2014 Google Inc.
# Copyright 2014 Linaro Ltd.
#
# Provided under the three clause BSD license found in the LICENSE file.
#

# Uncomment or set env to cross compile
#CROSS_COMPILE = arm-linux-gnueabi-
 
# Location of Greybus kernel headers
GBDIR = /home/mporter/src/greybus

# Install directory
INSTALLDIR = /usr/local/bin

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -g
INCS = -I$(GBDIR)
LFLAGS = 
LIBS = -lpthread -lusbg

SRCS = main.c gadget.c functionfs.c inotify.c
OBJS = $(SRCS:.c=.o)
MAIN = gbsim

.PHONY: depend clean

all:    $(MAIN)

$(MAIN): $(OBJS) 
	$(CC) $(CFLAGS) $(INCS) -o $(MAIN) $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCS) -c $<  -o $@

clean:
	$(RM) *.o *~ $(MAIN)

depend: $(SRCS)
	makedepend $(INCLUDES) $^

install: $(MAIN)
	install $(MAIN) $(INSTALLDIR)

# make depend magic
