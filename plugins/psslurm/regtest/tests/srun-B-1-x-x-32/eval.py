#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 32, p)

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
		print("[[0, 1], [0, 256], [0, 65536], [0, 16777216], [0, 2], [0, 512], [0, 131072], [0, 33554432], [0, 4], [0, 1024], [0, 262144], [0, 67108864], [0, 8], [0, 2048], [0, 524288], [0, 134217728], [0, 16], [0, 4096], [0, 1048576], [0, 268435456], [0, 32], [0, 8192], [0, 2097152], [0, 536870912], [0, 64], [0, 16384], [0, 4194304], [0, 1073741824], [0, 128], [0, 32768], [0, 8388608], [0, 2147483648]]")

		test.check(0 == result[0][0], p)
		test.check(1 == result[0][1], p)

		test.check(0 == result[1][0], p)
		test.check(256 == result[1][1], p)

		test.check(0 == result[2][0], p)
		test.check(65536 == result[2][1], p)

		test.check(0 == result[3][0], p)
		test.check(16777216 == result[3][1], p)

		test.check(0 == result[4][0], p)
		test.check(2 == result[4][1], p)

		test.check(0 == result[5][0], p)
		test.check(512 == result[5][1], p)

		test.check(0 == result[6][0], p)
		test.check(131072 == result[6][1], p)

		test.check(0 == result[7][0], p)
		test.check(33554432 == result[7][1], p)

		test.check(0 == result[8][0], p)
		test.check(4 == result[8][1], p)

		test.check(0 == result[9][0], p)
		test.check(1024 == result[9][1], p)

		test.check(0 == result[10][0], p)
		test.check(262144 == result[10][1], p)

		test.check(0 == result[11][0], p)
		test.check(67108864 == result[11][1], p)

		test.check(0 == result[12][0], p)
		test.check(8 == result[12][1], p)

		test.check(0 == result[13][0], p)
		test.check(2048 == result[13][1], p)

		test.check(0 == result[14][0], p)
		test.check(524288 == result[14][1], p)

		test.check(0 == result[15][0], p)
		test.check(134217728 == result[15][1], p)

		test.check(0 == result[16][0], p)
		test.check(16 == result[16][1], p)

		test.check(0 == result[17][0], p)
		test.check(4096 == result[17][1], p)

		test.check(0 == result[18][0], p)
		test.check(1048576 == result[18][1], p)

		test.check(0 == result[19][0], p)
		test.check(268435456 == result[19][1], p)

		test.check(0 == result[20][0], p)
		test.check(32 == result[20][1], p)

		test.check(0 == result[21][0], p)
		test.check(8192 == result[21][1], p)

		test.check(0 == result[22][0], p)
		test.check(2097152 == result[22][1], p)

		test.check(0 == result[23][0], p)
		test.check(536870912 == result[23][1], p)

		test.check(0 == result[24][0], p)
		test.check(64 == result[24][1], p)

		test.check(0 == result[25][0], p)
		test.check(16384 == result[25][1], p)

		test.check(0 == result[26][0], p)
		test.check(4194304 == result[26][1], p)

		test.check(0 == result[27][0], p)
		test.check(1073741824 == result[27][1], p)

		test.check(0 == result[28][0], p)
		test.check(128 == result[28][1], p)

		test.check(0 == result[29][0], p)
		test.check(32768 == result[29][1], p)

		test.check(0 == result[30][0], p)
		test.check(8388608 == result[30][1], p)

		test.check(0 == result[31][0], p)
		test.check(2147483648 == result[31][1], p)

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
