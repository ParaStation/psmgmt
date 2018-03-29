#
# ParaStation
#
# Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/plugins/pspam/include

pam_LTLIBRARIES = pam_pspam.la

libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

pam_pspam_la_SOURCES = pam_module.c

pam_pspam_la_LIBADD = -lpam $(libpscommon)
