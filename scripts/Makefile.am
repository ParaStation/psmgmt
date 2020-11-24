#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#
include $(top_srcdir)/common.am

bin_SCRIPTS = psh psaccview

profile_ddir = $(sysconfdir)/profile.d
profile_d_DATA = psmgmt.csh psmgmt.sh
init_ddir = $(sysconfdir)/init.d
gdbinit_ddir = $(sysconfdir)/gdbinit.d
gdbinit_d_DATA = psmgmt.gdb


if HAVE_SYSTEMD
  unit_DATA = systemd/psid.service
  unit_DATA += systemd/psidstarter.socket
  unit_DATA += systemd/psidstarter@.service
else
  init_d_SCRIPTS = init.d/parastation
  nobase_sysconf_DATA = xinetd.d/psidstarter
endif

config_DATA = mpiexec.gdb

config_SCRIPTS = ps_gm ps_acc

scripts_SCRIPTS = ps_gmcounters

noinst_SCRIPTS = psid2cmustatus memtest.py

# for simplicity, put all dist files here, so no dist_ primaries above
EXTRA_DIST = psmgmt.gdb init.d/parastation.in loggerdebug.py lsf  \
        memtest.py mpiexec.gdb mpirun_java parastation_tru64    \
        ps_acc.in ps_gm.in ps_gmcounters psaccview.in psh       \
        psid2cmustatus.in psmgmt.csh.in psmgmt.sh.in            \
        systemd/psid.service.in systemd/psidstarter.socket      \
        systemd/psidstarter@.service vcversion                  \
        xinetd.d/psidstarter.in

# clean up generated files
CLEANFILES = psaccview psmgmt.csh psmgmt.sh ps_gm ps_acc                \
        init.d/parastation xinetd.d/psidstarter systemd/psid.service    \
        psid2cmustatus
