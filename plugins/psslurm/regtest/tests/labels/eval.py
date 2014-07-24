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

stdout = {}

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	tmp = []
	for line in out.split("\n"):
		# TODO This is Juropa 3 specific
		tmp.append(re.sub(r'j3c.*$', "", line))

	stdout[p] = tmp

for p, e in stdout.iteritems():
	print("%s:" % p)
	pprint.pprint(e, indent = 1)

for p, e in stdout.iteritems():
	for q, f in stdout.iteritems():
		if p == q:
			continue

		Assert(len(e) == len(f))

		for x in e:
			Assert(1 == len([z for z in f if z.strip() == x.strip()]), \
			       "x = %s, p = %s, q = %s" % (x, p, q))

sys.exit(RETVAL)

