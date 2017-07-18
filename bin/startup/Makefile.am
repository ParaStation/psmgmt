#
# ParaStation
#
# Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt version -n)
PKG_REL := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt release -n)

AM_CPPFLAGS += -DVERSION_psmgmt=\"$(PKG_VER)\"
AM_CPPFLAGS += -DRELEASE_psmgmt=\"$(PKG_REL)\"

bin_PROGRAMS = mpiexec
pkglibexec_PROGRAMS = kvsprovider spawner

noinst_LTLIBRARIES = libstartupcommon.la
libstartupcommon_la_SOURCES = cloptions.c common.c

libpse = $(top_builddir)/lib/pse/libpse.la
libpskvs = $(top_builddir)/lib/pskvs/libpskvs.la

mpiexec_SOURCES = mpiexec.c
mpiexec_CPPFLAGS = $(AM_CPPFLAGS) -DLIBEXECDIR=\"$(pkglibexecdir)\"
mpiexec_LDADD = libstartupcommon.la $(libpse) $(POPT_LIBS)

kvsprovider_SOURCES = kvsprovider.c
kvsprovider_CPPFLAGS = $(AM_CPPFLAGS) -DLIBEXECDIR=\"$(pkglibexecdir)\"
kvsprovider_LDADD = libstartupcommon.la $(libpse) $(libpskvs) $(POPT_LIBS)

spawner_SOURCES = spawner.c
spawner_CPPFLAGS = $(AM_CPPFLAGS) -DCONFIGDIR=\"$(configdir)\"
spawner_LDADD = libstartupcommon.la $(libpse) $(POPT_LIBS)
