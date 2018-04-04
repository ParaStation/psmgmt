#
# ParaStation
#
# Copyright (C) 2012-2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = admin daemon logger startup

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt version -n)
PKG_REL := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt release -n)

AM_CPPFLAGS += -DVERSION_psmgmt=\"$(PKG_VER)\"
AM_CPPFLAGS += -DRELEASE_psmgmt=\"$(PKG_REL)\"

bin_PROGRAMS = mlisten psmstart mpirun_chp4 psispawn mpirun_chgm	\
	gmspawner mpirun_openib test_pse pssh psaccounter pspelogue
noinst_PROGRAMS = testacct

libpse = $(top_builddir)/lib/pse/libpse.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

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

pssh_LDADD = $(libpse) $(POPT_LIBS)

psaccounter_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"
psaccounter_LDADD = $(libpse) $(POPT_LIBS)

pspelogue_LDADD = $(libpsi) $(POPT_LIBS)

testacct_LDADD = $(libpsi) $(POPT_LIBS)

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/psaccounter
