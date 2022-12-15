#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2022 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

# active modules
MODULES="cpuset,oom,devices,memory"

# base directory of the cgroup filesystem
CGROUP_BASE="/sys/fs/cgroup"

# path to psnameexpand executable
EXPAND="/opt/parastation/bin/psnameexpand"

# path to psnamecompress executable
COMPRESS="/opt/parastation/bin/psnamecompress"

# enable debug logging
DEBUG="0"

# prefix of the cgroup name
PREFIX="psid"

# maximum retries to send signals to tasks of a cgroup
MAX_KILL_RETRY="10"

# path to logging file
LOG_FILE="/tmp/jail.log"
