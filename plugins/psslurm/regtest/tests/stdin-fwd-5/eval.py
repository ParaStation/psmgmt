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
	try:
		out = open("output/fproc-%s.out" % p).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda x: x.strip(), out.split("\n")) if len(x) > 0]
	Assert(64 == len(lines), p)
	Assert(1 == len([x for x in lines if re.match(r'.*\'OK\'.*', x)]), p)

	try:
		line = [x for x in lines if re.match(r'.*\'OK\'.*', x)][0]
		Assert("5\t'OK'" == line)
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))


sys.exit(RETVAL)

