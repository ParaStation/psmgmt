#
# ParaStation
#
# Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = psresport.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

psresport_la_SOURCES = psresport.c psresport.h psresportlog.c		\
	psresportlog.h psresportconfig.c psresportconfig.h

psresport_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"
psresport_la_LIBADD = $(libplugincommon)
