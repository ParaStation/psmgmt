#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	numbers = [int(x) for x in helper.fproc_stdout_lines(p)]

	test.check(5 == len(numbers))
	test.check(numbers[0] != numbers[1], p)	# If this test fails we need to change the numbers in submit.sh
	test.check(numbers[1] == numbers[2], p)
	test.check(numbers[0] != numbers[1], p)
	test.check(numbers[3] == numbers[4], p)


test.quit()

