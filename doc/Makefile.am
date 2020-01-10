#
# ParaStation
#
# Copyright (C) 2012-2019 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

dist_plugin_DATA = psresport.conf psaccount.conf cgroup.conf

dist_sysctlconf_DATA = 99-psid.conf

dist_noinst_DATA = parastation.conf.tmpl README.cgroup README.pspam

EXTRA_DIST = MessageTypes psconfig systemd cgroup.conf.in

CLEANFILES = cgroup.conf.in
