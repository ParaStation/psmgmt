#
# ParaStation
#
# Copyright (C) 2012-2015 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#
include $(top_srcdir)/common.am

dist_bin_SCRIPTS = psh psaccview

profile_ddir = $(sysconfdir)/profile.d
dist_profile_d_DATA = psmgmt.csh psmgmt.sh

init_ddir = $(sysconfdir)/init.d
dist_init_d_SCRIPTS = init.d/parastation

xinetd_ddir = $(sysconfdir)/xinetd.d
dist_xinetd_d_DATA = xinetd.d/psidstarter

dist_config_DATA = mpiexec.gdb
dist_config_SCRIPTS = ps_gm ps_acc

dist_scripts_SCRIPTS = ps_gmcounters

noinst_SCRIPTS = psid2cmustatus
noinst_SCRIPTS += memtest.py

EXTRA_DIST = vcversion
EXTRA_DIST += .gdbinit mpirun_java parastation_tru64
EXTRA_DIST += lsf memtest.py loggerdebug.py

# templates for generated files
EXTRA_DIST += psaccview.in
EXTRA_DIST += psmgmt.csh.in psmgmt.sh.in
EXTRA_DIST += ps_gm.in ps_acc.in
EXTRA_DIST += init.d/parastation.in xinetd.d/psidstarter.in
EXTRA_DIST += psid2cmustatus.in

# clean up generated files
CLEANFILES = psaccview
CLEANFILES += psmgmt.csh psmgmt.sh
CLEANFILES += ps_gm ps_acc
CLEANFILES += init.d/parastation xinetd.d/psidstarter
CLEANFILES += psid2cmustatus
