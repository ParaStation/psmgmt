#
# ParaStation
#
# Copyright (C) 2012-2019 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

AC_PREREQ([2.68])

AC_INIT([ParaStation Management], [5],
  [support@par-tec.com], [psmgmt], [http://par-tec.com/])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign -Wall -Wno-portability silent-rules subdir-objects no-define])
AM_MAINTAINER_MODE
AM_SILENT_RULES([yes])

# enable runpath. this has to be set before LT_INIT.
LDFLAGS="$LDFLAGS -Wl,--enable-new-dtags"

# autoconf's CFLAGS default is "-O2 -g", this would override -O3 from
# AM_CFLAGS (see below), so change the default to "-g" only
CFLAGS="${CFLAGS-"-g"}"

AC_PROG_LN_S

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

AC_CHECK_LIB([pam], [pam_open_session],
  [AC_SUBST([PAM_LIBS], [-lpam])
   AC_DEFINE([HAVE_PAM_DEVEL])
   [ pam_enabled=true ] ])
AM_CONDITIONAL([PAM_ENABLED], [test "x$pam_enabled" = xtrue])

AM_COND_IF([PAM_ENABLED], [
  AC_ARG_WITH([pamdir], [AS_HELP_STRING([--with-pamdir=DIR],
    [specify pam plugin dir])],
    AS_IF([test "x$withval" = xyes -o "x$withval" = no],
	  [AC_MSG_ERROR([missing arg to --with-pamdir])],
	  [pamdir="$withval"]),
    [pamdir=/lib/security])
  AC_SUBST([pamdir])
], [])

AC_CHECK_LIB([munge], [munge_encode],
  [AC_SUBST([MUNGE_LIBS], [-lmunge])
   AC_DEFINE([HAVE_MUNGE_DEVEL])
   [ munge_enabled=true ] ])
AM_CONDITIONAL([MUNGE_ENABLED], [test "x$munge_enabled" = xtrue])

AC_ARG_ENABLE([spank], [AS_HELP_STRING([--disable-spank],
  [build without spank support])])
AM_CONDITIONAL([BUILD_WITHOUT_SPANK], [test "x$enable_spank" = xno])

AM_COND_IF([BUILD_WITHOUT_SPANK], [
  AC_MSG_NOTICE([Building without spank support])
  AM_CFLAGS="$AM_CFLAGS -DBUILD_WITHOUT_SPANK"
], [
  AC_CHECK_HEADERS([slurm/spank.h],
    [ AC_DEFINE([HAVE_SPANK])
     [ spank_enabled=true ] ])
])
AM_CONDITIONAL([SPANK_ENABLED], [test "x$spank_enabled" = xtrue])

AC_ARG_ENABLE([psconfig], [AS_HELP_STRING([--disable-psconfig],
  [build without psconfig support])])
AM_CONDITIONAL([BUILD_WITHOUT_PSCONFIG], [test "x$enable_psconfig" = xno])

AM_COND_IF([BUILD_WITHOUT_PSCONFIG], [
  AC_MSG_NOTICE([Building without psconfig support])
  AM_CFLAGS="$AM_CFLAGS -DBUILD_WITHOUT_PSCONFIG"
], [
  PKG_CHECK_MODULES([glib2], [glib-2.0 >= 2.22.0])
  PKG_CHECK_MODULES([psconfig], [psconfig])
])

AC_ARG_ENABLE([pspmix], [AS_HELP_STRING([--disable-pspmix],
  [do not build pspmix plugin])])
AM_CONDITIONAL([BUILD_WITHOUT_PSPMIX], [test "x$enable_pspmix" = xno])

AM_COND_IF([BUILD_WITHOUT_PSPMIX], [
  AC_MSG_NOTICE([Not building pspmix plugin])
], [
  PKG_CHECK_MODULES([pmix], [pmix >= 3.1.3])
])

AX_CHECK_COMPILE_FLAG([-O3],
  [AM_CFLAGS="$AM_CFLAGS -O3"])

AX_CHECK_COMPILE_FLAG([-Wall -Wextra -Wno-unused-parameter],
  [AM_CFLAGS="$AM_CFLAGS -Wall -Wextra -Wno-unused-parameter"])

AX_CHECK_COMPILE_FLAG([-fstack-protector-all],
  [AM_CFLAGS="$AM_CFLAGS -fstack-protector-all"])

AX_CHECK_COMPILE_FLAG([-fno-strict-aliasing],
  [AM_CFLAGS="$AM_CFLAGS -fno-strict-aliasing"])

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_ARG_ENABLE([testplugins], [AS_HELP_STRING([--enable-testplugins],
  [build test plugins])])
AM_CONDITIONAL([TESTPLUGINS_ENABLED], [test "x$enable_testplugins" = xyes])

PKG_PROG_PKG_CONFIG
AC_ARG_WITH([unitdir], [AS_HELP_STRING([--with-unitdir=DIR],
  [directory for systemd service files])],,[with_unitdir=auto])
AS_IF([test "x$with_unitdir" = "xyes" -o "x$with_unitdir" = "xauto"],
  [def_unitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
   AS_IF([test "x$def_unitdir" = "x"],
     [AS_IF([test "x$with_unitdir" = "xyes"],
       [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
      with_unitdir=no],
     [with_unitdir="$def_unitdir"])])
AS_IF([test "x$with_unitdir" != "xno"],
  [AC_SUBST([unitdir], [$with_unitdir])])
AM_CONDITIONAL([HAVE_SYSTEMD], [test "x$with_unitdir" != "xno"])

AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES(bin/Makefile)
AC_CONFIG_FILES(bin/admin/Makefile)
AC_CONFIG_FILES(bin/daemon/Makefile)
AC_CONFIG_FILES(bin/logger/Makefile)
AC_CONFIG_FILES(bin/startup/Makefile)
AC_CONFIG_FILES(doc/Makefile)
AC_CONFIG_FILES(include/Makefile)
AC_CONFIG_FILES(lib/Makefile)
AC_CONFIG_FILES(lib/plugincommon/Makefile)
AC_CONFIG_FILES(lib/pscommon/Makefile)
AC_CONFIG_FILES(lib/pse/Makefile)
AC_CONFIG_FILES(lib/psi/Makefile)
AC_CONFIG_FILES(lib/pskvs/Makefile)
AC_CONFIG_FILES(lib/pslog/Makefile)
AC_CONFIG_FILES(lib/putil/Makefile)
AC_CONFIG_FILES(plugins/Makefile)
AC_CONFIG_FILES(plugins/cgroup/Makefile)
AC_CONFIG_FILES(plugins/jail/Makefile)
AC_CONFIG_FILES(plugins/pelogue/Makefile)
AC_CONFIG_FILES(plugins/psaccount/Makefile)
AC_CONFIG_FILES(plugins/psexec/Makefile)
AC_CONFIG_FILES(plugins/psgw/Makefile)
AC_CONFIG_FILES(plugins/psgw/doc/Makefile)
AC_CONFIG_FILES(plugins/psgw/route/Makefile)
AC_CONFIG_FILES(plugins/psgw/spank/Makefile)
AC_CONFIG_FILES(plugins/psmom/Makefile)
AC_CONFIG_FILES(plugins/psmom/doc/Makefile)
AC_CONFIG_FILES(plugins/psmom/include/Makefile)
AC_CONFIG_FILES(plugins/psmom/pbs/Makefile)
AC_CONFIG_FILES(plugins/psmom/psmom/Makefile)
AC_CONFIG_FILES(plugins/psmunge/Makefile)
AC_CONFIG_FILES(plugins/pspam/Makefile)
AC_CONFIG_FILES(plugins/pspam/include/Makefile)
AC_CONFIG_FILES(plugins/pspam/module/Makefile)
AC_CONFIG_FILES(plugins/pspam/pspam/Makefile)
AC_CONFIG_FILES(plugins/pspmi/Makefile)

AM_COND_IF([BUILD_WITHOUT_PSPMIX], [], [
  AC_CONFIG_FILES(plugins/pspmix/Makefile)
])

AC_CONFIG_FILES(plugins/psresport/Makefile)
AC_CONFIG_FILES(plugins/psslurm/Makefile)
AC_CONFIG_FILES(plugins/psslurm/doc/Makefile)
AC_CONFIG_FILES(plugins/psslurm/src/common/Makefile)
AC_CONFIG_FILES(plugins/psslurm/src/psslurm/Makefile)
AC_CONFIG_FILES(plugins/psslurm/src/spank/Makefile)
AC_CONFIG_FILES(scripts/Makefile)
AC_CONFIG_FILES(tests/plugins/Makefile)
AC_CONFIG_FILES(tests/functional/Makefile)
AC_CONFIG_LINKS(dist/Makefile:dist/Makefile)
AC_CONFIG_COMMANDS([mkdir],[$MKDIR_P scripts/init.d scripts/xinetd.d scripts/systemd])

AC_OUTPUT
