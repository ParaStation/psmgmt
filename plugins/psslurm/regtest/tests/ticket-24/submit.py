#!/usr/bin/env python

import os
import sys
import subprocess
import select
import time
import re
import signal

start = time.time()

srun = ["srun"]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = srun + ["-n", "1", "-t", "2", "./loop.sh"]

p = subprocess.Popen(cmd, \
                     stdout = subprocess.PIPE,
                     stderr = subprocess.PIPE)

while 1:
	x = p.poll()
	if None != x:
		sys.exit(x)
	
	ready, _, _ = select.select([p.stdout, p.stderr], [], [], 1)
	for x in ready:
		line = x.readline()
		if "" != line.strip():
			if re.match(r'.*allocated resources.*', line):
				time.sleep(1)
				for i in range(10):
					p.send_signal(signal.SIGINT)
					time.sleep(0.1)
		
			sys.stdout.write(("%.2f:\t" % (time.time() - start)) + line)

