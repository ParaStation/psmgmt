#
# ParaStation
#
# Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

psaccount_dir := plugins/psaccount

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
	       -I$(top_srcdir)/$(psaccount_dir)

plugin_LTLIBRARIES = pspam.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

pspam_la_SOURCES = pspam.c pspam.h pspamlog.c pspamlog.h pspamssh.c pspamssh.h \
			pspamuser.c pspamuser.h pspamhandles.h

pspam_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
        -DLOCALSTATEDIR=\"$(localstatedir)\"

pspam_la_LIBADD = $(libplugincommon)
