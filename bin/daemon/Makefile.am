#
# ParaStation
#
# Copyright (C) 2017-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

bin_PROGRAMS = test_config
sbin_PROGRAMS = psid

include_HEADERS = psidcomm.h psidhook.h	psidnodes.h psidplugin.h	\
	psidscripts.h psidstatus.h psidtask.h psidutil.h

libputil = $(top_builddir)/lib/putil/libputil.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la

psid_SOURCES = psid.c psidaccount.c psidaccount.h psidclient.c		\
	psidclient.h psidcomm.c psidcomm.h psidenv.c psidenv.h		\
	psidflowcontrol.c psidflowcontrol.h psidforwarder.c		\
	psidforwarder.h psidhook.c psidhook.h psidhw.c psidhw.h		\
	psidinfo.c psidinfo.h psidmsgbuf.c psidmsgbuf.h psidnodes.c	\
	psidnodes.h psidoption.c psidoption.h psidpartition.c		\
	psidpartition.h psidpin.c psidpin.h psidplugin.c psidplugin.h	\
	psidrdp.c psidrdp.h psidscripts.c psidscripts.h psidsession.c	\
	psidsession.h psidsignal.c psidsignal.h psidspawn.c		\
	psidspawn.h psidstate.c psidstate.h psidstatus.c psidstatus.h	\
	psidtask.c psidtask.h psidutil.c psidutil.h

psid_LDFLAGS = $(AM_LDFLAGS) $(glib2_LIBS) -export-dynamic
psid_LDADD = $(libputil) $(libpscommon) $(libpslog) -lutil -ldl -lrt	\
	$(POPT_LIBS) $(NUMA_LIBS) $(HWLOC_LIBS) $(psconfig_LIBS)

test_config_SOURCES = test_config.c psidnodes.c psidnodes.h	\
	psidscripts.c psidscripts.h psidutil.c psidutil.h
test_config_LDADD = $(libputil) $(libpscommon) -lrt $(POPT_LIBS)	\
	$(glib2_LIBS) $(psconfig_LIBS)
