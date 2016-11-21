#
# ParaStation
#
# Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = psmunge.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

psmunge_la_SOURCES = psmunge.c psmungelog.c psmungelog.h	\
	psmungetypes.h psmungeinter.h psmungeinter.c psmungehandles.h

psmunge_la_LIBADD = -lmunge $(libplugincommon)
