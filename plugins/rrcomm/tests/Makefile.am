#
# ParaStation
#
# Copyright (C) 2022-2026 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(srcdir)/../include

check_PROGRAMS = poll_rrcomm test_rrcomm cornercases spawn_rrcomm

TESTS = poll_rrcomm test_rrcomm cornercases

librrcomm = ../lib/librrcomm.a

poll_rrcomm_SOURCES = poll_rrcomm.c
poll_rrcomm_LDADD = $(librrcomm)

test_rrcomm_SOURCES = test_rrcomm.c
test_rrcomm_LDADD = $(librrcomm)

cornercases_SOURCES = cornercases.c
cornercases_LDADD = $(librrcomm)

spawn_rrcomm_SOURCES = spawn_rrcomm.c
spawn_rrcomm_LDADD = $(librrcomm)

if !BUILD_WITHOUT_PSPMIX
check_PROGRAMS += spawnPMIx_rrcomm
spawnPMIx_rrcomm_SOURCES = spawnPMIx_rrcomm.c
spawnPMIx_rrcomm_CPPFLAGS = $(AM_CPPFLAGS) $(pmix_CFLAGS)
spawnPMIx_rrcomm_LDADD = $(librrcomm) $(pmix_LIBS)
endif
