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

if [ "$SLURM_CONSTRAIN_CORES" == "no" ]; then
	dlog "setting *no* constrains for CPU cores"
elif [ -n "$PSPAM_USER" ]; then
	# ssh login via pspam which needs an existing user cgroup
	CG_USER="$BASE/$PREFIX-$PSPAM_USER"
	if [ -d $CG_USER ]; then
		if [ -z $(cat $CG_USER/cpuset.cpus) ]; then
			# error empty cpuset
			elog "error: user cgroup $CG_USER has empty cpuset.cpus"
		fi

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

	[ -n "$JOB_CPUS" ] || {
		elog "no CPU cores for job $SLURM_JOBID"
	}

	CG_USER="$BASE/$PREFIX-$USER"
	mdsave "$CG_USER"
	if [ -z $(cat $CG_USER/cpuset.cpus) ]; then
		dlog "empty cpuset.cpus"
		echo "$JOB_CPUS" > $CG_USER/cpuset.cpus
	else
		OLD_CPU=$(cat $CG_USER/cpuset.cpus)
		dlog "old cpuset.cpus is $OLD_CPU"
		dlog "add cpuset.cpus is $JOB_CPUS"

		if [ "$OLD_CPU" == "JOB_CPUS" ]; then
			dlog "using old cpuset: $OLD_CPU"
		else
			NEW_CPU=$($EXPAND $OLD_CPU,$JOB_CPUS |sort -g -u)
			COMP_CPU=$($COMPRESS $NEW_CPU | tr -d "\[\]")
			dlog "using new cpuset: $COMP_CPU"
			echo "$COMP_CPU" > $CG_USER/cpuset.cpus
		fi
	fi

	# set memory nodes from base for now
	MEM_NODES=$(cat $BASE/cpuset.mems)

	echo "$MEM_NODES" > $CG_USER/cpuset.mems
	dlog "user cpuset: " $(cat $CG_USER/cpuset.cpus)

	[ -n "$SLURM_JOBID" ] || elog "no jobid for pid $CHILD user $USER"

	CG_JOB="$CG_USER/job-$SLURM_JOBID"
	mdsave "$CG_JOB"

	# job cgroup
	dlog "JOB: $SLURM_JOBID STEP:$SLURM_STEPID"

	echo "$JOB_CPUS" > $CG_JOB/cpuset.cpus
	echo "$MEM_NODES" > $CG_JOB/cpuset.mems

	dlog "adding child $CHILD to $CG_JOB/tasks"
	echo "$CHILD" > $CG_JOB/tasks
	dlog "job cpuset: " $(cat $CG_JOB/cpuset.cpus)

	# step cgroup
	[ -n "$SLURM_STEPID" ] && {
		CG_STEP="$CG_JOB/step-$SLURM_STEPID"
		mdsave "$CG_STEP"

		echo "$JOB_CPUS" > $CG_STEP/cpuset.cpus
		echo "$MEM_NODES" > $CG_STEP/cpuset.mems

		dlog "adding child $CHILD to $CG_STEP/tasks"
		echo "$CHILD" > $CG_STEP/tasks
		dlog "step cpuset: " $(cat $CG_STEP/cpuset.cpus)
	}
fi
