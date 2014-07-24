#!/usr/bin/env python

import sys


sys.stdout.write("srun -n 1 hostname\n")
sys.stdout.write("echo $?\n")
sys.stdout.write("exit\n")

