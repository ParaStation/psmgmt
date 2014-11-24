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
		print("[[0, 65537], [0, 16777472], [0, 65537], [0, 16777472], [0, 131074], [0, 33554944], [0, 131074], [0, 33554944], [0, 262148], [0, 67109888], [0, 262148], [0, 67109888], [0, 524296], [0, 134219776], [0, 524296], [0, 134219776]]")

		test.check(0 == result[0][0], p)
		test.check(65537 == result[0][1], p)

		test.check(0 == result[1][0], p)
		test.check(16777472 == result[1][1], p)

		test.check(0 == result[2][0], p)
		test.check(65537 == result[2][1], p)

		test.check(0 == result[3][0], p)
		test.check(16777472 == result[3][1], p)

		test.check(0 == result[4][0], p)
		test.check(131074 == result[4][1], p)

		test.check(0 == result[5][0], p)
		test.check(33554944 == result[5][1], p)

		test.check(0 == result[6][0], p)
		test.check(131074 == result[6][1], p)

		test.check(0 == result[7][0], p)
		test.check(33554944 == result[7][1], p)

		test.check(0 == result[8][0], p)
		test.check(262148 == result[8][1], p)

		test.check(0 == result[9][0], p)
		test.check(67109888 == result[9][1], p)

		test.check(0 == result[10][0], p)
		test.check(262148 == result[10][1], p)

		test.check(0 == result[11][0], p)
		test.check(67109888 == result[11][1], p)

		test.check(0 == result[12][0], p)
		test.check(524296 == result[12][1], p)

		test.check(0 == result[13][0], p)
		test.check(134219776 == result[13][1], p)

		test.check(0 == result[14][0], p)
		test.check(524296 == result[14][1], p)

		test.check(0 == result[15][0], p)
		test.check(134219776 == result[15][1], p)

	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
