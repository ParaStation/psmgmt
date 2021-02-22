#
# ParaStation
#
# Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

psslurm_dir := plugins/psslurm/src

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon		\
	       -I$(top_srcdir)/$(psslurm_dir)/common 	\
	       -I$(top_srcdir)/$(psslurm_dir)/psslurm

slurmlib_LTLIBRARIES = spank_api.la x11spank.la

spank_api_la_SOURCES = spank_api.c spank_api.h x11spank.c

spank_api_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"
