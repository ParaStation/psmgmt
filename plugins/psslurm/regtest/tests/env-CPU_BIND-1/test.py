#!/usr/bin/env python

import sys
import os

keys = ["SBATCH_CPU_BIND_LIST", \
        "SBATCH_CPU_BIND_VERBOSE", \
        "SBATCH_CPU_BIND_TYPE", \
        "SBATCH_CPU_BIND"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

