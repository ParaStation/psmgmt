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

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P])
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P])

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	tmp = {}

	for line in out.split("\n"):
		x = line.split("=")
		z = x[0].strip()
		if len(z) > 0:
			tmp[z] = "=".join(x[1:])

	env[p] = tmp

for p, e in env.iteritems():
	print("%s:" % p)
	pprint.pprint(e, indent = 1)

for p, e in env.iteritems():
	for q, f in env.iteritems():
		if p == q:
			continue

		for k, v in e.iteritems():
			Assert(k in f.keys(), "k = %s, p = %s, q = %s" % (k, p, q))

sys.exit(RETVAL)

