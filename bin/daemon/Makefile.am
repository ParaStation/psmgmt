#
# ParaStation
#
# Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
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

bin_PROGRAMS = psid test_config

include_HEADERS = psidcomm.h psidhook.h	psidnodes.h psidplugin.h	\
	psidscripts.h psidstatus.h psidtask.h psidutil.h

libputil = $(top_builddir)/lib/putil/libputil.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la

psid_SOURCES = psid.c psidaccount.c psidaccount.h psidamd.c psidamd.h	\
	psidclient.c psidclient.h psidcomm.c psidcomm.h psidenv.c	\
	psidenv.h psidforwarder.c psidforwarder.h psidhook.c		\
	psidhook.h psidhw.c psidhw.h psidinfo.c psidinfo.h		\
	psidmsgbuf.c psidmsgbuf.h psidnodes.c psidnodes.h		\
	psidoption.c psidoption.h psidpartition.c psidpartition.h	\
	psidplugin.c psidplugin.h psidppc.c psidppc.h psidrdp.c		\
	psidrdp.h psidscripts.c psidscripts.h psidsignal.c		\
	psidsignal.h psidspawn.c psidspawn.h psidstate.c psidstate.h	\
	psidstatus.c psidstatus.h psidtask.c psidtask.h psidtimer.h	\
	psidutil.c psidutil.h psidintel.c psidintel.h			\
	psidflowcontrol.c psidflowcontrol.h

psid_LDFLAGS = $(AM_LDFLAGS) $(glib2_LIBS) -export-dynamic
psid_LDADD = $(libputil) $(libpscommon) $(libpslog) -lutil	\
	-ldl $(POPT_LIBS) $(NUMA_LIBS) $(psconfig_LIBS)

test_config_SOURCES = test_config.c psidnodes.c psidnodes.h	\
	psidscripts.c psidscripts.h psidutil.c psidutil.h
test_config_LDADD = $(libputil) $(libpscommon) $(POPT_LIBS)	\
	$(glib2_LIBS) $(psconfig_LIBS)
