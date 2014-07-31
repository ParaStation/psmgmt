#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
        helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)

	env = {}

	for line in lines:
		x = line.split("=")
		z = x[0].strip()
		if len(z) > 0:
			env[z] = "=".join(x[1:])

	print("%s:" % p)
	helper.pretty_print_dict(env)

	test.check("5" == env["SLURM_NNODES"], p)
	test.check(not "SLURM_NTASKS" in env.keys(), p)
	test.check(not "SLURM_NPROCS" in env.keys(), p)


test.quit()

