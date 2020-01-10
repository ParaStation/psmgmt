#
# ParaStation
#
# Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

psaccount_dir := plugins/psaccount
pspam_dir := plugins/pspam/pspam

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include \
		-I$(top_srcdir)/$(psaccount_dir) -I$(top_srcdir)/$(pspam_dir)

plugin_LTLIBRARIES = psmom.la

libpbs = $(top_builddir)/plugins/psmom/pbs/libpbs.la
libpsi = $(top_builddir)/lib/psi/libpsi.la
libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

psmom_la_SOURCES = Makefile.am psmomacc.c psmomacc.h psmom.c		\
	psmomchild.c psmomchild.h psmomauth.c psmomauth.h		\
	psmomcollect.c psmomcollect.h psmomcomm.c psmomcomm.h		\
	psmomconfig.c psmomconfig.h psmomconv.c psmomconv.h		\
	psmomenv.c psmomenv.h psmomforwarder.c psmomforwarder.h		\
	psmom.h psmominteractive.c psmominteractive.h psmomjob.c	\
	psmomjob.h psmomjobinfo.c psmomjobinfo.h psmomkvs.c		\
	psmomkvs.h psmomlist.c psmomlist.h psmomlocalcomm.c		\
	psmomlocalcomm.h psmomlog.c psmomlog.h psmompartition.c		\
	psmompartition.h psmompbsserver.c psmompbsserver.h		\
	psmomproto.c psmomproto.h psmompscomm.c psmompscomm.h		\
	psmomrecover.c psmomrecover.h psmomrpp.c psmomrpp.h		\
	psmomscript.c psmomscript.h psmomsignal.c psmomsignal.h		\
	psmomspawn.c psmomspawn.h psmomtcp.c psmomtcp.h

psmom_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"

psmom_la_LIBADD = $(libpsi) $(libpbs) $(libplugincommon)

if PAM_ENABLED
  psmom_la_LIBADD += -lpam
endif

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/jobs
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/nodefiles
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/temp
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/undelivered
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/account
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/backup
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/scripts
if PAM_ENABLED
	$(INSTALL) -d $(DESTDIR)$(pamdir)
	$(LN_S) pam_pspam.so $(DESTDIR)$(pamdir)/pam_psmom.so
endif

uninstall-local:
	$(RM) $(DESTDIR)$(pamdir)/pam_psmom.so
