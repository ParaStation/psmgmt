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

psslurm_dir   := plugins/psslurm/src
psaccount_dir := plugins/psaccount
pelogue_dir   := plugins/pelogue
psmunge_dir   := plugins/psmunge

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
	       -I$(top_srcdir)/$(psslurm_dir)/common \
	       -I$(top_srcdir)/$(psaccount_dir) \
	       -I$(top_srcdir)/$(pelogue_dir) \
	       -I$(top_srcdir)/$(psmunge_dir)

plugin_LTLIBRARIES = psslurm.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libslurmcommon = $(top_builddir)/$(psslurm_dir)/common/libslurmcommon.la

psslurm_la_SOURCES = psslurm.c psslurm.h psslurmlog.c psslurmlog.h \
	psslurmpscomm.c psslurmpscomm.h psslurmconfig.c psslurmconfig.h \
	psslurmjob.c psslurmjob.h psslurmforwarder.c psslurmforwarder.h \
	psslurmcomm.c psslurmcomm.h psslurmauth.c psslurmauth.h \
	psslurmproto.c psslurmproto.h psslurmenv.c psslurmenv.h \
	psslurmjobinfo.c psslurmjobinfo.h psslurmlimits.c psslurmlimits.h \
	psslurmmultiprog.c psslurmmultiprog.h psslurmgres.c psslurmgres.h \
	psslurmpin.c psslurmpin.h

psslurm_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"

psslurm_la_LIBADD = $(libpsi) $(libplugincommon) $(libslurmcommon)
