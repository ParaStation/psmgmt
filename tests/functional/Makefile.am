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

AM_LDFLAGS += -avoid-version -module

check_PROGRAMS = test_logging test_paramspace listInfoTypes	\
	test_bitsets test_psenv

TESTS = test_logging test_paramspace test_bitsets test_psenv

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libputil = $(top_builddir)/lib/putil/libputil.la
libpsi = $(top_builddir)/lib/psi/libpsi.la

test_logging_SOURCES = test_logging.c
test_logging_LDADD = $(libpscommon)

test_paramspace_SOURCES = test_paramspace.c
test_paramspace_LDADD = $(libpscommon) $(libputil)

listInfoTypes_SOURCES = listInfoTypes.c
listInfoTypes_LDADD = $(libpscommon)

test_bitsets_SOURCES = test_bitsets.c
test_bitsets_LDADD = $(libpscommon)

test_psenv_SOURCES = test_psenv.c
test_psenv_LDADD = $(libpscommon)
