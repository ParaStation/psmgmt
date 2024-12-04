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

bin_SCRIPTS = psaccview

profile_ddir = $(sysconfdir)/profile.d
profile_d_DATA = psmgmt.csh psmgmt.sh
init_ddir = $(sysconfdir)/init.d
gdbinit_ddir = $(sysconfdir)/gdbinit.d
gdbinit_d_DATA = psmgmt.gdb


if HAVE_SYSTEMD
  unit_DATA = systemd/psid.service
  unit_DATA += systemd/psidstarter.socket
  unit_DATA += systemd/psidstarter@.service
  systemd_psid_ddir = $(systemdconfdir)/psid.service.d
if MUNGE_ENABLED
  systemd_psid_d_DATA = systemd/munge.conf
endif
else
  init_d_SCRIPTS = init.d/parastation
  nobase_sysconf_DATA = xinetd.d/psidstarter
endif

config_DATA = mpiexec.gdb

config_SCRIPTS = ps_acc

scripts_SCRIPTS = single_node_config

noinst_SCRIPTS = memtest.py

pkglibexec_SCRIPTS = pscompress.py psexpand.py

# for simplicity, put all dist files here, so no dist_ primaries above
EXTRA_DIST = psmgmt.gdb init.d/parastation.in loggerdebug.py		\
	memtest.py mpiexec.gdb ps_acc.in psaccview.in psmgmt.csh.in	\
	psmgmt.sh.in systemd/munge.conf systemd/psid.service.in		\
	systemd/psidstarter.socket systemd/psidstarter@.service		\
	vcversion xinetd.d/psidstarter.in single_node_config.in

# clean up generated files
CLEANFILES = psaccview psmgmt.csh psmgmt.sh ps_acc			\
	init.d/parastation xinetd.d/psidstarter systemd/psid.service	\
	single_node_config
