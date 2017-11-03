#
# ParaStation
#
# Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
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

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
	       -I$(top_srcdir)/$(psslurm_dir)/common \
	       -I$(top_srcdir)/$(psaccount_dir) \
	       -I$(top_srcdir)/$(pelogue_dir) \
	       -I$(top_srcdir)/$(psmunge_dir) \
	       -I$(top_srcdir)/$(pspam_dir) \
	       -I$(top_srcdir)/$(psexec_dir) \
	       -I$(top_srcdir)/$(pspmi_dir)

plugin_LTLIBRARIES = psslurm.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libslurmcommon = $(top_builddir)/$(psslurm_dir)/common/libslurmcommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

psslurm_la_SOURCES = psslurm.c psslurm.h psslurmlog.c psslurmlog.h	\
	psslurmpscomm.c psslurmpscomm.h psslurmconfig.c			\
	psslurmconfig.h psslurmjob.c psslurmjob.h psslurmforwarder.c	\
	psslurmforwarder.h psslurmcomm.c psslurmcomm.h psslurmauth.c	\
	psslurmauth.h psslurmproto.c psslurmproto.h psslurmenv.c	\
	psslurmenv.h psslurmjobinfo.c psslurmjobinfo.h			\
	psslurmlimits.c psslurmlimits.h psslurmmultiprog.c		\
	psslurmmultiprog.h psslurmgres.c psslurmgres.h psslurmpin.c	\
	psslurmpin.h psslurmio.c psslurmio.h psslurmpelogue.h		\
	psslurmpelogue.c psslurmkvs.c psslurmspawn.c psslurmspawn.h     \
	psslurmpack.h psslurmpack.c psslurmtypes.h psslurminter.c 	\
	psslurminter.h psslurmhandles.h  psslurmmsg.h psslurmmsg.c      \
	psslurmtasks.h psslurmtasks.c psslurmjobcred.h

psslurm_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"	\
	-DLOCALSTATEDIR=\"$(localstatedir)\"				\
	-DLIBEXECDIR=\"$(pkglibexecdir)\" -DBINDIR=\"$(bindir)\"


psslurm_la_LIBADD = $(libpsi) $(libplugincommon) $(libslurmcommon) \
		    $(libpscommon) $(NUMA_LIBS) -lm

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/jobs
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/nodefiles
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/temp
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/undelivered
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/account
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/backup
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/scripts
