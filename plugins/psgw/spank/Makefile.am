#
# ParaStation
#
# Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module

slurmlib_LTLIBRARIES = psgw_spank.la

psgw_spank_la_SOURCES = psgw_spank.c

psgw_spank_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"
