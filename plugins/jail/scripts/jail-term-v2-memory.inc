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
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and
# jail-main.sh with the process ID of the process to be jailed in
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

CLEANUP_DONE="false"

function rmMemFromUser()
{
    local MEM_VAL=$1
    local MEM_NAME=$2

    if [ -n "$MEM_VAL" ] && [ -n "$MEM_NAME" ]; then
	USER_MEM=$(cat "$CG_USER/$MEM_NAME" 2>/dev/null)
	if [ -n "$USER_MEM" ] && [ "$USER_NAME" != "max" ]; then
	    NEW_MEM=$((USER_MEM - MEM_VAL))

	    [ $NEW_MEM -lt 0 ] && {
		mlog "invalid user memory $NEW_MEM for $MEM_NAME"
		return
	    }
	    dlog "$NEW_MEM > $CG_USER/$MEM_NAME User: $USER"
	    cgWrite "$NEW_MEM" "$CG_USER/$MEM_NAME"
	fi
    fi
}

function termMemory()
{
    [ $CLEANUP_DONE == "true" ] && return;

    [ -n "$USER" ] || elog "no user env variable for job $JOBID"
    getLock

    if [ -n "$STEPID" ]; then
	dlog "killing step $JOBID:$STEPID with signal $KILL_SIGNAL"

	if [ -d "$CG_STEP" ]; then
	    killTasks "$CG_STEP"
	else
	    dlog "no cgroup step $CG_STEP"
	fi

    elif [ -n "$JOBID" ]; then
	dlog "killing job $JOBID with signal $KILL_SIGNAL"

	# remove memory from user cgroup
	rmMemFromUser "$JOB_RAM_SOFT" "memory.high"
	rmMemFromUser "$JOB_RAM_HARD" "memory.max"
	rmMemFromUser "$JOB_SWAP" "memory.swap.max"

	killJob "$CG_JOB"

	BASE="$CG_USER"
	for i in "$BASE"/job-*/; do
	    if [ "$i" == "$CG_USER/job-*/" ]; then
		dlog "no more jobs, kill user cgroup $CG_USER"
		killTasks "$CG_USER"
	    else
		dlog "leftover job: $i"
	    fi
	done

    else
	elog "missing JOBID"
    fi

    rmLock

    CLEANUP_DONE="true"
}

if [ "$CONSTRAIN_KMEM" == "no" ]; then
    dlog "constrain KMEM *disabled*"
else
    dlog "constrain KMEM enabled"
    termMemory
fi

if [ "$CONSTRAIN_RAM" == "no" ]; then
    dlog "constrain RAM *disabled*"
else
    dlog "constrain RAM enabled"
    termMemory
fi

if [ "$CONSTRAIN_SWAP" == "no" ]; then
    dlog "constrain SWAP *disabled*"
else
    dlog "constrain SWAP enabled"
    termMemory
fi
