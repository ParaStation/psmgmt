#!/usr/bin/env python

import os
import sys
import subprocess
import select
import time


f = None
for i in range(100):
	try:
		f = "/tmp/ticket-159-%s-%08d" % (os.environ["PSTEST_OUTDIR"].split("-")[-1], i)
		os.mkdir(f)
		break
	except Exception:
		f = None

assert(f)

srun = ["srun"]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = srun + ["-N", "1", "-n", "1", "-t", "1", "hostname"]

p    = subprocess.Popen(cmd, \
                        stdout = subprocess.PIPE, \
                        stderr = subprocess.PIPE, \
                        stdin  = subprocess.PIPE, \
                        cwd = f)
o, e = p.communicate()
ret  = p.wait()

sys.stdout.write(o)
sys.stderr.write(e)

try:
	os.rmdir(f)
except Exception:
	pass

sys.exit(ret)

