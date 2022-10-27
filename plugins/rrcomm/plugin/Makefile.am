#
# ParaStation
#
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = rrcomm.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

rrcomm_la_SOURCES = rrcomm.c rrcommconfig.c rrcommconfig.h		\
	rrcommforwarder.c rrcommforwarder.h rrcommlog.c rrcommlog.h	\
	rrcommproto.h

rrcomm_la_CPPFLAGS = $(AM_CPPFLAGS)
rrcomm_la_LIBADD = $(libplugincommon) $(libpscommon)
