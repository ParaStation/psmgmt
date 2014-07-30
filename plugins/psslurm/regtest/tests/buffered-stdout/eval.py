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

env = {}

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	try:
		out = open(os.environ["PSTEST_FPROC_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda x: x.strip(), out.split("\n")) if len(x) > 0]

	x = float(lines[ 0].split(':')[0])
	y = float(lines[-1].split(':')[0])

	Assert(y - x < 0.5, p)


sys.exit(RETVAL)

