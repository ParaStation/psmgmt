#
# ParaStation
#
# Copyright (C) 2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = doc

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = dynIP.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

dynIP_la_SOURCES = dynIP.c dynIPlog.c dynIPlog.h

dynIP_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"
dynIP_la_LIBADD = $(libplugincommon) $(libpscommon)
