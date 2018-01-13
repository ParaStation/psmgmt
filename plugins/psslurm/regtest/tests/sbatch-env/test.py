#!/usr/bin/env python

import sys
import os

keys = ["SLURM_CHECKPOINT_IMAGE_DIR", \
        "SLURM_CPUS_ON_NODE", \
        "SLURMD_NODENAME", \
        "SLURM_GTIDS", \
        "SLURM_JOB_CPUS_PER_NODE", \
        "SLURM_JOB_ID", \
        "SLURM_JOBID", \
        "SLURM_JOB_NODELIST", \
        "SLURM_JOB_NUM_NODES", \
        "SLURM_JOB_PARTITION", \
        "SLURM_JOB_UID", \
        "SLURM_JOB_USER", \
        "SLURM_LOCALID", \
        "SLURM_NNODES", \
        "SLURM_NODE_ALIASES", \
        "SLURM_NODEID", \
        "SLURM_NODELIST", \
        "SLURM_PRIO_PROCESS", \
        "SLURM_PROCID", \
        "SLURM_SUBMIT_DIR", \
        "SLURM_SUBMIT_HOST", \
        "SLURM_TASK_PID"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

