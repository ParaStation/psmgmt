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

def expand1(matchobj):
	tmp0, tmp1 = matchobj.group(0).split('-')

	assert(len(tmp0) == len(tmp1))
	fmt = "%%0%dd" % len(tmp0)

	return ",".join([fmt % z for z in range(int(tmp0), int(tmp1)+1)])

def expand2(matchobj):
	tmp0, tmp1 = matchobj.group(0).replace(']', '').split('[')
	return ",".join([tmp0 + x for x in tmp1.split(',')])

pprint.pprint(os.environ, indent = 1)

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	# FIXME Juropa-3 specific
	nodes = re.sub(r'j3c([0-9]*\[[0-9,]+\])', expand2, 	               re.sub(r'([0-9]+)-([0-9]+)', expand1, 	                      os.environ["PSTEST_SCONTROL_%s_NODE_LIST" % P])).split(',')

	nodeno = {}
	for i, n in enumerate(nodes):
		nodeno[n] = i

	try:
		lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]
		Assert(len(lines) == 32, p)

		result = [None] * len(lines)
		for line in lines:
			rank, host, mask = line.split()
			result[int(rank)] = [nodeno[host], int(mask, base = 16)]

		Assert(0 == result[0][0], p)
		Assert(65537 == result[0][1], p)

		Assert(0 == result[1][0], p)
		Assert(16777472 == result[1][1], p)

		Assert(0 == result[2][0], p)
		Assert(65537 == result[2][1], p)

		Assert(0 == result[3][0], p)
		Assert(16777472 == result[3][1], p)

		Assert(0 == result[4][0], p)
		Assert(131074 == result[4][1], p)

		Assert(0 == result[5][0], p)
		Assert(33554944 == result[5][1], p)

		Assert(0 == result[6][0], p)
		Assert(131074 == result[6][1], p)

		Assert(0 == result[7][0], p)
		Assert(33554944 == result[7][1], p)

		Assert(0 == result[8][0], p)
		Assert(262148 == result[8][1], p)

		Assert(0 == result[9][0], p)
		Assert(67109888 == result[9][1], p)

		Assert(0 == result[10][0], p)
		Assert(262148 == result[10][1], p)

		Assert(0 == result[11][0], p)
		Assert(67109888 == result[11][1], p)

		Assert(0 == result[12][0], p)
		Assert(524296 == result[12][1], p)

		Assert(0 == result[13][0], p)
		Assert(134219776 == result[13][1], p)

		Assert(0 == result[14][0], p)
		Assert(524296 == result[14][1], p)

		Assert(0 == result[15][0], p)
		Assert(134219776 == result[15][1], p)

		Assert(1 == result[16][0], p)
		Assert(65537 == result[16][1], p)

		Assert(1 == result[17][0], p)
		Assert(16777472 == result[17][1], p)

		Assert(1 == result[18][0], p)
		Assert(65537 == result[18][1], p)

		Assert(1 == result[19][0], p)
		Assert(16777472 == result[19][1], p)

		Assert(1 == result[20][0], p)
		Assert(131074 == result[20][1], p)

		Assert(1 == result[21][0], p)
		Assert(33554944 == result[21][1], p)

		Assert(1 == result[22][0], p)
		Assert(131074 == result[22][1], p)

		Assert(1 == result[23][0], p)
		Assert(33554944 == result[23][1], p)

		Assert(1 == result[24][0], p)
		Assert(262148 == result[24][1], p)

		Assert(1 == result[25][0], p)
		Assert(67109888 == result[25][1], p)

		Assert(1 == result[26][0], p)
		Assert(262148 == result[26][1], p)

		Assert(1 == result[27][0], p)
		Assert(67109888 == result[27][1], p)

		Assert(1 == result[28][0], p)
		Assert(524296 == result[28][1], p)

		Assert(1 == result[29][0], p)
		Assert(134219776 == result[29][1], p)

		Assert(1 == result[30][0], p)
		Assert(524296 == result[30][1], p)

		Assert(1 == result[31][0], p)
		Assert(134219776 == result[31][1], p)

	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
