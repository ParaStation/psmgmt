#
# ParaStation
#
# Copyright (C) 2012-2014 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -DLOCALSTATEDIR=\"$(localstatedir)\"
AM_LDFLAGS += -avoid-version

lib_LTLIBRARIES = libpsi.la

libpsi_la_SOURCES = psi.c psienv.c psiinfo.c psilog.c psipartition.c	\
	psispawn.c

libpsi_la_LIBADD = $(top_builddir)/lib/pscommon/libpscommon.la
