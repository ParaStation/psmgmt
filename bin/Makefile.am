#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = admin daemon logger startup

bin_PROGRAMS = mlisten test_pse pssh
pkglibexec_PROGRAMS = psaccounter pspelogue
noinst_PROGRAMS = testacct

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libpse = $(top_builddir)/lib/pse/libpse.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

mlisten_LDADD = $(POPT_LIBS) $(libpscommon)

test_pse_LDADD = $(libpse) $(POPT_LIBS)

pssh_LDADD = $(libpse) $(POPT_LIBS)

psaccounter_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"
psaccounter_LDADD = $(libpse) $(POPT_LIBS)

pspelogue_CFLAGS = $(AM_CFLAGS) $(glib2_CFLAGS)
pspelogue_LDFLAGS = $(AM_LDFLAGS) $(glib2_LDFLAGS)
pspelogue_CPPFLAGS = $(AM_CPPFLAGS)  -I$(top_srcdir)/plugins/pelogue
pspelogue_LDADD = $(libpsi) $(POPT_LIBS) $(psconfig_LIBS)

testacct_LDADD = $(libpsi) $(POPT_LIBS)

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/psaccounter
