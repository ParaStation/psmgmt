#
# ParaStation
#
# Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include

plugin_LTLIBRARIES = psexec.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

psexec_la_SOURCES = psexec.c psexec.h psexeclog.c psexeclog.h psexecscripts.c psexecscripts.h \
			psexeccomm.c psexeccomm.h psexecinter.c psexecinter.h psexechandles.h

psexec_la_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"

psexec_la_LIBADD = $(libplugincommon)
