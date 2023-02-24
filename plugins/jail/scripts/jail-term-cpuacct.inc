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

[ -z "$KILL_SIGNAL" ] && export KILL_SIGNAL="9"

[ -n "$USER" ] || {
		elog "no user env variable for job $JOBID"
}

if [ -n "$STEPID" ]; then
	dlog "killing step $JOBID:$STEPID with signal $KILL_SIGNAL"

	if [ -d "$CG_STEP" ]; then
		killTasks "$CG_STEP"
	else
		dlog "no cgroup step $CG_STEP"
	fi

elif [ -n "$JOBID" ]; then
	dlog "killing job $JOBID with signal $KILL_SIGNAL"
	killJob $CG_JOB

	# cleanup user directory
	LEFT_JOBS=$(ls $CG_USER/job-*/ 2>/dev/null)
	if [ -z "$LEFT_JOBS" ]; then
		killTasks "$CG_USER"
	fi
else
	elog "missing JOBID"
fi
