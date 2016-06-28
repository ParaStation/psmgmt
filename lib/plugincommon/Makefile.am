#
# ParaStation
#
# Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

noinst_LTLIBRARIES = libplugincommon.la

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

libplugincommon_la_SOURCES = plugincomm.c pluginconfig.c pluginfrag.c \
	pluginhelper.c pluginlog.c pluginmalloc.c pluginstrv.c        \
	pluginpty.c pluginpartition.c pluginhostlist.c pluginenv.c

libplugincommon_la_LIBADD = $(libpscommon) -lrt -lm
