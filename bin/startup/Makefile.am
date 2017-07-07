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
# libexec_PROGRAMS = kvsprovider spawner # to be used later
pkglibexec_PROGRAMS = spawner

libpse = $(top_builddir)/lib/pse/libpse.la
libpskvs = $(top_builddir)/lib/pskvs/libpskvs.la

mpiexec_SOURCES = mpiexec.c cloptions.c	cloptions.h environment.c environment.h
mpiexec_CPPFLAGS = $(AM_CPPFLAGS) -DLIBEXECDIR=\"$(pkglibexecdir)\"
mpiexec_LDADD = $(libpse) $(libpskvs) $(POPT_LIBS)

spawner_SOURCES = spawner.c cloptions.c cloptions.h environment.c environment.h
spawner_CPPFLAGS = $(AM_CPPFLAGS) -DCONFIGDIR=\"$(configdir)\"
spawner_LDADD = $(libpse) $(libpskvs) $(POPT_LIBS)
