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

	jobid = os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P]
	
	try:
		fil = [x for x in os.listdir(os.environ["PSTEST_OUTDIR"]) if re.match(r'slurm-%s\.txt.*' % jobid, x)][0]
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	try:
		out = open(os.environ["PSTEST_OUTDIR"] + "/" + fil).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))
	Assert("OK\n" == out, p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]
	Assert(3 == len(lines), p)
	Assert("0" == lines[0], p)
	Assert("0" == lines[1], p)
	Assert("1" == lines[2], p)

sys.exit(RETVAL)

