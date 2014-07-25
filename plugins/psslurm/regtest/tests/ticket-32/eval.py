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

	Assert("16:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("FAILED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_ERR" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines   = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]

	for i in range(16):
		Assert(1 == len([x for x in lines if re.match(r'srun: error:.*Exited with exit code %d$' % (i + 1), x)]), p) 


sys.exit(RETVAL)

