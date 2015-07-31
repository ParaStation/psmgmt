#!/usr/bin/env python

import os
import sys
import stat


tests = {
	"propagate-AS"		: ["-v", "AS", "%d" % (128*1024*1024)],
	"propagate-CORE"	: ["-c", "CORE", "%d" % (128*1024*1024)],
	"propagate-CPU"		: ["-t", "CPU", "600"],
	"propagate-DATA"	: ["-d", "DATA", "%d" % (128*1024*1024)],
	"propagate-FSIZE"	: ["-f", "FSIZE", "%d" % (128*1024*1024)],
	"propagate-MEMLOCK"	: ["-l", "MEMLOCK", "%d" % (128*1024*1024)],
	"propagate-NOFILE"	: ["-n", "NOFILE", "512"],
	"propagate-NPROC"	: ["-u", "NPROC", "512"],
	"propagate-RSS"		: ["-m", "RSS", "%d" % (128*1024)],
	"propagate-STACK"	: ["-s", "STACK", "5120"]
}

for k, v in tests.iteritems():
	if not os.path.isdir(k):
		os.mkdir(k)

	open("%s/descr.json" % k, "w").write("""{
	"type":	"batch",
	"submit": "sbatch -N 1 -t 1 ./test.sh",
	"eval": ["eval.py"],
	"fproc": null,
	"monitor_hz": 10
}
""")

	open("%s/ulimit.sh" % k, "w").write("""#!/bin/bash
ulimit %s
""" % v[0])
	os.chmod("%s/ulimit.sh" % k, stat.S_IRWXU)

	open("%s/test.sh" % k, "w").write("""#!/bin/bash

ulimit %s
srun -n 1 --propagate=%s  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit %s %s

ulimit %s
srun -n 1 --propagate=%s  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

""" % (v[0], v[1], v[0], v[2], v[0], v[1]))
	os.chmod("%s/test.sh" % k, stat.S_IRWXU)

	evl = """#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]
	err   = helper.job_stderr(p)

	test.check(6 == len(lines), p)

	test.check(lines[0] == lines[1], p)
	test.check(lines[0] == lines[2], p)

	if not re.match(r'.*cannot modify limit.*', err):
		test.check(lines[3] != lines[0], p)
		test.check(lines[3] == lines[4], p)
		test.check(lines[3] == lines[5], p)

test.quit()
"""

	open("%s/eval.py" % k, "w").write(evl)
	os.chmod("%s/eval.py" % k, stat.S_IRWXU)

