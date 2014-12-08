#!/usr/bin/env python

import os
import sys
import subprocess


p = subprocess.Popen(["gcc", "prog.c", "-o", os.environ["PSTEST_OUTDIR"] + "/prog.exe", "-lelf"])
p.wait()

srun = None
for path in os.environ["PATH"].split(os.pathsep):
	x = os.path.join(path, "srun")
	if os.path.isfile(x) and os.access(x, os.X_OK):
		srun = x
		break

assert(srun)

srun = [srun]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd  = [os.environ["PSTEST_OUTDIR"] + "/prog.exe", 
        os.environ["PSTEST_OUTDIR"] + "/mpir_proctable.txt"] + \
        srun + ["-t", "2", "-n", "%d" % (2*int(os.environ["PSTEST_PARTITION_CPUS"])), "sleep", "10"]

p = subprocess.Popen(cmd)

sys.exit(p.wait())

