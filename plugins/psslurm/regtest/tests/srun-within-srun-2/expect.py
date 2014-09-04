#!/usr/bin/env python

import os
import sys
import subprocess

cmd = "srun -v -N 1 -t 1 -p %s " % os.environ["PSTEST_PARTITION"]
if "" != os.environ["PSTEST_RESERVATION"]:
	cmd += "--reservation %s " % os.environ["PSTEST_RESERVATION"]
cmd += "--pty /bin/bash"

stdin = """
set timeout -1

spawn %s

expect -re "tasks started"
expect -re "bash" { send "srun -n 1 hostname\n" }
expect -re "bash" { send "echo $?\n" }
expect -re "bash" { send "exit\n" }
expect eof

catch wait result
exit [lindex $result 3]
""" % cmd

p = subprocess.Popen(["/usr/bin/expect", "-"], stdin = subprocess.PIPE)
p.communicate(stdin)

sys.exit(p.wait())

