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
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
		err = open(os.environ["PSTEST_SCONTROL_%s_STD_ERR" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	try:
		lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]
		Assert(6 == len(lines), p)

		Assert(lines[0] == lines[1], p)
		Assert(lines[0] == lines[2], p)
		
		if not re.match(r'.*cannot modify limit.*', err):
			Assert(lines[3] != lines[0], p)
			Assert(lines[3] == lines[4], p)
			Assert(lines[3] == lines[5], p)
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
