#
# ParaStation
#
# Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

AC_PREREQ([2.68])

AC_INIT([ParaStation Management], [5],
  [support@par-tec.com], [psmgmt], [http://par-tec.com/])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([/opt/parastation])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign -Wall -Wno-portability silent-rules subdir-objects no-define])
AM_MAINTAINER_MODE
AM_SILENT_RULES([yes])

# enable runpath. this has to be set before LT_INIT.
LDFLAGS="$LDFLAGS -Wl,--enable-new-dtags"

# autoconf's CFLAGS default is "-O2 -g", this would override -O3 from
# AM_CFLAGS (see below), so change the default to "-g" only
CFLAGS="${CFLAGS-"-g"}"

AC_PROG_CC
AM_PROG_CC_C_O

LT_INIT([dlopen disable-static])

AC_CHECK_LIB([numa], [numa_available],
  [AC_SUBST([NUMA_LIBS], [-lnuma])
   AC_DEFINE([HAVE_LIBNUMA])]
   AC_CHECK_LIB([numa], [numa_allocate_nodemask],
     [AC_DEFINE([HAVE_NUMA_ALLOCATE_NODEMASK])]))

AC_CHECK_LIB([popt], [poptGetContext],
  [AC_SUBST([POPT_LIBS], [-lpopt])],
  [AC_MSG_ERROR([please install the popt devel package])])

PS_SEARCH_LIBS([initscr], [ncurses curses], [RL_LIBS], [],
  [AC_MSG_ERROR([please install the (n)curses devel package])])

PS_SEARCH_LIBS([readline], [readline], [RL_LIBS], [],
  [AC_MSG_ERROR([please install the readline devel package])])

PS_SEARCH_LIBS([using_history], [history], [RL_LIBS], [],
  [AC_MSG_ERROR([please install the readline devel package])])

AX_CHECK_COMPILE_FLAG([-O3],
  [AM_CFLAGS="$AM_CFLAGS -O3"])

AX_CHECK_COMPILE_FLAG([-Wall -Wextra -Wno-unused],
  [AM_CFLAGS="$AM_CFLAGS -Wall -Wextra -Wno-unused"])

AX_CHECK_COMPILE_FLAG([-fstack-protector-all],
  [AM_CFLAGS="$AM_CFLAGS -fstack-protector-all"])

AX_CHECK_COMPILE_FLAG([-fno-strict-aliasing],
  [AM_CFLAGS="$AM_CFLAGS -fno-strict-aliasing"])

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_ARG_ENABLE([testplugins], [AS_HELP_STRING([--enable-testplugins],
  [build test plugins])])
AM_CONDITIONAL([TESTPLUGINS_ENABLED], [test "x$enable_testplugins" = xyes])

PS_FIX_LIBDIR

AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES(bin/Makefile)
AC_CONFIG_FILES(doc/Makefile)
AC_CONFIG_FILES(include/Makefile)
AC_CONFIG_FILES(lib/Makefile)
AC_CONFIG_FILES(lib/pscommon/Makefile)
AC_CONFIG_FILES(lib/pse/Makefile)
AC_CONFIG_FILES(lib/psi/Makefile)
AC_CONFIG_FILES(lib/pslog/Makefile)
AC_CONFIG_FILES(lib/putil/Makefile)
AC_CONFIG_FILES(plugins/Makefile)
AC_CONFIG_FILES(plugins/psaccount/Makefile)
AC_CONFIG_FILES(plugins/psresport/Makefile)
AC_CONFIG_FILES(scripts/Makefile)
AC_CONFIG_FILES(tests/plugins/Makefile)
AC_CONFIG_FILES(tests/functional/Makefile)

AC_OUTPUT

# copy hand written Makefiles
if test ! ${srcdir}/dist/Makefile -ef dist/Makefile ; then
   install -D -p ${srcdir}/dist/Makefile dist/Makefile
fi
