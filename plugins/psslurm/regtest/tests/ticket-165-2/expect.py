#!/usr/bin/env python

import os
import sys
import subprocess


srun = ["srun"]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = srun + ["-v", "-N", "1", "-t", "2", "-J", os.environ["PSTEST_TESTKEY"], "--pty", "/bin/bash"]

stdin = """
set timeout -1

spawn %s

expect -re "launching"
expect -re "tasks started"
expect -re "bash" { send "JOB_NAME=\$(scontrol show job -o \${SLURM_JOB_ID} | python2 job-name.py)\n" }
expect -re "bash" { send "gcc prog1.c -o output-\${JOB_NAME}/prog1.exe\n" }
expect -re "bash" { send "gcc prog2.c -o output-\${JOB_NAME}/prog2.exe\n" }
expect -re "bash" { send "for SZ in 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 ; do output-\${JOB_NAME}/prog1.exe \${SZ} | srun -n 4 output-\${JOB_NAME}/prog2.exe \${SZ} > output-\${JOB_NAME}/\${SZ}.txt 2>&1 ; done\n" }
expect -re "bash" { send "exit\n" }
expect eof

catch wait result
exit [lindex $result 3]
""" % " ".join(cmd)

env = os.environ.copy()
env["PS1"] = "bash$ "

p = subprocess.Popen(["/usr/bin/expect", "-"], stdin = subprocess.PIPE, env = env)
p.communicate(stdin)

sys.exit(p.wait())

