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
		Assert(len(lines) == 64, p)

		result = [None] * len(lines)
		for line in lines:
			rank, host, mask = line.split()
			result[int(rank)] = [nodeno[host], int(mask, base = 16)]

		Assert(0 == result[0][0], p)
		Assert(1 == result[0][1], p)

		Assert(0 == result[1][0], p)
		Assert(65536 == result[1][1], p)

		Assert(0 == result[2][0], p)
		Assert(2 == result[2][1], p)

		Assert(0 == result[3][0], p)
		Assert(131072 == result[3][1], p)

		Assert(0 == result[4][0], p)
		Assert(4 == result[4][1], p)

		Assert(0 == result[5][0], p)
		Assert(262144 == result[5][1], p)

		Assert(0 == result[6][0], p)
		Assert(8 == result[6][1], p)

		Assert(0 == result[7][0], p)
		Assert(524288 == result[7][1], p)

		Assert(0 == result[8][0], p)
		Assert(16 == result[8][1], p)

		Assert(0 == result[9][0], p)
		Assert(1048576 == result[9][1], p)

		Assert(0 == result[10][0], p)
		Assert(32 == result[10][1], p)

		Assert(0 == result[11][0], p)
		Assert(2097152 == result[11][1], p)

		Assert(0 == result[12][0], p)
		Assert(64 == result[12][1], p)

		Assert(0 == result[13][0], p)
		Assert(4194304 == result[13][1], p)

		Assert(0 == result[14][0], p)
		Assert(128 == result[14][1], p)

		Assert(0 == result[15][0], p)
		Assert(8388608 == result[15][1], p)

		Assert(0 == result[16][0], p)
		Assert(256 == result[16][1], p)

		Assert(0 == result[17][0], p)
		Assert(16777216 == result[17][1], p)

		Assert(0 == result[18][0], p)
		Assert(512 == result[18][1], p)

		Assert(1 == result[19][0], p)
		Assert(1 == result[19][1], p)

		Assert(1 == result[20][0], p)
		Assert(65536 == result[20][1], p)

		Assert(1 == result[21][0], p)
		Assert(2 == result[21][1], p)

		Assert(1 == result[22][0], p)
		Assert(131072 == result[22][1], p)

		Assert(1 == result[23][0], p)
		Assert(4 == result[23][1], p)

		Assert(1 == result[24][0], p)
		Assert(262144 == result[24][1], p)

		Assert(1 == result[25][0], p)
		Assert(8 == result[25][1], p)

		Assert(1 == result[26][0], p)
		Assert(524288 == result[26][1], p)

		Assert(1 == result[27][0], p)
		Assert(16 == result[27][1], p)

		Assert(1 == result[28][0], p)
		Assert(1048576 == result[28][1], p)

		Assert(1 == result[29][0], p)
		Assert(32 == result[29][1], p)

		Assert(1 == result[30][0], p)
		Assert(2097152 == result[30][1], p)

		Assert(1 == result[31][0], p)
		Assert(64 == result[31][1], p)

		Assert(1 == result[32][0], p)
		Assert(4194304 == result[32][1], p)

		Assert(1 == result[33][0], p)
		Assert(128 == result[33][1], p)

		Assert(1 == result[34][0], p)
		Assert(8388608 == result[34][1], p)

		Assert(1 == result[35][0], p)
		Assert(256 == result[35][1], p)

		Assert(1 == result[36][0], p)
		Assert(16777216 == result[36][1], p)

		Assert(1 == result[37][0], p)
		Assert(512 == result[37][1], p)

		Assert(0 == result[38][0], p)
		Assert(33554432 == result[38][1], p)

		Assert(0 == result[39][0], p)
		Assert(1024 == result[39][1], p)

		Assert(0 == result[40][0], p)
		Assert(67108864 == result[40][1], p)

		Assert(0 == result[41][0], p)
		Assert(2048 == result[41][1], p)

		Assert(0 == result[42][0], p)
		Assert(134217728 == result[42][1], p)

		Assert(0 == result[43][0], p)
		Assert(4096 == result[43][1], p)

		Assert(0 == result[44][0], p)
		Assert(268435456 == result[44][1], p)

		Assert(0 == result[45][0], p)
		Assert(8192 == result[45][1], p)

		Assert(0 == result[46][0], p)
		Assert(536870912 == result[46][1], p)

		Assert(0 == result[47][0], p)
		Assert(16384 == result[47][1], p)

		Assert(0 == result[48][0], p)
		Assert(1073741824 == result[48][1], p)

		Assert(0 == result[49][0], p)
		Assert(32768 == result[49][1], p)

		Assert(0 == result[50][0], p)
		Assert(2147483648 == result[50][1], p)

		Assert(1 == result[51][0], p)
		Assert(33554432 == result[51][1], p)

		Assert(1 == result[52][0], p)
		Assert(1024 == result[52][1], p)

		Assert(1 == result[53][0], p)
		Assert(67108864 == result[53][1], p)

		Assert(1 == result[54][0], p)
		Assert(2048 == result[54][1], p)

		Assert(1 == result[55][0], p)
		Assert(134217728 == result[55][1], p)

		Assert(1 == result[56][0], p)
		Assert(4096 == result[56][1], p)

		Assert(1 == result[57][0], p)
		Assert(268435456 == result[57][1], p)

		Assert(1 == result[58][0], p)
		Assert(8192 == result[58][1], p)

		Assert(1 == result[59][0], p)
		Assert(536870912 == result[59][1], p)

		Assert(1 == result[60][0], p)
		Assert(16384 == result[60][1], p)

		Assert(1 == result[61][0], p)
		Assert(1073741824 == result[61][1], p)

		Assert(1 == result[62][0], p)
		Assert(32768 == result[62][1], p)

		Assert(1 == result[63][0], p)
		Assert(2147483648 == result[63][1], p)

	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
