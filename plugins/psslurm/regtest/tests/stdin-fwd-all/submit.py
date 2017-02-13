#!/usr/bin/env python

import os
import sys
import subprocess
import select
import re

srun = ["srun"]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = srun + ["-v", "-N", "2", "-n", "%d" % (2*int(os.environ["PSTEST_PARTITION_CPUS"])), "-t", "2", "./read.py"]

p = subprocess.Popen(cmd, \
                     stdout = subprocess.PIPE, \
                     stderr = subprocess.PIPE, \
                     stdin  = subprocess.PIPE)

interact = 0

stdout = ""
stderr = ""

while 1:
	x = p.poll()
	if None != x:
		sys.stdout.write(stdout)
		sys.stderr.write(stderr)
		sys.exit(x)

	ready, _, _ = select.select([p.stdout, p.stderr], [], [], 1)
	for x in ready:
		line = x.readline()
		if "" != line.strip():
			if re.match(r'.*tasks started.*', line):
				p.stdin.write("OK\n")
				interact = 0

		if p.stdout == x:
			stdout += line
		if p.stderr == x:
			stderr += line	

