#
# ParaStation
#
# Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

#psaccount_dir := plugins/psaccount

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon
#AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon \
#	       -I$(top_srcdir)/$(psaccount_dir)
AM_CFLAGS += -std=gnu99
AM_LDFLAGS += -avoid-version -module

plugin_LTLIBRARIES = pspmix.la

pspmix_la_SOURCES = pspmixmain.c pspmixtypes.h \
		    pspmixlog.c pspmixlog.h \
		    pspmixcommon.c pspmixcommon.h \
		    pspmixdaemon.c pspmixdaemon.h \
		    pspmixforwarder.c pspmixforwarder.h \
		    pspmixjobserver.c pspmixjobserver.h \
		    pspmixcomm.c pspmixcomm.h \
		    pspmixservice.c pspmixservice.h \
		    pspmixservicespawn.c pspmixservicespawn.h \
		    pspmixserver.c pspmixserver.h

pspmix_la_CPPFLAGS = $(AM_CPPFLAGS) \
		     -DLIBEXECDIR=\"$(pkglibexecdir)\"

pspmix_la_CFLAGS = $(AM_CFLAGS) $(pmix_CFLAGS)
pspmix_la_LIBADD = $(pmix_LIBS) $(libplugincommon) $(libpscommon)
