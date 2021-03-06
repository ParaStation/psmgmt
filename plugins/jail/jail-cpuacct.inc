#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2021 ParTec AG, Munich
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
# daemon psid, i.e. typically with root permission! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and get
# the process ID of the process to be jailed as an argument.

if [ -n "$PSPAM_USER" ]; then
	# ssh login via pspam which needs an existing user cgroup
	CG_USER="$BASE/$PREFIX-$PSPAM_USER"
	if [ -d $CG_USER ]; then
		# add user to cgroup
		mlog "adding child $CHILD to user cgroup $CG_USER"
		echo "$CHILD" > $CG_USER/tasks
	else
		elog "error: user cgroup $CG_USER is missing"
	fi
else
	[ -n "$SLURM_USER" ] && USER=$SLURM_USER

	[ -n "$USER" ] || {
		elog "no user env variable for job $SLURM_JOBID"
	}

	CG_USER="$BASE/$PREFIX-$USER"
	mdsave "$CG_USER"

	[ -n "$SLURM_JOBID" ] || elog "no jobid for pid $CHILD user $USER"

	CG_JOB="$CG_USER/job-$SLURM_JOBID"
	mdsave "$CG_JOB"

	dlog "adding child $CHILD to $CG_JOB/tasks"
	echo "$CHILD" > $CG_JOB/tasks

	# step cgroup
	[ -n "$SLURM_STEPID" ] && {
		CG_STEP="$CG_JOB/step-$SLURM_STEPID"
		mdsave "$CG_STEP"

		dlog "adding child $CHILD to $CG_STEP/tasks"
		echo "$CHILD" > $CG_STEP/tasks
	}
fi
