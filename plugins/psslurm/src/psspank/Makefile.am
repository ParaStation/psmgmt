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

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
	       -I$(top_srcdir)/plugins/psslurm/src/common

slurm_LTLIBRARIES = psspank.la

plugincommon_objs = $(top_builddir)/lib/plugincommon/pluginfrag.lo
plugincommon_objs += $(top_builddir)/lib/plugincommon/plugincomm.lo
plugincommon_objs += $(top_builddir)/lib/plugincommon/pluginmalloc.lo
plugincommon_objs += $(top_builddir)/lib/plugincommon/pluginlog.lo
libpsi = $(top_builddir)/lib/psi/libpsi.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
#libputil = $(top_builddir)/lib/putil/libputil.la

psspank_la_SOURCES = psspank.c

psspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"

psspank_la_LIBADD = $(libpsi) $(libpslog) $(plugincommon_objs) $(libpscommon) \
			-lrt -lm
