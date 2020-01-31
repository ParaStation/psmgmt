#
# ParaStation
#
# Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

psslurm_dir   := plugins/psslurm/src
psaccount_dir := plugins/psaccount
pelogue_dir   := plugins/pelogue
psmunge_dir   := plugins/psmunge
pspam_dir     := plugins/pspam/pspam
psexec_dir    := plugins/psexec
pspmi_dir     := plugins/pspmi

AM_CFLAGS += -std=gnu99
AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
	       -I$(top_srcdir)/$(psslurm_dir)/common \
	       -I$(top_srcdir)/$(psslurm_dir)/psslurm \
	       -I$(top_srcdir)/$(psaccount_dir) \
	       -I$(top_srcdir)/$(pelogue_dir) \
	       -I$(top_srcdir)/$(psmunge_dir) \
	       -I$(top_srcdir)/$(pspam_dir) \
	       -I$(top_srcdir)/$(psexec_dir) \
	       -I$(top_srcdir)/$(pspmi_dir)


bin_PROGRAMS = psslurmgetpin


libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libslurmcommon = $(top_builddir)/$(psslurm_dir)/common/libslurmcommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

psslurmgetpin_SOURCES=\
	psslurmgetpin.c

psslurmgetpin_CFLAGS=\
	$(AM_CFLAGS)

psslurmgetpin_LDFLAGS=\
	$(AM_LDFLAGS)\
	$(NUMA_LIBS) -lm

psslurmgetpin_LDADD=\
	$(libpscommon)\
	$(libslurmcommon)\
	$(libplugincommon)\
	$(top_builddir)/$(psslurm_dir)/psslurm/.libs/psslurm_la-psslurmpin.o\
	$(libpsslurm)

