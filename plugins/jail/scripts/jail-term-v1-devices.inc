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

function termDevices()
{
    [ -n "$USER" ] || elog "no user env variable for job $JOBID"

    if [ -n "$STEPID" ]; then
	dlog "killing step $JOBID:$STEPID with signal $KILL_SIGNAL"

	if [ -d "$CG_STEP" ]; then
	    killTasks "$CG_STEP"
	else
	    dlog "no cgroup step $CG_STEP"
	fi

    elif [ -n "$JOBID" ]; then
	dlog "killing job $JOBID with signal $KILL_SIGNAL"
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
}

if [ "$CONSTRAIN_DEVICES" == "no" ]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"
    termDevices
fi