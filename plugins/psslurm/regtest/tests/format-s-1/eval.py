#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	jobid = helper.job_id(p)
	hosts = helper.job_node_list(p)

	for suffix in ["out", "err"]:
		for i in range(8):
			fn = "%s/slurm-%s-%d.%s" % (os.environ["PSTEST_OUTDIR"], \
			                            os.environ["PSTEST_SCONTROL_%s_JOB_ID" % p.upper()], \
			                            i, suffix)

			test.check(os.path.isfile(fn), p)
			try:
				lines = [x for x in map(lambda z: z.strip(), open(fn, "r").read().split("\n")) if len(x) > 0]
				lines = sorted(lines, key = lambda z: z.split()[0])

				test.check(8 == len(lines), p)
				for j in range(4):
					test.check("%d %s %d" % (2*j    , hosts[j], i) == lines[2*j    ], p)
					test.check("%d %s %d" % (2*j + 1, hosts[j], i) == lines[2*j + 1], p)
			except Exception as e:
				test.check(1 == 0, p + ": " + str(e))

test.quit()

