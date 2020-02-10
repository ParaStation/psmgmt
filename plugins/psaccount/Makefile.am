#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon
AM_CFLAGS += -fcommon

plugin_LTLIBRARIES = psaccount.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

psaccount_la_SOURCES = psaccount.c psaccountclient.c			\
	psaccountclient.h psaccountcomm.c psaccountcomm.h		\
	psaccountinter.c psaccountinter.h psaccountjob.c		\
	psaccountjob.h psaccountkvs.c psaccountkvs.h psaccountlog.c	\
	psaccountlog.h psaccountproc.c psaccountproc.h			\
	psaccountconfig.c psaccountconfig.h psaccounthistory.h		\
	psaccounthistory.c psaccounthandles.h psaccounttypes.h		\
	psaccountenergy.c psaccountenergy.h

psaccount_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"
psaccount_la_LIBADD = $(libplugincommon) $(libpscommon)
