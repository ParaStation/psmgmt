#
# ParaStation
#
# Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022 ParTec AG, Munich
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

slurmlib_LTLIBRARIES = cudaspank.la energyreport.la globresspank.la \
	nopshcspank.la noturbospank.la perfparanoidspank.la \
	perftoolspank.la showglobresspank.la spank_api.la \
	visspank.la x11spank.la

spank_api_la_SOURCES = spank_api.c spank_api.h

spank_api_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

cudaspank_la_SOURCES = cudaspank.c

cudaspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

energyreport_la_SOURCES = energyreport.c

energyreport_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

globresspank_la_SOURCES = globresspank.c

globresspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

nopshcspank_la_SOURCES = nopshcspank.c

nopshcspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

noturbospank_la_SOURCES = noturbospank.c

noturbospank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

perfparanoidspank_la_SOURCES = perfparanoidspank.c

perfparanoidspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

perftoolspank_la_SOURCES = perftoolspank.c

perftoolspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

showglobresspank_la_SOURCES = showglobresspank.c

showglobresspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

visspank_la_SOURCES = visspank.c

visspank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"

x11spank_la_SOURCES = x11spank.c

x11spank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"
