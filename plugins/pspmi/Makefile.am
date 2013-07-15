#
# ParaStation
#
# Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

libpskvs = $(top_builddir)/lib/pskvs/libpskvs.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = pspmi.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

pspmi_la_SOURCES = pmiclient.c pmiclient.h pmiforwarder.c \
	pmiforwarder.h pmilog.c pmilog.h pmimain.c pmimain.h \
	pmispawn.c pmispawn.h pmiservice.c pmiservice.h \
	pmiprovider.c pmiprovider.h

pspmi_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"

pspmi_la_LIBADD = $(libpskvs) $(libpsi) $(libplugincommon)
