#
# ParaStation
#
# Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = cgroup.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

cgroup_la_SOURCES = cgroup.c cgroupconfig.c cgroupconfig.h cgrouplog.c \
	cgrouplog.h

cgroup_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DDEFAULT_CGROUP_ROOT=\"$(default_cgroup_root)\" \
	-DDEFAULT_CGROUP_NAME=\"$(default_cgroup_name)\"
cgroup_la_LIBADD = $(libplugincommon) $(libpscommon)
