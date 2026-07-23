#
# ParaStation
#
# Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2026 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

psslurm_dir := plugins/psslurm/src

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon		\
	       -I$(top_srcdir)/$(psslurm_dir)/common	\
	       -I$(top_srcdir)/$(psslurm_dir)/psslurm

slurmlib_LTLIBRARIES = cudaspank.la energyreport.la globresspank.la	\
	nopshcspank.la noturbospank.la perfparanoidspank.la		\
	perftoolspank.la showglobresspank.la spank_api.la visspank.la	\
	x11spank.la psetspank.la

spank_api_la_SOURCES = spank_api.c spank_api.h

cudaspank_la_SOURCES = cudaspank.c

energyreport_la_SOURCES = energyreport.c

globresspank_la_SOURCES = globresspank.c

nopshcspank_la_SOURCES = nopshcspank.c

noturbospank_la_SOURCES = noturbospank.c

perfparanoidspank_la_SOURCES = perfparanoidspank.c

perftoolspank_la_SOURCES = perftoolspank.c

showglobresspank_la_SOURCES = showglobresspank.c

visspank_la_SOURCES = visspank.c

x11spank_la_SOURCES = x11spank.c

psetspank_la_SOURCES = psetspank.c
