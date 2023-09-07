#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2022-2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
#
# Script to be executed by ParaStation's jail plugin each time a
# process will be jailed into the cgroup. Nevertheless, this
# functionality is independent of the actual cgroup plugin.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and
# jail-main.sh with the process ID of the process to be jailed in
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

function loadBPF()
{
    local BPF_PATH=$1
    local BPF_ID=$2

    local QUIET=""
    [[ $DEBUG == "0" ]] && QUIET="-q"

    "$BPF_DEVICE_LOADER" -s -i "$BPF_ID" "$QUIET" 1>/dev/null 2>&1 && {
	dlog "skip loading for $BPF_PATH with ID $BPF_ID"
	return
    }

    mdsave "$BPF_PATH"
    "$BPF_DEVICE_LOADER" -l "$BPF_DEVICE_PROG" -p "$BPF_PATH" \
	-i "$BPF_ID" "$QUIET" || {
	elog "attaching $BPF_DEVICE_PROG to $BPF_PATH with ID $BPF_ID failed"
    }

    dlog "attached $BPF_DEVICE_PROG to $BPF_PATH with ID $BPF_ID"
}

function initDevices()
{
    [[ -n $USER ]] || elog "no user env variable for job $JOBID"
    mdsave "$CG_USER"
    loadBPF "$CG_USER/tasks" "USER_$USER"

    [[ -n $JOBID ]] || elog "no jobid for pid $CHILD user $USER"
    mdsave "$CG_JOB"
    loadBPF "$CG_JOB/tasks" "JOB_$JOBID"

    [[ -n $STEPID ]] && {
	mdsave "$CG_STEP"
	loadBPF "$CG_STEP/tasks" "STEP_${JOBID}_$STEPID"
    }

    dlog "adding child $CHILD to $CG_JOB/tasks/cgroup.procs"
    cgWrite "$CHILD" "$CG_JOB/tasks/cgroup.procs"

    # step cgroup
    [[ -n $STEPID ]] && {
	dlog "adding child $CHILD to $CG_STEP/tasks/cgroup.procs"
	cgWrite "$CHILD" "$CG_STEP/tasks/cgroup.procs"
    }
}

function setDev()
{
    local ACTION=$1
    local DEV=$2
    local ID=$3

    local QUIET=""
    [[ $DEBUG == "0" ]] && QUIET="-q"

    dlog "Set '$ACTION' dev $DEV id $ID"

    "$BPF_DEVICE_LOADER" -i "$ID" "$ACTION" "$DEV" "$QUIET" || {
	elog "set JOB access for device $DEV id $ID failed"
    }
}

function setDevices()
{
    local ACCESS=$1
    local ACTION=$2
    local ID=$3

    # set allowed devices
    cnt=0
    while true; do
	next="__PSJAIL_DEV_${ACCESS}_$cnt"
	[[ -z ${!next} ]] && break
	local DEV=${!next}
	DEV=${DEV#* }
	DEV=${DEV% *}

	dlog "setDevices: $ACTION device$cnt = $DEV id $ID"
	setDev "$ACTION" "$DEV" "$ID"

	cnt=$(( cnt + 1 ))
    done
}

function constrainSingleUser()
{
    # ssh login via pspam/pspmix server needs an existing user cgroup
    if [[ -d $CG_USER ]]; then
	# add user to cgroup
	dlog "adding child $CHILD to user cgroup $CG_USER"
	cgWrite "$CHILD" "$CG_USER/tasks/cgroup.procs"
    else
	elog "error: user cgroup $CG_USER is missing"
    fi
}

function initUserDevices()
{
    getLock

    [[ -n $USER ]] || elog "no user env variable for job $JOBID"
    mdsave "$CG_USER"
    loadBPF "$CG_USER/tasks" "USER_$USER"

    # add allowed devices to user cgroup
    ID="USER_$USER"
    BPF_SET=$("$BPF_DEVICE_LOADER" -s -i "$ID" 2>/dev/null)

    if [[ ${#BPF_SET} -lt 4 ]]; then
	# only deny devices if user cgroup is empty
	setDevices "DENY" "-d" "$ID"
    fi
    setDevices "ALLOW" "-a" "$ID"

    rmLock
}

if [[ $CONSTRAIN_DEVICES == "no" ]]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"

    if [[ -n $ADD_USER_TO_CGROUP ]]; then
	constrainSingleUser
    elif [[ -n $USER_INIT ]]; then
	initUserDevices
    else
	[[ -f $BPF_DEVICE_LOADER ]] || {
	    elog "BPF device loader $BPF_DEVICE_LOADER not found"
	}

	[[ -f $BPF_DEVICE_PROG ]] || {
	    elog "BPF device program $BPF_DEVICE_PROG not found"
	}

	getLock
	initDevices

	# set job cgroup access
	ID="JOB_$JOBID"
	BPF_SET=$("$BPF_DEVICE_LOADER" -s -i "$ID" 2>/dev/null)

	if [[ ${#BPF_SET} -lt 4 ]]; then
	    setDevices "DENY" "-d" "$ID"
	    setDevices "ALLOW" "-a" "$ID"

	    # add allowed devices to user cgroup
	    ID="USER_$USER"
	    BPF_SET=$("$BPF_DEVICE_LOADER" -s -i "$ID" 2>/dev/null)

	    if [[ ${#BPF_SET} -lt 4 ]]; then
		# only deny devices if user cgroup is empty
		setDevices "DENY" "-d" "$ID"
	    fi
	    setDevices "ALLOW" "-a" "$ID"
	else
	    dlog "job $JOBID cgroup id $ID already set"
	fi

	# set step cgroup access
	if [[ -n $STEPID ]]; then
	    ID="STEP_${JOBID}_$STEPID"
	    BPF_SET=$("$BPF_DEVICE_LOADER" -s -i "$ID" 2>/dev/null)

	    if [[ ${#BPF_SET} -lt 4 ]]; then
		setDevices "DENY" "-d" "$ID"
		setDevices "ALLOW" "-a" "$ID"
	    else
		dlog "step $JOBID:$STEPID cgroup id $ID already set"
	    fi
	fi

	rmLock
    fi
fi

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
