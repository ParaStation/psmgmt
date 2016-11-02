#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	P = p.upper()

	try:
		for i in range(3):
			test.check("0:0" == os.environ["PSTEST_SCONTROL_%s_%d_EXIT_CODE" % (P, i)], p)
			test.check("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_%d_JOB_STATE" % (P, i)], p)

			jobid  = os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)]
			taskid = os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_TASK_ID" % (P, i)]

			# In Slurm 14.03 the ith job has task id i. In Slurm 16.05 the ith job in the list has
			# task id (n - i), i.e., the order is reverseed. We accept either one as long as there
			# is no chaos.
			test.check(int(taskid) in [i, (2 - i)], p)

			out = os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]
			err = os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]

			# Fix a Slurm bug
			err = re.sub(r'%A', jobid , err)
			err = re.sub(r'%a', taskid, err)

			test.check(os.path.isfile(out), p)
			test.check(os.path.isfile(err), p)

			test.check(re.match(r'.*slurm-%s-x-%s.out' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], taskid), out), p)
			test.check(re.match(r'.*slurm-%s-x-%s.err' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], taskid), err), p)

			lines = [x for x in map(lambda z: z.strip(), open(out, "r").read().split("\n")) \
			             if len(x) > 0 and not re.match(r'Submitted.*', x)]
			test.check(1 == len(lines), p)
			test.check("OK" == lines[0], p)

			lines = [x for x in map(lambda z: z.strip(), open(err, "r").read().split("\n")) \
			             if len(x) > 0 and not re.match(r'sbatch:.*', x)]
			test.check(1 == len(lines), p)
			test.check("NOK" == lines[0], p)

			out = "%s/slurm-%s-z-%s.out" % (os.environ["PSTEST_OUTDIR"], jobid, taskid)
			err = "%s/slurm-%s-z-%s.err" % (os.environ["PSTEST_OUTDIR"], jobid, taskid)

			lines = [x for x in map(lambda z: z.strip(), open(out, "r").read().split("\n")) if len(x) > 0]
			test.check(1 == len(lines), p)
			test.check("OK" == lines[0], p)

			lines = [x for x in map(lambda z: z.strip(), open(err, "r").read().split("\n")) \
			             if len(x) > 0 and not re.match(r'srun:.*', x)]
			test.check(1 == len(lines), p)
			test.check("NOK" == lines[0], p)
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()

