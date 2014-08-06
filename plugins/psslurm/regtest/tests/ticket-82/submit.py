#!/usr/bin/env python

import os
import sys
import subprocess
import time

cmd  = ["srun", "--mincpus=10000", "-n", "1", "-t", "2"]
cmd += ["-o", "%s/slurm-%s.out" % (os.environ["PSTEST_OUTDIR"], os.environ["PSTEST_PARTITION"])]
cmd += ["-p", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	cmd += ["--reservation", os.environ["PSTEST_RESERVATION"]]
cmd += ["hostname"]

p = subprocess.Popen(cmd, \
                     stdout = subprocess.PIPE,
                     stderr = subprocess.PIPE)

time.sleep(30)

x = p.poll()
if None != x:
	out, err = p.communicate()

	sys.stdout.write(out)
	sys.stdout.write(err)

	sys.stdout.write("%d\n" % x)

	sys.exit(0)
else:
	p.terminate()
	sys.exit(1)

