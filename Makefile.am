AM_LDFLAGS = \
	-Wl,--as-needed

GBUS_CPPFLAGS = -I${GBDIR}

data_DATA =

bin_PROGRAMS = \
	gbsim

gbsim_SOURCES = \
	config.h \
	connection.c \
	firmware.c \
	functionfs.c \
	gadget.c \
	gbsim.h \
	gpio.c \
	control.c \
	svc.c \
	i2c.c \
	inotify.c \
	loopback.c \
	main.c \
	manifest.c \
	pwm.c \
	sdio.c \
	light.c \
	uart.c

gbsim_CPPFLAGS = \
	-Wall \
	-Wformat \
	-Wshadow \
	$(GBUS_CPPFLAGS) \
	$(AM_CPPFLAGS) \
	$(SOC_CFLAGS) \
	$(USBG_CFLAGS)

gbsim_LDADD = \
	$(SOC_LIBS) \
	$(USBG_LIBS)


distclean-local:
	rm -rf autom4te.cache
