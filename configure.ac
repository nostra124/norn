AC_PREREQ([2.69])
AC_INIT([norn], [0.1.0], [noreply@example.com])
AC_CONFIG_SRCDIR([libnorn/norn.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign -Wall -Werror])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
LT_INIT

# Checks for libraries.
PKG_CHECK_MODULES([SODIUM], [libsodium >= 1.0.0])

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

AC_CONFIG_FILES([Makefile norn.pc])
AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([norn $VERSION configuration:])
AC_MSG_NOTICE([  Prefix:          $prefix])
AC_MSG_NOTICE([  CC:              $CC])
AC_MSG_NOTICE([  CFLAGS:          $CFLAGS])
AC_MSG_NOTICE([  libsodium:       yes])
AC_MSG_NOTICE([])