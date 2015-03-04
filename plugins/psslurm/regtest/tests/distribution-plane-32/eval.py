#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 64, p)

	nodes = helper.job_node_list(p)

	nodeno = {}
	for i, n in enumerate(nodes):
		nodeno[n] = i

	try:
		result = [None] * len(lines)
		for line in lines:
			rank, host, mask = line.split()
			result[int(rank)] = [nodeno[host], int(mask, base = 16)]

		test.check(0 == result[0][0], p)
		test.check(1 == result[0][1], p)

		test.check(0 == result[1][0], p)
		test.check(2 == result[1][1], p)

		test.check(0 == result[2][0], p)
		test.check(4 == result[2][1], p)

		test.check(0 == result[3][0], p)
		test.check(8 == result[3][1], p)

		test.check(0 == result[4][0], p)
		test.check(16 == result[4][1], p)

		test.check(0 == result[5][0], p)
		test.check(32 == result[5][1], p)

		test.check(0 == result[6][0], p)
		test.check(64 == result[6][1], p)

		test.check(0 == result[7][0], p)
		test.check(128 == result[7][1], p)

		test.check(0 == result[8][0], p)
		test.check(256 == result[8][1], p)

		test.check(0 == result[9][0], p)
		test.check(512 == result[9][1], p)

		test.check(0 == result[10][0], p)
		test.check(1024 == result[10][1], p)

		test.check(0 == result[11][0], p)
		test.check(2048 == result[11][1], p)

		test.check(0 == result[12][0], p)
		test.check(4096 == result[12][1], p)

		test.check(0 == result[13][0], p)
		test.check(8192 == result[13][1], p)

		test.check(0 == result[14][0], p)
		test.check(16384 == result[14][1], p)

		test.check(0 == result[15][0], p)
		test.check(32768 == result[15][1], p)

		test.check(0 == result[16][0], p)
		test.check(65536 == result[16][1], p)

		test.check(0 == result[17][0], p)
		test.check(131072 == result[17][1], p)

		test.check(0 == result[18][0], p)
		test.check(262144 == result[18][1], p)

		test.check(0 == result[19][0], p)
		test.check(524288 == result[19][1], p)

		test.check(0 == result[20][0], p)
		test.check(1048576 == result[20][1], p)

		test.check(0 == result[21][0], p)
		test.check(2097152 == result[21][1], p)

		test.check(0 == result[22][0], p)
		test.check(4194304 == result[22][1], p)

		test.check(0 == result[23][0], p)
		test.check(8388608 == result[23][1], p)

		test.check(0 == result[24][0], p)
		test.check(16777216 == result[24][1], p)

		test.check(0 == result[25][0], p)
		test.check(33554432 == result[25][1], p)

		test.check(0 == result[26][0], p)
		test.check(67108864 == result[26][1], p)

		test.check(0 == result[27][0], p)
		test.check(134217728 == result[27][1], p)

		test.check(0 == result[28][0], p)
		test.check(268435456 == result[28][1], p)

		test.check(0 == result[29][0], p)
		test.check(536870912 == result[29][1], p)

		test.check(0 == result[30][0], p)
		test.check(1073741824 == result[30][1], p)

		test.check(0 == result[31][0], p)
		test.check(2147483648 == result[31][1], p)

		test.check(1 == result[32][0], p)
		test.check(1 == result[32][1], p)

		test.check(1 == result[33][0], p)
		test.check(2 == result[33][1], p)

		test.check(1 == result[34][0], p)
		test.check(4 == result[34][1], p)

		test.check(1 == result[35][0], p)
		test.check(8 == result[35][1], p)

		test.check(1 == result[36][0], p)
		test.check(16 == result[36][1], p)

		test.check(1 == result[37][0], p)
		test.check(32 == result[37][1], p)

		test.check(1 == result[38][0], p)
		test.check(64 == result[38][1], p)

		test.check(1 == result[39][0], p)
		test.check(128 == result[39][1], p)

		test.check(1 == result[40][0], p)
		test.check(256 == result[40][1], p)

		test.check(1 == result[41][0], p)
		test.check(512 == result[41][1], p)

		test.check(1 == result[42][0], p)
		test.check(1024 == result[42][1], p)

		test.check(1 == result[43][0], p)
		test.check(2048 == result[43][1], p)

		test.check(1 == result[44][0], p)
		test.check(4096 == result[44][1], p)

		test.check(1 == result[45][0], p)
		test.check(8192 == result[45][1], p)

		test.check(1 == result[46][0], p)
		test.check(16384 == result[46][1], p)

		test.check(1 == result[47][0], p)
		test.check(32768 == result[47][1], p)

		test.check(1 == result[48][0], p)
		test.check(65536 == result[48][1], p)

		test.check(1 == result[49][0], p)
		test.check(131072 == result[49][1], p)

		test.check(1 == result[50][0], p)
		test.check(262144 == result[50][1], p)

		test.check(1 == result[51][0], p)
		test.check(524288 == result[51][1], p)

		test.check(1 == result[52][0], p)
		test.check(1048576 == result[52][1], p)

		test.check(1 == result[53][0], p)
		test.check(2097152 == result[53][1], p)

		test.check(1 == result[54][0], p)
		test.check(4194304 == result[54][1], p)

		test.check(1 == result[55][0], p)
		test.check(8388608 == result[55][1], p)

		test.check(1 == result[56][0], p)
		test.check(16777216 == result[56][1], p)

		test.check(1 == result[57][0], p)
		test.check(33554432 == result[57][1], p)

		test.check(1 == result[58][0], p)
		test.check(67108864 == result[58][1], p)

		test.check(1 == result[59][0], p)
		test.check(134217728 == result[59][1], p)

		test.check(1 == result[60][0], p)
		test.check(268435456 == result[60][1], p)

		test.check(1 == result[61][0], p)
		test.check(536870912 == result[61][1], p)

		test.check(1 == result[62][0], p)
		test.check(1073741824 == result[62][1], p)

		test.check(1 == result[63][0], p)
		test.check(2147483648 == result[63][1], p)

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
