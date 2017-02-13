#!/usr/bin/env python

import sys
import os
import re
import pprint

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stderr_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]
	rx    = re.compile(r'([0-9]+): .*mask 0x([0-9a-fA-F]+) .*')

	matches = filter(lambda z: None != z, map(lambda u: rx.match(u), lines))
	test.check(24 == len(matches))

	for x in matches:
		rank = int(x.group(1))
		mask = int('0x' + x.group(2), 16)

		test.check(mask == (1 << rank))

test.quit()

