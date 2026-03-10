#
# ParaStation
#
# Copyright (C) 2026 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include

plugin_LTLIBRARIES = qproxy.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

qproxy_la_SOURCES = qproxy.c qproxyconfig.c qproxyconfig.h	\
	qproxylog.c qproxylog.h

qproxy_la_CPPFLAGS = $(AM_CPPFLAGS)
qproxy_la_LIBADD = $(libplugincommon) $(libpscommon)
