#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

inplines = [x for x in map(lambda z: z.strip(), open(os.environ["PSTEST_OUTDIR"] + "/../input.txt", "r").readlines()) if len(x) > 0]

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	for i in range(4):
		lines1 = [x for x in map(lambda z: z.strip(), open(os.environ["PSTEST_OUTDIR"] + "/slurm-1-%d.out" % i, "r").readlines()) if len(x) > 0]
		lines2 = [x for x in map(lambda z: z.strip(), open(os.environ["PSTEST_OUTDIR"] + "/slurm-2-%d.out" % i, "r").readlines()) if len(x) > 0]

		test.check("".join(inplines) == "".join(lines1), p)
		test.check("".join(inplines) == "".join(lines2), p)

test.quit()

