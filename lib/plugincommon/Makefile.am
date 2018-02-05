#
# ParaStation
#
# Copyright (C) 2013-2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

noinst_LTLIBRARIES = libplugincommon.la

libplugincommon_la_SOURCES = psserial.c pluginconfig.c pluginhelper.c   \
	pluginlog.c pluginmalloc.c pluginstrv.c	pluginpty.c 		\
	pluginpartition.c pluginhostlist.c pluginenv.c			\
	pluginforwarder.c

pluginforwarder.lo: CPPFLAGS = $(AM_CPPFLAGS) \
			-DLOCALSTATEDIR=\"$(localstatedir)\"

libplugincommon_la_LIBADD = -lrt -lm
