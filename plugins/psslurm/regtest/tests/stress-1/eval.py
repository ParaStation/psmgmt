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

pprint.pprint(os.environ, indent = 1)

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	try:
		count = 0
		lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]
		for line in lines:
			Assert("0" == line or "11" == line, p)
			if "11" == line:
				count += 1

		Assert(count < 10, p + ": count = %d" % count)	# number of failures < 5%

	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
