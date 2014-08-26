#!/usr/bin/env python

import sys
import os
import re
import pprint

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

stdout = {}

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	for i in range(10):
		fn = "%s/slurm-%s-%d.out" % (os.environ["PSTEST_OUTDIR"], helper.job_id(p), i)

		if i > 0 and not os.path.isfile(fn):
			break

		lines = [x for x in map(lambda z: z.strip(), open(fn, "r").readlines()) if len(x) > 0]
		nodes = helper.job_node_list(p)

		for line in lines:
			tmp = [x for x in map(lambda z: z.strip(), line.split(':')) if len(x) > 0]

			test.check(2 == len(tmp), p)
			test.check(tmp[1] == nodes[int(tmp[0])], p)

test.quit()

