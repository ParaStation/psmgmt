#!/usr/bin/env python

import os
import sys
import subprocess


testsuite = "/".join(os.getcwd().split("/")[:-2] + ["expect"])
if "SLURM_TESTSUITE" in os.environ.keys():
	testsuite = os.environ["SLURM_TESTSUITE"]

if "" != os.environ["PSTEST_PARTITION"]:
	os.environ["SBATCH_PARTITION"] = os.environ["PSTEST_PARTITION"]
	os.environ["SALLOC_PARTITION"] = os.environ["PSTEST_PARTITION"]
	os.environ["SLURM_PARTITION"]  = os.environ["PSTEST_PARTITION"]

if "" != os.environ["PSTEST_RESERVATION"]:
	os.environ["SBATCH_RESERVATION"] = os.environ["PSTEST_RESERVATION"]
	os.environ["SALLOC_RESERVATION"] = os.environ["PSTEST_RESERVATION"]
	os.environ["SLURM_RESERVATION"]  = os.environ["PSTEST_RESERVATION"]

if "" != os.environ["PSTEST_QOS"]:
	os.environ["SBATCH_QOS"] = os.environ["PSTEST_QOS"]
	os.environ["SALLOC_QOS"] = os.environ["PSTEST_QOS"]
	os.environ["SLURM_QOS"]  = os.environ["PSTEST_QOS"]

if "" != os.environ["PSTEST_ACCOUNT"]:
	os.environ["SBATCH_ACCOUNT"] = os.environ["PSTEST_ACCOUNT"]
	os.environ["SALLOC_ACCOUNT"] = os.environ["PSTEST_ACCOUNT"]
	os.environ["SLURM_ACCOUNT"]  = os.environ["PSTEST_ACCOUNT"]

for x in ["globals", "test2.13"]:
	os.symlink(testsuite + "/" + x, os.environ["PSTEST_OUTDIR"] + "/" + x)

p = subprocess.Popen([os.environ["PSTEST_OUTDIR"] + "/test2.13"], \
                     cwd = os.environ["PSTEST_OUTDIR"])

sys.exit(p.wait())

