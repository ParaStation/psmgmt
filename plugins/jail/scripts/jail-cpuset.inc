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

if [ "$CONSTRAIN_CORES" == "no" ]; then
	dlog "constrain cores *disabled*"
elif [ -n "$PSPAM_USER" ]; then
	dlog "constrain cores enabled"
	getLock

	# ssh login via pspam which needs an existing user cgroup
	if [ -d $CG_USER ]; then
		if [ -z $(cat $CG_USER/cpuset.cpus) ]; then
			# error empty cpuset
			elog "error: user cgroup $CG_USER has empty cpuset.cpus"
		fi

		# add user to cgroup
		mlog "adding child $CHILD to user cgroup $CG_USER"
		cgWrite "$CHILD" $CG_USER/tasks
	else
		elog "error: user cgroup $CG_USER is missing"
	fi
	rmLock
else
	dlog "constrain cores enabled"

	[ -n "$USER" ] || {
		elog "no user env variable for job $JOBID"
	}

	[ -n "$JOB_CPUS" ] || {
		elog "no CPU cores for job $JOBID"
	}

	getLock
	mdsave "$CG_USER"
	if [ -z $(cat $CG_USER/cpuset.cpus) ]; then
		dlog "empty cpuset.cpus"
		cgWrite "$JOB_CPUS" $CG_USER/cpuset.cpus
	else
		OLD_CPU=$(cat $CG_USER/cpuset.cpus)
		dlog "old cpuset.cpus is $OLD_CPU"
		dlog "add cpuset.cpus is $JOB_CPUS"

		if [ "$OLD_CPU" == "JOB_CPUS" ]; then
			dlog "using old cpuset: $OLD_CPU"
		else
			NEW_CPU=$($EXPAND [$OLD_CPU],[$JOB_CPUS] |sort -g -u)
			COMP_CPU=$($COMPRESS $NEW_CPU | tr -d "\[\]")
			dlog "using new cpuset: $COMP_CPU"
			cgWrite "$COMP_CPU" $CG_USER/cpuset.cpus
		fi
	fi

	# set memory nodes from base for now
	MEM_NODES=$(cat $BASE/cpuset.mems)

	cgWrite "$MEM_NODES" $CG_USER/cpuset.mems
	dlog "user cpuset: " $(cat $CG_USER/cpuset.cpus)

	[ -n "$JOBID" ] || elog "no jobid for pid $CHILD user $USER"

	mdsave "$CG_JOB"

	# job cgroup
	dlog "JOB: $JOBID STEP:$STEPID"

	cgWrite "$JOB_CPUS" $CG_JOB/cpuset.cpus
	cgWrite "$MEM_NODES" $CG_JOB/cpuset.mems

	dlog "adding child $CHILD to $CG_JOB/tasks"
	cgWrite "$CHILD" $CG_JOB/tasks
	dlog "job cpuset: " $(cat $CG_JOB/cpuset.cpus)

	# step cgroup
	[ -n "$STEPID" ] && {
		mdsave "$CG_STEP"

		cgWrite "$JOB_CPUS" $CG_STEP/cpuset.cpus
		cgWrite "$MEM_NODES" $CG_STEP/cpuset.mems

		dlog "adding child $CHILD to $CG_STEP/tasks"
		cgWrite "$CHILD" $CG_STEP/tasks
		dlog "step cpuset: " $(cat $CG_STEP/cpuset.cpus)
	}
	rmLock
fi
