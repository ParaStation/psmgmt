#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

# active modules
MODULES="cpuset,oom,devices,memory"

# base directory of the cgroup filesystem
CGROUP_BASE="/sys/fs/cgroup"

# path to psnameexpand executable
EXPAND="$CommandPath/../psmgmt/psexpand.py"

# path to psnamecompress executable
COMPRESS="$CommandPath/../psmgmt/pscompress.py"

# enable debug logging
DEBUG="0"

# prefix of the cgroup name
PREFIX="psid"

# maximum retries to send signals to tasks of a cgroup
MAX_KILL_RETRY="10"

# path to logging file
LOG_FILE="/tmp/jail.log"