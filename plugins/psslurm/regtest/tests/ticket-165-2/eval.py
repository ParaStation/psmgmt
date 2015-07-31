#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	for i in range(4, 16):
		fn = os.environ["PSTEST_OUTDIR"] + "/%d.txt" % (2**i)
			
		test.check(4*(2**i + 1) == os.stat(fn).st_size, p)

		for line in [x for x in map(lambda z: z.strip(), open(fn, "r").readlines()) if len(x) > 0]:
			test.check(not re.match(r'.*SIGPIPE.*' , line) and \
			           not re.match(r'.*do_write.*', line), p + ": %d" % (2**i))

test.quit()
