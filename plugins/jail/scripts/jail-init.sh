#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2021-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Script to be executed by ParaStation's jail plugin once at initialization.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special
# care has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and get
# the process ID of the main psid as an argument.

# save PID of main psid
echo "$1" > /run/psid.pid
SELF=$(realpath "$0")
CommandPath=${SELF%/*}

export modName="init"
export __PSJAIL_CGROUP_VERSION="autodetect"

# shellcheck source=/dev/null
source "${CommandPath}/jail-functions.inc"
# shellcheck source=/dev/null
source "${CommandPath}/jail-config.inc"

exec 2>>"$LOG_FILE" 1>&2

initJailEnv

if [[ ! -d $CGROUP_BASE ]]; then
    elog "cgroup filesystem $CGROUP_BASE not mounted"
fi

# cleanup leftover BPF psid directory
if [[ -d $BPF_PSID_MAPS ]]; then
    rm -r "$BPF_PSID_MAPS" 2>/dev/null
fi

# cleanup leftover cgroup psid directory
if [[ $CGROUP_VERSION == "v2" ]]; then
    dlog "cleanup v2 $CGROUP_BASE"
    for i in "$CGROUP_BASE"/psid-*/; do
	for user in "$i"/user-*; do
	    for job in "$user"/job-*; do
		for step in "$job"/step-*; do
		    rmdir -p "$step/tasks" 2>/dev/null
		    rmdir -p "$step" 2>/dev/null
		done
		rmdir -p "$job/tasks" 2>/dev/null
		rmdir -p "$job" 2>/dev/null
	    done
	    rmdir -p "$user/tasks" 2>/dev/null
	    rmdir -p "$user" 2>/dev/null
	done
	rmdir -p "$i" 2>/dev/null
    done
else
    dlog "cleanup v1 $CGROUP_BASE"
    for modName in ${MODULES//,/$IFS}; do
	for i in "$CGROUP_BASE/$modName"/psid-*/; do
	    for job in "$i"/job-*; do
		for step in "$job"/step-*; do
		    rmdir -p "$step" 2>/dev/null
		done
		rmdir -p "$job" 2>/dev/null
	    done
	    rmdir -p "$i" 2>/dev/null
	done
    done
fi

if [[ $CGROUP_VERSION == "v2" ]]; then
    BASE="$CGROUP_BASE/$PREFIX-$PSID_PID"
    mdsave "$BASE"

    for controller in ${CGROUP_CONTROLLER//,/$IFS}; do
	assertController "$controller"

	# ensure controller is enabled in main cgroup dir
	enableSingleController "$controller" "$CGROUP_BASE"

	# enable cgroup controller for psid directory
	enableSingleController "$controller" "$BASE"
    done
fi

exit 0
