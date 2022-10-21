#
# ParaStation
#
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(srcdir)/../include

check_PROGRAMS = test_rrcomm

TESTS = test_rrcomm

librrcomm = ../lib/librrcomm.a

test_rrcomm_SOURCES = test_rrcomm.c
test_rrcomm_LDADD = $(librrcomm)
