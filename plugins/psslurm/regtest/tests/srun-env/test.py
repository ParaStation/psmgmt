#!/usr/bin/env python

import sys
import os

keys = ["SLURM_CHECKPOINT_IMAGE_DIR", \
        "SLURM_CPUS_ON_NODE", \
        "SLURM_DISTRIBUTION", \
        "SLURMD_NODENAME", \
        "SLURM_GTIDS", \
        "SLURM_JOB_CPUS_PER_NODE", \
        "SLURM_JOB_ID", \
        "SLURM_JOBID", \
        "SLURM_JOB_NAME", \
        "SLURM_JOB_NUM_NODES", \
        "SLURM_JOB_PARTITION", \
        "SLURM_JOB_UID", \
        "SLURM_JOB_USER", \
        "SLURM_LAUNCH_NODE_IPADDR", \
        "SLURM_LOCALID", \
        "SLURM_NNODES", \
        "SLURM_NODEID", \
        "SLURM_NODELIST", \
        "SLURM_NPROCS", \
        "SLURM_NTASKS", \
        "SLURM_PRIO_PROCESS", \
        "SLURM_PROCID", \
        "SLURM_SRUN_COMM_HOST", \
        "SLURM_SRUN_COMM_PORT", \
        "SLURM_STEP_ID", \
        "SLURM_STEPID", \
        "SLURM_STEP_LAUNCHER_PORT", \
        "SLURM_STEP_NODELIST", \
        "SLURM_STEP_NUM_NODES", \
        "SLURM_STEP_NUM_TASKS", \
        "SLURM_STEP_TASKS_PER_NODE", \
        "SLURM_SUBMIT_DIR", \
        "SLURM_SUBMIT_HOST", \
        "SLURM_TASK_PID", \
        "SLURM_TASKS_PER_NODE"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

