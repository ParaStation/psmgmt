#
# ParaStation
#
# Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

SUBDIRS = doc

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = nodeinfo.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

nodeinfo_la_SOURCES = nodeinfo.c nodeinfo.h nodeinfoconfig.c		\
	nodeinfoconfig.h nodeinfohandles.h nodeinfointer.c		\
	nodeinfointer.h nodeinfolog.c nodeinfolog.h nodeinfotypes.h

nodeinfo_la_CPPFLAGS = $(AM_CPPFLAGS)
nodeinfo_la_LIBADD = $(libplugincommon) $(libpscommon)
