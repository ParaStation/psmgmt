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

psaccount_dir := plugins/psaccount

libpskvs = $(top_builddir)/lib/pskvs/libpskvs.la
libpsi = $(top_builddir)/lib/psi/libpsi.la


AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon \
	       -I$(top_srcdir)/$(psaccount_dir)

plugin_LTLIBRARIES = pspmi.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

pspmi_la_SOURCES = pmiclient.c pmiclient.h pmiforwarder.c		\
	pmiforwarder.h pmilog.c pmilog.h pmimain.c pmispawn.c		\
	pmispawn.h pmiservice.c pmiservice.h pmikvs.h pmikvs.c		\
	pmiclientspawn.c pmiclientspawn.h pmitypes.h pspmihandles.h

pspmi_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DPKGLIBEXECDIR=\"$(pkglibexecdir)\"

pspmi_la_LIBADD = $(libpskvs) $(libpsi) $(libplugincommon)
