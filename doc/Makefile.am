#
# ParaStation
#
# Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

dist_plugin_DATA = psaccount.conf

dist_noinst_DATA = parastation.conf.tmpl

if PAM_ENABLED
  dist_pamconf_DATA = pam/psid
endif

EXTRA_DIST = MessageTypes psconfig systemd
