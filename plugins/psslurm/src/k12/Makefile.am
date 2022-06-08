#
# ParaStation
#
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

include $(top_srcdir)/common.am

noinst_LTLIBRARIES = libk12.la

libk12_la_SOURCES = align.h brg_endian.h k12-config.h KangarooTwelve.c \
	KangarooTwelve.h KeccakP-1600-64.macros KeccakP-1600-opt64.c   \
	KeccakP-1600-opt64-config.h KeccakP-1600-SnP.h		       \
	KeccakP-1600-unrolling.macros KeccakSponge.c KeccakSponge.h    \
	KeccakSponge.inc Phases.h SnP-Relaned.h
