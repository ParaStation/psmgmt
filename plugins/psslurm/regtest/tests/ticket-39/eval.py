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

stdout = {}

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	Assert(os.path.isfile("output/slurm-%s.out" % os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P]), p)
	Assert(not os.path.isfile("slurm-%s.out" % os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P]), p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if "i = 10" == x]
	Assert(4 == len(lines), p)


sys.exit(RETVAL)

