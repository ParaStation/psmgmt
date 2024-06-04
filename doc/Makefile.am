#
# ParaStation
#
# Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

dist_plugin_DATA = psaccount.conf

if HAVE_SYSTEMD
  dist_sysctl_DATA = 99-psid.conf
endif

dist_noinst_DATA = parastation.conf.tmpl README.nodeinfo README.pspam	\
	README.psgw

if PAM_ENABLED
  dist_pamconf_DATA = pam/psid
endif

EXTRA_DIST = MessageTypes psconfig systemd
