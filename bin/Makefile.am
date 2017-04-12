#
# ParaStation
#
# Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt version -n)
PKG_REL := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt release -n)

AM_CPPFLAGS += -DVERSION_psmgmt=\"$(PKG_VER)\"
AM_CPPFLAGS += -DRELEASE_psmgmt=\"$(PKG_REL)\"

bin_PROGRAMS = psiadmin psid psilogger mlisten psmstart mpirun_chp4	\
	psispawn mpirun_chgm gmspawner mpirun_openib test_pse		\
	test_config pssh psaccounter mpiexec
# libexec_PROGRAMS = kvsprovider spawner # to be used later
noinst_PROGRAMS = testacct

noinst_HEADERS = admin/helpmsgs.c

include_HEADERS = daemon/psidcomm.h daemon/psidhook.h			\
	daemon/psidnodes.h daemon/psidplugin.h daemon/psidscripts.h	\
	daemon/psidstatus.h daemon/psidtask.h daemon/psidutil.h

libpse = $(top_builddir)/lib/pse/libpse.la
libpsi = $(top_builddir)/lib/psi/libpsi.la
libputil = $(top_builddir)/lib/putil/libputil.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la
libpskvs = $(top_builddir)/lib/pskvs/libpskvs.la

psiadmin_SOURCES = admin/psiadmin.c admin/psiadmin.h admin/commands.c	\
	admin/commands.h admin/adminparser.c admin/adminparser.h
psiadmin_LDADD = $(libputil) $(libpse) $(POPT_LIBS)

psid_SOURCES = daemon/psidaccount.c daemon/psidaccount.h		\
	daemon/psidamd.c daemon/psidamd.h daemon/psid.c			\
	daemon/psidclient.c daemon/psidclient.h daemon/psidcomm.c	\
	daemon/psidcomm.h daemon/psidenv.c daemon/psidenv.h		\
	daemon/psidforwarder.c daemon/psidforwarder.h			\
	daemon/psidhook.c daemon/psidhook.h daemon/psidhw.c		\
	daemon/psidhw.h daemon/psidinfo.c daemon/psidinfo.h		\
	daemon/psidmsgbuf.c daemon/psidmsgbuf.h daemon/psidnodes.c	\
	daemon/psidnodes.h daemon/psidoption.c daemon/psidoption.h	\
	daemon/psidpartition.c daemon/psidpartition.h			\
	daemon/psidplugin.c daemon/psidplugin.h daemon/psidppc.c	\
	daemon/psidppc.h daemon/psidrdp.c daemon/psidrdp.h		\
	daemon/psidscripts.c daemon/psidscripts.h daemon/psidsignal.c	\
	daemon/psidsignal.h daemon/psidspawn.c daemon/psidspawn.h	\
	daemon/psidstate.c daemon/psidstate.h daemon/psidstatus.c	\
	daemon/psidstatus.h daemon/psidtask.c daemon/psidtask.h		\
	daemon/psidtimer.h daemon/psidutil.c daemon/psidutil.h		\
	daemon/psidintel.c daemon/psidintel.h				\
	daemon/psidflowcontrol.c daemon/psidflowcontrol.h
psid_LDFLAGS = $(AM_LDFLAGS) $(glib2_LIBS) -export-dynamic
psid_LDADD = $(libputil) $(libpscommon) $(libpslog) -lutil	\
	-ldl $(POPT_LIBS) $(NUMA_LIBS) $(psconfig_LIBS)

psilogger_SOURCES = logger/psilogger.c logger/psilogger.h	\
	logger/psiloggermerge.c logger/psiloggermerge.h		\
	logger/psiloggerclient.c logger/psiloggerclient.h
psilogger_LDADD = $(libputil) $(libpscommon) $(libpslog)

mlisten_LDADD = $(POPT_LIBS)

psmstart_LDADD = $(libpse) $(POPT_LIBS)

mpirun_chp4_LDADD = $(libpse) $(POPT_LIBS)

psispawn_LDADD = $(libpsi)

mpirun_chgm_LDADD = $(libpse) $(POPT_LIBS)

gmspawner_CPPFLAGS = $(AM_CPPFLAGS) -pthread
gmspawner_LDFLAGS = $(AM_LDFLAGS) -pthread
gmspawner_LDADD = $(libpsi) $(POPT_LIBS)

mpirun_openib_LDADD = $(libpse) $(POPT_LIBS)

test_pse_LDADD = $(libpse) $(POPT_LIBS)

test_config_SOURCES = test_config.c daemon/psidnodes.c			\
	daemon/psidnodes.h daemon/psidscripts.c daemon/psidscripts.h	\
	daemon/psidutil.c daemon/psidutil.h
test_config_LDADD = $(libputil) $(libpse) $(POPT_LIBS) $(glib2_LIBS)	\
	$(psconfig_LIBS)

pssh_LDADD = $(libpse) $(POPT_LIBS)

psaccounter_SOURCES = accounter/psaccounter.c
psaccounter_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"
psaccounter_LDADD = $(libpse) $(POPT_LIBS)

mpiexec_SOURCES = startup/mpiexec.c
mpiexec_CPPFLAGS = $(AM_CPPFLAGS) -DCONFIGDIR=\"$(configdir)\"
mpiexec_LDADD = $(libpse) $(libpskvs) $(POPT_LIBS)

testacct_LDADD = $(libpse) $(POPT_LIBS)

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/psaccounter
