#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 48, p)

	nodes = helper.job_node_list(p)

	nodeno = {}
	for i, n in enumerate(nodes):
		nodeno[n] = i

	try:
		result = [None] * len(lines)
		for line in lines:
			rank, host, mask = line.split()
			result[int(rank)] = [nodeno[host], int(mask, base = 16)]

		print(result)
		print("[[0, 1], [0, 256], [0, 65536], [0, 16777216], [0, 2], [0, 512], [0, 131072], [0, 33554432], [0, 4], [0, 1024], [0, 262144], [0, 67108864], [0, 8], [0, 2048], [0, 524288], [0, 134217728], [0, 16], [0, 4096], [0, 1048576], [0, 268435456], [0, 32], [0, 8192], [0, 2097152], [0, 536870912], [1, 1], [1, 256], [1, 65536], [1, 16777216], [1, 2], [1, 512], [1, 131072], [1, 33554432], [1, 4], [1, 1024], [1, 262144], [1, 67108864], [1, 8], [1, 2048], [1, 524288], [1, 134217728], [1, 16], [1, 4096], [1, 1048576], [1, 268435456], [1, 32], [1, 8192], [1, 2097152], [1, 536870912]]")

		test.check(0 == result[0][0], p)
#		test.check(1 == result[0][1], p)

		test.check(0 == result[1][0], p)
#		test.check(256 == result[1][1], p)

		test.check(0 == result[2][0], p)
#		test.check(65536 == result[2][1], p)

		test.check(0 == result[3][0], p)
#		test.check(16777216 == result[3][1], p)

		test.check(0 == result[4][0], p)
#		test.check(2 == result[4][1], p)

		test.check(0 == result[5][0], p)
#		test.check(512 == result[5][1], p)

		test.check(0 == result[6][0], p)
#		test.check(131072 == result[6][1], p)

		test.check(0 == result[7][0], p)
#		test.check(33554432 == result[7][1], p)

		test.check(0 == result[8][0], p)
#		test.check(4 == result[8][1], p)

		test.check(0 == result[9][0], p)
#		test.check(1024 == result[9][1], p)

		test.check(0 == result[10][0], p)
#		test.check(262144 == result[10][1], p)

		test.check(0 == result[11][0], p)
#		test.check(67108864 == result[11][1], p)

		test.check(0 == result[12][0], p)
#		test.check(8 == result[12][1], p)

		test.check(0 == result[13][0], p)
#		test.check(2048 == result[13][1], p)

		test.check(0 == result[14][0], p)
#		test.check(524288 == result[14][1], p)

		test.check(0 == result[15][0], p)
#		test.check(134217728 == result[15][1], p)

		test.check(0 == result[16][0], p)
#		test.check(16 == result[16][1], p)

		test.check(0 == result[17][0], p)
#		test.check(4096 == result[17][1], p)

		test.check(0 == result[18][0], p)
#		test.check(1048576 == result[18][1], p)

		test.check(0 == result[19][0], p)
#		test.check(268435456 == result[19][1], p)

		test.check(0 == result[20][0], p)
#		test.check(32 == result[20][1], p)

		test.check(0 == result[21][0], p)
#		test.check(8192 == result[21][1], p)

		test.check(0 == result[22][0], p)
#		test.check(2097152 == result[22][1], p)

		test.check(0 == result[23][0], p)
#		test.check(536870912 == result[23][1], p)

		test.check(1 == result[24][0], p)
#		test.check(1 == result[24][1], p)

		test.check(1 == result[25][0], p)
#		test.check(256 == result[25][1], p)

		test.check(1 == result[26][0], p)
#		test.check(65536 == result[26][1], p)

		test.check(1 == result[27][0], p)
#		test.check(16777216 == result[27][1], p)

		test.check(1 == result[28][0], p)
#		test.check(2 == result[28][1], p)

		test.check(1 == result[29][0], p)
#		test.check(512 == result[29][1], p)

		test.check(1 == result[30][0], p)
#		test.check(131072 == result[30][1], p)

		test.check(1 == result[31][0], p)
#		test.check(33554432 == result[31][1], p)

		test.check(1 == result[32][0], p)
#		test.check(4 == result[32][1], p)

		test.check(1 == result[33][0], p)
#		test.check(1024 == result[33][1], p)

		test.check(1 == result[34][0], p)
#		test.check(262144 == result[34][1], p)

		test.check(1 == result[35][0], p)
#		test.check(67108864 == result[35][1], p)

		test.check(1 == result[36][0], p)
#		test.check(8 == result[36][1], p)

		test.check(1 == result[37][0], p)
#		test.check(2048 == result[37][1], p)

		test.check(1 == result[38][0], p)
#		test.check(524288 == result[38][1], p)

		test.check(1 == result[39][0], p)
#		test.check(134217728 == result[39][1], p)

		test.check(1 == result[40][0], p)
#		test.check(16 == result[40][1], p)

		test.check(1 == result[41][0], p)
#		test.check(4096 == result[41][1], p)

		test.check(1 == result[42][0], p)
#		test.check(1048576 == result[42][1], p)

		test.check(1 == result[43][0], p)
#		test.check(268435456 == result[43][1], p)

		test.check(1 == result[44][0], p)
#		test.check(32 == result[44][1], p)

		test.check(1 == result[45][0], p)
#		test.check(8192 == result[45][1], p)

		test.check(1 == result[46][0], p)
#		test.check(2097152 == result[46][1], p)

		test.check(1 == result[47][0], p)
#		test.check(536870912 == result[47][1], p)

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
