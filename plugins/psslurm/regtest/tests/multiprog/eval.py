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

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_WORK_DIR" % P] + \
		           "/output/slurm-" + \
		           os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P] + ".out").read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda x: x.strip(), out.split("\n")) if len(x) > 0]
	Assert(6 == len(lines))
	for line in lines:
		Assert(re.match(r'1:.*', line) or \
		       re.match(r'2:.*', line) or \
		       "3: task:3"   == line or \
		       "4: task:4"   == line or \
		       "0: offset:0" == line or \
		       "5: offset:1" == line, p)

sys.exit(RETVAL)

