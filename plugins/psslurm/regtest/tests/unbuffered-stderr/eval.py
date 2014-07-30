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
		err = open(os.environ["PSTEST_FPROC_%s_STD_ERR" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	lines = [x for x in map(lambda x: x.strip(), err.split("\n")) if len(x) > 0]
	
	# Find the beginning of "Hello"
	i = [x for x in range(len(lines)) if lines[x].split(':')[1].strip() == 'H'][0]

	x = float(lines[ i].split(':')[0])
	y = float(lines[-1].split(':')[0])
	z = float(lines[-2].split(':')[0])

	Assert(y - x > 20.0, p)
	Assert(y - z >  8.0, p)


sys.exit(RETVAL)

