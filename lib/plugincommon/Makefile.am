#
# ParaStation
#
# Copyright (C) 2013-2014 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

noinst_LTLIBRARIES = libplugincommon.la

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

libplugincommon_la_SOURCES = plugincomm.c pluginfrag.c pluginlog.c \
	pluginmalloc.c

libplugincommon_la_LIBADD = $(libpscommon) -lrt -lm
