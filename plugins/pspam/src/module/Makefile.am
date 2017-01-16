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

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include

pam_LTLIBRARIES = pam_pspam.la

plugincommon_objs = $(top_builddir)/lib/plugincommon/plugincomm.lo
plugincommon_objs += $(top_builddir)/lib/plugincommon/pluginmalloc.lo
plugincommon_objs += $(top_builddir)/lib/plugincommon/pluginlog.lo
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

pam_pspam_la_SOURCES = pam_module.c

pam_pspam_la_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"

pam_pspam_la_LIBADD = $(libpscommon) $(plugincommon_objs) -lpam
