#
# ParaStation
#
# Copyright (C) 2013-2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

noinst_LTLIBRARIES = libplugincommon.la

libplugincommon_la_SOURCES = pluginconfig.c pluginhelper.c	\
	pluginlog.c pluginmalloc.c pluginstrv.c pluginpty.c	\
	pluginpartition.c pluginforwarder.c pluginvector.c

pluginforwarder.lo: CPPFLAGS = $(AM_CPPFLAGS) \
			-DLOCALSTATEDIR=\"$(localstatedir)\"
