#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	lines = helper.fproc_stderr_lines(p)

	# Find the beginning of "Hello"
	i = [x for x in range(len(lines)) if lines[x].split(':')[1].strip() == 'H'][0]

	x = float(lines[ i].split(':')[0])
	y = float(lines[-1].split(':')[0])
	z = float(lines[-2].split(':')[0])

	test.check(y - x > 20.0, p)
	test.check(y - z >  8.0, p)


test.quit()

