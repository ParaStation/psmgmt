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
	"partitions": ["batch", "psslurm"],
	"reservations": ["", "psslurm"],
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
import traceback
import re
import pprint

RETVAL = 0

def Assert(x, msg = None):
	global RETVAL

	if not x:
		if msg:
			sys.stderr.write("Test failure ('%s'):\\n" % msg)
		else:
			sys.stderr.write("Test failure:\\n")
		map(lambda x: sys.stderr.write("\\t" + x.strip() + "\\n"), traceback.format_stack())
		RETVAL = 1

for p in [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]:
	P = p.upper()

	Assert("0:0" == os.environ["PSTEST_SCONTROL_%s_EXIT_CODE" % P], p)
	Assert("COMPLETED" == os.environ["PSTEST_SCONTROL_%s_JOB_STATE" % P], p)

	try:
		out = open(os.environ["PSTEST_SCONTROL_%s_STD_OUT" % P]).read()
		err = open(os.environ["PSTEST_SCONTROL_%s_STD_ERR" % P]).read()
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

	try:
		lines = [x for x in map(lambda z: z.strip(), out.split("\\n")) if len(x) > 0]
		Assert(6 == len(lines), p)

		Assert(lines[0] == lines[1], p)
		Assert(lines[0] == lines[2], p)
		
		if not re.match(r'.*cannot modify limit.*', err):
			Assert(lines[3] != lines[0], p)
			Assert(lines[3] == lines[4], p)
			Assert(lines[3] == lines[5], p)
	except Exception as e:
		Assert(1 == 0, p + ": " + str(e))

sys.exit(RETVAL)
"""

	open("%s/eval.py" % k, "w").write(evl)
	os.chmod("%s/eval.py" % k, stat.S_IRWXU)

