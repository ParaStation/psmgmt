#!/usr/bin/env python

import sys
import os
import traceback
import re
import pprint
import subprocess

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


N = 60
def split60(line):
	tmp = []

	n = N + 1
	while '' != line:
		tmp.append(line[0:n].strip())
		line = line[n:]	

	return tmp

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:1" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("TIMEOUT" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	q = subprocess.Popen(["sacct", "-o", "ALL%%-%d" % N, "-j", os.environ["PSTEST_SCONTROL_%s_JOB_ID" % P]], \
	                     stdout = subprocess.PIPE,
	                     stderr = subprocess.PIPE)
	
	x, _ = q.communicate()
	q.wait()
	
	lines = x.split('\n')	# Important: Do not strip here
	keys  = split60(lines[0])

	Assert(3 == len([x for x in map(lambda z: z.strip(), lines) if len(x) > 0]), p)
	line = lines[2]
	
	d = {}
	for i, v in enumerate(split60(line)):
		d[keys[i]] = v
	
	print("%s:" % p)
	pprint.pprint(d, indent = 1)

	Assert(len(d["MaxPagesNode"]) > 0, p)
	Assert(0 == int(d["MaxPagesTask"]), p)
	Assert(len(d["MaxRSSNode"]) > 0, p)
	Assert(0 == int(d["MaxRSSTask"]), p)
	Assert(len(d["MaxVMSizeNode"]) > 0, p)
	Assert(0 == int(d["MaxVMSizeTask"]), p)
	Assert(len(d["MinCPUNode"]) > 0, p)
	Assert(0 == int(d["MinCPUTask"]), p)
	Assert(not re.match(r'.*G', d["MaxVMSize"]), p)
	Assert("00:00:00" != d["TotalCPU"], p)
	Assert("00:00:00" != d["UserCPU"], p)
	Assert("00:00:00" != d["SystemCPU"], p)

sys.exit(RETVAL)

