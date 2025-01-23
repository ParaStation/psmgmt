#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2023-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and
# jail-main.sh with the process ID of the process to be jailed in
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

function cleanupBPF()
{
    local BPF_ID=$1

    local QUIET=""
    [[ $DEBUG == 0 ]] && QUIET="-q"

    dlog "$BPF_DEVICE_LOADER -c -i $BPF_ID $QUIET"
    "$BPF_DEVICE_LOADER" -c -i "$BPF_ID" "$QUIET"
}

function removeFromUserBPF()
{
    local BPF_ID=$1

    local QUIET=""
    [[ $DEBUG == 0 ]] && QUIET="-q"

    dlog "$BPF_DEVICE_LOADER -i $BPF_ID $QUIET --remove_allowed USER_$USER"
    "$BPF_DEVICE_LOADER" -i "$BPF_ID" "$QUIET" --remove_allowed "USER_$USER"
}

function termDevices()
{
    [[ -n $USER ]] || elog "no user env variable for job $JOBID"

    if [[ -n $STEPID ]]; then
	dlog "killing step ${JOBID}:$STEPID with signal $KILL_SIGNAL"

	cleanupBPF "STEP_${JOBID}_$STEPID"

	if [[ -d $CG_STEP ]]; then
	    killTasks "$CG_STEP"
	else
	    dlog "no cgroup step $CG_STEP"
	fi

    elif [[ -n $JOBID ]]; then
	dlog "killing job $JOBID with signal $KILL_SIGNAL"
	killJob "$CG_JOB"
	cleanupBPF "JOB_$JOBID"

	local BASE="$CG_USER"
	for i in "$BASE"/step-*/; do
	    [[ -d $i ]] || break
	    dlog "cleanup dev: $i"
	    cleanupBPF "STEP_${JOBID}_$i"
	done

	# cleanup user directory
	if [[ -n $ALLOC_LIST && $ALLOC_LIST == "$JOBID" ]]; then
	    dlog "no more jobs, kill user cgroup $CG_USER"
	    killTasks "$CG_USER"
	    removeFromUserBPF "JOB_$JOBID"
	    cleanupBPF "USER_$USER"
	else
	    dlog "leftover jobs: $ALLOC_LIST"
	fi
    else
	elog "missing JOBID"
    fi
}

if [[ $CONSTRAIN_DEVICES == "no" ]]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"
    termDevices
fi

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
