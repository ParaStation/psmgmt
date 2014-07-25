#!/usr/bin/env python

import sys
import os
import traceback
import re
import pprint

RETVAL = 0

def Assert(x, msg = None):
	global RETVAL

	if not x:
		if msg:
			sys.stderr.write("Test failure ('%s'):\n" % msg)
		else:
			sys.stderr.write("Test failure:\n")
		map(lambda x: sys.stderr.write("\t" + x.strip() + "\n"), traceback.format_stack())
		RETVAL = 1

# env = {}
print(os.environ)


for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	try:
		for i in range(3):
			Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_%d_EXIT_CODE" % (P, i)], p)
			Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_%d_JOB_STATE" % (P, i)], p)

			Assert(i == int(os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_TASK_ID" % (P, i)]), p)

			Assert(os.path.isfile(os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]), p)
			Assert(os.path.isfile(os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]), p)

			Assert(re.match(r'.*slurm-%s_%d.out' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], i), \
			                                        os.environ["PSTEST_SCONTROL_%s_%d_STD_OUT" % (P, i)]), p)
			Assert(re.match(r'.*slurm-%s_%d.out' % (os.environ["PSTEST_SCONTROL_%s_%d_ARRAY_JOB_ID" % (P, i)], i), \
			                                        os.environ["PSTEST_SCONTROL_%s_%d_STD_ERR" % (P, i)]), p)

		Assert(os.environ["PSTEST_SCONTROL_%s_0_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_1_JOB_ID" % P], p)
		Assert(os.environ["PSTEST_SCONTROL_%s_0_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_2_JOB_ID" % P], p)
		Assert(os.environ["PSTEST_SCONTROL_%s_1_JOB_ID" % P] != \
		       os.environ["PSTEST_SCONTROL_%s_2_JOB_ID" % P], p)

	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

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


sys.exit(RETVAL)

