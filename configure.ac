#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([gbsim],
	[0.1],
	[https://github.com/projectara/gbsim])

AC_PREREQ([2.60])
AM_INIT_AUTOMAKE([check-news foreign 1.9 dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_USE_SYSTEM_EXTENSIONS

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB([pthread], [main])
PKG_CHECK_MODULES(SOC, libsoc)
PKG_CHECK_MODULES(USBG, libusbg)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h sys/mount.h unistd.h])

# Checks for library functions.
AC_CHECK_FUNCS([memset mkdir strerror strtol])

AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
])

AC_ARG_ENABLE(legacy-descriptors,
[AS_HELP_STRING([--enable-legacy-descriptors],
		[Use the legacy (pre-V2) USB descriptor format])],
[if test x$enableval = xyes; then
  AC_DEFINE(GBSIM_LEGACY_DESCRIPTORS, [],
	  [Use deprecated functionfs descriptors])
fi])

AC_OUTPUT

AC_MSG_RESULT([
	gbsim $VERSION
	=============

	prefix:                 ${prefix}
	datarootdir:            ${datarootdir}
	datadir:                ${datadir}
	mandir:                 ${mandir}

	compiler:               ${CC}
	cflags:                 ${CFLAGS}
	ldflags:                ${LDFLAGS}

])

