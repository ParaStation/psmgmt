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

AM_LDFLAGS += -avoid-version -module

check_PROGRAMS = test_logging test_paramspace test_psicomm

TESTS = test_logging test_paramspace

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libputil = $(top_builddir)/lib/putil/libputil.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

test_logging_SOURCES = test_logging.c
test_logging_LDADD = $(libpscommon)

test_paramspace_SOURCES = test_paramspace.c
test_paramspace_LDADD = $(libpscommon) $(libputil)

test_psicomm_SOURCES = test_psicomm.c
test_psicomm_LDADD = $(libpsi)