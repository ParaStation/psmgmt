#
# ParaStation
#
# Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = scripts helpers config

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = jail.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

jail_la_SOURCES = jail.c jailconfig.c jailconfig.h jaillog.c	\
	jaillog.h jailtypes.h jailhandles.h

jail_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"	\
	-DJAILLIBDIR=\"$(jaillibdir)\"
jail_la_LIBADD = $(libplugincommon) $(libpscommon)

plugin_DATA = jail.conf

EXTRA_DIST = jail.conf.in

# clean up generated files
CLEANFILES = jail.conf
