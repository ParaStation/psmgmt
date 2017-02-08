#
# ParaStation
#
# Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = psexec.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

psexec_la_SOURCES = psexec.c psexeclog.c psexeclog.h psexecscripts.c	\
	psexecscripts.h psexeccomm.c psexeccomm.h psexecinter.c		\
	psexecinter.h psexechandles.h psexectypes.h

psexec_la_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"
psexec_la_LIBADD = $(libplugincommon) $(libpscommon)
