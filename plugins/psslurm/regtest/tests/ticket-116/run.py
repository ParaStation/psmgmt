#!/usr/bin/env python

import os
import sys
import subprocess


testsuite = "/".join(os.getcwd().split("/")[:-2] + ["expect"])
if "SLURM_TESTSUITE" in os.environ.keys():
	testsuite = os.environ["SLURM_TESTSUITE"]

os.environ["SBATCH_PARTITION"] = os.environ["PSTEST_PARTITION"]
os.environ["SALLOC_PARTITION"] = os.environ["PSTEST_PARTITION"]
os.environ["SLURM_PARTITION"]  = os.environ["PSTEST_PARTITION"]

if "" != os.environ["PSTEST_RESERVATION"]:
	os.environ["SBATCH_RESERVATION"] = os.environ["PSTEST_RESERVATION"]
	os.environ["SALLOC_RESERVATION"] = os.environ["PSTEST_RESERVATION"]
	os.environ["SLURM_RESERVATION"]  = os.environ["PSTEST_RESERVATION"]

for x in ["globals", "test16.4", "test16.4.prog.c"]:
	os.symlink(testsuite + "/" + x, os.environ["PSTEST_OUTDIR"] + "/" + x)

p = subprocess.Popen([os.environ["PSTEST_OUTDIR"] + "/test16.4"], \
                     cwd = os.environ["PSTEST_OUTDIR"])

sys.exit(p.wait())

