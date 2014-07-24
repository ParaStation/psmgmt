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

	prologf = "%s/prolog-%s.txt" % (os.environ["PSTEST_OUTDIR"], jobid)
	epilogf = "%s/epilog-%s.txt" % (os.environ["PSTEST_OUTDIR"], jobid)

	out = ""
	try:
		out = open(prologf).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))
	Assert("OK\n" == out, p)

	out = ""
	try:
		out = open(epilogf).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))
	Assert("OK\n" == out, p)

sys.exit(RETVAL)

