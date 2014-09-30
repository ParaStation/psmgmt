#!/usr/bin/env python

import sys
import os

keys = ["SLURM_CPU_BIND_LIST", \
        "SLURM_CPU_BIND", \
        "SLURM_CPU_BIND_TYPE", \
        "SLURM_CPU_BIND_VERBOSE"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

