#!/usr/bin/env python

import os
import sys
import subprocess
import select
import time
import re
import signal

start = time.time()

cmd  = ["srun", "-v", "-X", "-n", "1", "-t", "2" ]
cmd += ["-o", "%s/slurm-%s.out" % (os.environ["PSTEST_OUTDIR"], os.environ["PSTEST_PARTITION"])]
cmd += ["-p", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	cmd += ["--reservation", os.environ["PSTEST_RESERVATION"]]
cmd += ["./loop.sh"]

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
			if re.match(r'.*tasks started.*', line):
				time.sleep(30)
				p.send_signal(signal.SIGINT)
		
			sys.stdout.write(("%.2f:\t" % (time.time() - start)) + line)

