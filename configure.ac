AC_PREREQ([2.69])
AC_INIT([norn], m4_esyscmd_s([cat VERSION]), [noreply@example.com])
AC_CONFIG_SRCDIR([src/libnorn/norn.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_DEFINE_UNQUOTED([NORN_VERSION], ["$PACKAGE_VERSION"], [norn version string])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
LT_INIT

# Checks for libraries.
PKG_CHECK_MODULES([SODIUM], [libsodium >= 1.0.0])

# Optional Bonjour/mDNS support via Avahi
PKG_CHECK_MODULES([AVAHI], [avahi-client >= 0.6], [have_avahi=yes], [have_avahi=no])
AS_IF([test "x$have_avahi" = "xyes"], [
    AC_DEFINE([HAVE_AVAHI], [1], [Define if Avahi is available for Bonjour])
])

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Coverage support
AC_ARG_ENABLE([coverage],
              [AS_HELP_STRING([--enable-coverage],
                              [Enable code coverage testing @<:@default=no@:>@])],
              [enable_coverage=$enableval],
              [enable_coverage=no])
AM_CONDITIONAL([ENABLE_COVERAGE], [test "x$enable_coverage" = "xyes"])

AS_IF([test "x$enable_coverage" = "xyes"],
      [CFLAGS="$CFLAGS --coverage -O0"])

AC_CONFIG_FILES([Makefile norn.pc
                 man/norn.1 man/nornd.8 man/libnorn.3
                 Doxyfile])
AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([norn $VERSION configuration:])
AC_MSG_NOTICE([  Prefix:          $prefix])
AC_MSG_NOTICE([  CC:              $CC])
AC_MSG_NOTICE([  CFLAGS:          $CFLAGS])
AC_MSG_NOTICE([  libsodium:       yes])
AC_MSG_NOTICE([])