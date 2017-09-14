#!/usr/bin/env python

import sys
import os

fail = 0

if "SLURM_MEM_PER_NODE" in os.environ.keys():
	sys.stderr.write("SLURM_MEM_PER_NODE is set when --mem was not given.\n")
	fail = 1

sys.exit(fail)

