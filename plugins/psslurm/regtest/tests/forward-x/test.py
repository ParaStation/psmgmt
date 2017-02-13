#!/usr/bin/python2

import sys
import os
import re

if 1 == len(re.findall(r"%s:[0-9\.]+" % os.environ["SLURM_SUBMIT_HOST"], os.environ["DISPLAY"])):
        sys.exit(0)
else:
        sys.exit(1)

