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

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P])
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P])

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_WORK_DIR" % P] + \
		           "/slurm-" + \
		           os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P] + ".out").read()
	except Exception as e:
		Assert(not str(e), str(e))

sys.exit(RETVAL)

