#
# ParaStation
#
# Copyright (C) 2023-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

dist_jaillib_SCRIPTS = jail-functions.inc jail-main.sh		\
		       jail-v2-cpuset.inc jail-v2-devices.inc	\
		       jail-v2-memory.inc jail-v2-oom.inc	\
		       jail-term-v2-cpuset.inc			\
		       jail-term-v2-devices.inc			\
		       jail-term-v2-memory.inc jail-init.sh	\
		       jail-finalize.sh

jaillib_SCRIPTS = jail-config.inc

EXTRA_DIST = jail-config.inc.in

# clean up generated files
CLEANFILES = jail-config.inc
