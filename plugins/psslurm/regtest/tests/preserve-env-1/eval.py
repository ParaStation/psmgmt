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

	env = {}

	for line in out.split("\n"):
		x = line.split("=")
		z = x[0].strip()
		if len(z) > 0:
			env[z] = "=".join(x[1:])

	print("%s:" % p)
	pprint.pprint(env, indent = 1)

	Assert("1" == env["SLURM_NNODES"], p)
	Assert(not "SLURM_NTASKS" in env.keys(), p)
	Assert(not "SLURM_NPROCS" in env.keys(), p)

sys.exit(RETVAL)

