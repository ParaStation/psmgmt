#
# ParaStation
#
# Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon
AM_LDFLAGS += $(glib2_LDFLAGS)

noinst_LTLIBRARIES = libplugincommon.la

libplugincommon_la_SOURCES = pluginconfig.c pluginforwarder.c		\
	pluginhelper.c pluginlog.c pluginmalloc.c pluginpartition.c	\
	pluginpsconfig.c pluginpty.c pluginstrv.c pluginvector.c

pluginpsconfig.lo: AM_CFLAGS += $(glib2_CFLAGS)

pluginforwarder.lo: CPPFLAGS = $(AM_CPPFLAGS) \
			-DLOCALSTATEDIR=\"$(localstatedir)\"
