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

plugin_LTLIBRARIES = psaccount.la

psaccount_la_SOURCES = helper.c helper.h psaccount.c			\
	 psaccountclient.c psaccountclient.h psaccountcollect.c		\
	 psaccountcollect.h psaccountcomm.c psaccountcomm.h		\
	 psaccount.h psaccountinter.c psaccountinter.h psaccountjob.c	\
	 psaccountjob.h psaccountkvs.c psaccountkvs.h psaccountlog.c	\
	 psaccountlog.h psaccountproc.c psaccountproc.h			\
	 psaccountconfig.c psaccountconfig.h psaccounthistory.h 	\
	 psaccounthistory.c

psaccount_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"
