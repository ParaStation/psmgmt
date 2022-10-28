#
# ParaStation
#
# Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version

lib_LTLIBRARIES = libpse.la

libpse_la_SOURCES = pse.c

libpse_la_LIBADD = $(top_builddir)/lib/psi/libpsi.la
