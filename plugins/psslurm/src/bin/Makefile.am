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


bin_PROGRAMS = psslurmgetbind

psslurmgetbind_SOURCES=\
	psslurmgetbind.c

psslurmgetbind_CPPFLAGS=\
	$(AM_CPPFLAGS)\
	-DPLUGINDIR=\"$(plugindir)\"\
	-DHAVE_NUMA_ALLOCATE_NODEMASK

psslurmgetbind_CFLAGS=\
	$(AM_CFLAGS)

psslurmgetbind_LDFLAGS=\
	$(AM_LDFLAGS)\
	-lm

psslurmgetbind_LDADD=\
	$(top_builddir)/$(psslurm_dir)/psslurm/.libs/psslurm_la-psslurmpin.o\
	$(top_builddir)/lib/plugincommon/.libs/pluginmalloc.o\
	$(top_builddir)/lib/pscommon/.libs/pscpu.o\
	$(top_builddir)/lib/plugincommon/.libs/pluginconfig.o\
	$(top_builddir)/lib/pscommon/.libs/psenv.o \
	$(top_builddir)/lib/pscommon/.libs/pscommon.o
