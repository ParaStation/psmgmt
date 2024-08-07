#
# ParaStation
#
# Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon
AM_CFLAGS += -std=gnu99
AM_LDFLAGS += -avoid-version -module

plugin_LTLIBRARIES = pspmix.la

pspmix_la_SOURCES = pspmixmain.c pspmixtypes.h pspmixlog.c		\
	pspmixlog.h pspmixconfig.c pspmixconfig.h pspmixcommon.c	\
	pspmixcommon.h pspmixdaemon.c pspmixdaemon.h			\
	pspmixforwarder.c pspmixforwarder.h pspmixuserserver.c		\
	pspmixuserserver.h pspmixcomm.c pspmixcomm.h pspmixservice.c	\
	pspmixservice.h pspmixserver.c pspmixserver.h pspmixutil.c	\
	pspmixutil.h pspmixhandles.h

pspmix_la_CPPFLAGS = $(AM_CPPFLAGS) \
		     -DPKGLIBEXECDIR=\"$(pkglibexecdir)\" \
		     -DPLUGINDIR=\"$(plugindir)\"

pspmix_la_CFLAGS = $(AM_CFLAGS) $(pmix_CFLAGS)
pspmix_la_LIBADD = $(pmix_LIBS) $(libplugincommon) $(libpscommon) $(libpsi)

dist_plugin_DATA = pspmix.conf

EXTRA_DIST = slurm/mpi_pspmix.c tests/pmix_test_putget.c
