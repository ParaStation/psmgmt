#!/usr/bin/env python

import os
import sys
import subprocess
import select
import time


start = time.time()

cmd = ["srun", "-N", "1", "-n", "1", "-t", "3", "-p", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	cmd += ["--reservation", os.environ["PSTEST_RESERVATION"]]
cmd += ["--unbuffered", "./hello.py"]

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

	ready, _, _ = select.select([p.stdout, p.stderr], [], [])
	for x in ready:
		tmp = x.read(1)
		if 0 == len(tmp):
			continue
		
		if "\n" != tmp:
			tmp += "\n"

		if p.stderr == x:
			stderr += ("%.2f:\t" % (time.time() - start)) + tmp
		if p.stdout == x:
			stdout += ("%.2f:\t" % (time.time() - start)) + tmp

