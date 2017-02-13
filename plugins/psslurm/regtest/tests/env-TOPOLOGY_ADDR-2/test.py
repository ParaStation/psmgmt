#!/usr/bin/env python

import sys
import os

keys = ["SLURM_TOPOLOGY_ADDR", \
        "SLURM_TOPOLOGY_ADDR_PATTERN"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

