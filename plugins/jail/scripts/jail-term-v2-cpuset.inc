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
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and
# jail-main.sh with the process ID of the process to be jailed in
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

function cleaupUserCgroup2()
{
    if [[ $ALLOC_LIST == "$JOBID" ]]; then
	# cleanup user directory

	dlog "no more jobs, kill user cgroup $CG_USER"
	killTasks "$CG_USER"
    else
	# update remaining cpus for user cgroup

	BASE=$CG_USER
	REMAINING_CPUS=""
	for i in "$BASE"/job-*/; do
	    LEFTOVER_JOB_TASKS=$(cat "$i/tasks" 2>/dev/null)
	    LEFTOVER_STEP_TASKS=$(cat "$i"/step-*/tasks 2>/dev/null)
	    if [[ -z "$LEFTOVER_JOB_TASKS" && -z "$LEFTOVER_STEP_TASKS" ]]; then
		dlog "removing leftover job $i"
		rmdir "$i"/step-* 2>/dev/null
		rmdir "$i"
	    else
		dlog "adding running job: $i"
		JOB_CPUS=$(cat "$i/cpuset.cpus")
		REMAINING_CPUS="$REMAINING_CPUS,$JOB_CPUS"
	    fi
	done

	if [[ -n $REMAINING_CPUS ]]; then
	    NEW_CPUS=$("$EXPAND" "$REMAINING_CPUS" |sort -g -u)
	    # shellcheck disable=SC2086
	    COMP_CPUS=$("$COMPRESS" $NEW_CPUS | tr -d "\[\]")
	    dlog "set remaining cpus: $COMP_CPUS"
	    cgWrite "$COMP_CPUS" "$CG_USER/cpuset.cpus"
	fi
    fi
}

if [[ $CONSTRAIN_CORES == "no" ]]; then
    dlog "constrain cores *disabled*"
else
    dlog "constrain cores enabled"

    [[ -n $USER ]] || elog "no user env variable for job $JOBID"

    getLock
    if [[ -n $STEPID ]]; then
	dlog "killing step $JOBID:$STEPID with signal $KILL_SIGNAL"

	if [[ -d $CG_STEP ]]; then
	    killTasks "$CG_STEP"
	else
	    dlog "no cgroup step $CG_STEP"
	fi

    elif [[ -n $JOBID ]]; then
	dlog "killing job $JOBID with signal $KILL_SIGNAL"
	killJob "$CG_JOB"

	if [[ -n $ALLOC_LIST ]]; then
	    cleaupUserCgroup2
	fi

    else
	elog "missing JOBID"
    fi
    rmLock
fi

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
