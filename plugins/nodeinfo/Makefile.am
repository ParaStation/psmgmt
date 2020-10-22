#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = nodeinfo.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

nodeinfo_la_SOURCES = nodeinfo.c nodeinfoconfig.c nodeinfoconfig.h	\
	nodeinfolog.c nodeinfolog.h

nodeinfo_la_CPPFLAGS = $(AM_CPPFLAGS)
nodeinfo_la_LIBADD = $(libplugincommon) $(libpscommon)
