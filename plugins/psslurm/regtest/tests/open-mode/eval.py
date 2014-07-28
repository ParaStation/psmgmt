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
		outA = open(os.environ["PSTEST_OUTDIR"] + "/slurm-" + \
		            os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P] + "-A.out").read()
		outB = open(os.environ["PSTEST_OUTDIR"] + "/slurm-" + \
		            os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P] + "-B.out").read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	try:
		lines = [x for x in map(lambda z: z.strip(), outA.split("\n")) if len(x) > 0]
		Assert(len(lines) == 2, p)

		Assert("1" == lines[0], p)
		Assert("2" == lines[1], p)

		lines = [x for x in map(lambda z: z.strip(), outB.split("\n")) if len(x) > 0]
		Assert(len(lines) == 1, p)

		Assert("2" == lines[0], p)
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
