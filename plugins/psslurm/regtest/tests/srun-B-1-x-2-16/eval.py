#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 16, p)

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
		print("[[0, 1], [0, 256], [0, 65536], [0, 16777216], [0, 2], [0, 512], [0, 131072], [0, 33554432], [0, 4], [0, 1024], [0, 262144], [0, 67108864], [0, 8], [0, 2048], [0, 524288], [0, 134217728]]")

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

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
