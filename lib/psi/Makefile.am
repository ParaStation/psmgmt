#
# ParaStation
#
# Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version

lib_LTLIBRARIES = libpsi.la

libpsi_la_SOURCES = psi.c psilog.c psienv.c psispawn.c psipartition.c	\
	psiinfo.c psicomm.c

libpsi_la_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"

libpsi_la_LIBADD = $(top_builddir)/lib/pscommon/libpscommon.la
