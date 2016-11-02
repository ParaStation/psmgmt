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

			taskid = os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_TASK_ID" % (P, i)]
			# In Slurm 14.03 the ith job has task id i. In Slurm 16.05 the ith job in the list has
			# task id (n - i), i.e., the order is reverseed. We accept either one as long as there
			# is no chaos.
			test.check(int(taskid) in [i, (2 - i)], p)

			test.check(os.path.isfile(os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]), p)
			test.check(os.path.isfile(os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]), p)

			test.check(re.match(r'.*slurm-%s_%s.out' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], taskid), \
			                                        os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]), p)
			test.check(re.match(r'.*slurm-%s_%s.out' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], taskid), \
			                                        os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]), p)

		test.check(os.environ["PSTEST_SCONTROL_%s_0_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_1_JOB_ID" % P], p)
		test.check(os.environ["PSTEST_SCONTROL_%s_0_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_2_JOB_ID" % P], p)
		test.check(os.environ["PSTEST_SCONTROL_%s_1_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_2_JOB_ID" % P], p)

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

# Cleanup
for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
        P = p.upper()

        for i in range(3):
		fn = os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]
		if os.path.isfile(fn):
			os.rename(fn, os.environ["PSTEST_OUTDIR"] + "/%s" % os.path.basename(fn))
		fn = os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]
		if os.path.isfile(fn):
			os.rename(fn, os.environ["PSTEST_OUTDIR"] + "/%s" % os.path.basename(fn))


test.quit()

