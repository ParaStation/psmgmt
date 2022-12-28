#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2022 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permission! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and get
# the process ID of the process to be jailed as an argument.

[ -n "$SLURM_USER" ] && export USER="$SLURM_USER"

CG_USER="$BASE/$PREFIX-$USER"
CG_JOB="$CG_USER/job-$SLURM_JOBID"
CG_STEP="$CG_JOB/step-$SLURM_STEPID"

[ -z "$KILL_SIGNAL" ] && export KILL_SIGNAL="9"

[ -n "$USER" ] || {
		elog "no user env variable for job $SLURM_JOBID"
}

if [ -n "$SLURM_STEPID" ]; then
	dlog "killing step $SLURM_JOBID:$SLURM_STEPID with signal $KILL_SIGNAL"

	if [ -d "$CG_STEP" ]; then
		killTasks "$CG_STEP"
	else
		dlog "no cgroup step $CG_STEP"
	fi

elif [ -n "$SLURM_JOBID" ]; then
	dlog "killing job $SLURM_JOBID with signal $KILL_SIGNAL"
	killJob $CG_JOB

	# cleanup user directory
	LEFT_JOBS=$(ls $CG_USER/job-*/ 2>/dev/null)
	if [ -z "$LEFT_JOBS" ]; then
		killTasks "$CG_USER"
	fi
else
	elog "missing SLURM_JOBID"
fi
