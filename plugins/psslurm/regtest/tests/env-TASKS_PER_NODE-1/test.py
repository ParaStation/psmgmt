#!/usr/bin/env python

import sys
import os

keys = ["SLURM_TASKS_PER_NODE"]

fail = 0

for k in keys:
	if not k in os.environ.keys():
		sys.stderr.write("'%s' is missing in the environment.\n" % k)
		fail = 1

sys.exit(fail)

