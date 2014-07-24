#!/usr/bin/env python

import os
import sys
import subprocess

cmd = "srun -N 2 -t 1 -p %s " % os.environ["PSTEST_PARTITION"]
if "" != os.environ["PSTEST_RESERVATION"]:
	cmd += "--reservation %s " % os.environ["PSTEST_RESERVATION"]
cmd += "--pty /bin/bash"

stdin = """
spawn %s
expect {
        -re "queued and waiting for resources" {
                exp_continue
        }
        -re "has been allocated resources" {
                sleep 1
                send "exit\n"
                exp_continue
        }
        timeout {
                exp_continue
        }
        eof
}

catch wait result
exit [lindex $result 3]
""" % cmd

p = subprocess.Popen(["/usr/bin/expect", "-"], stdin = subprocess.PIPE)
p.communicate(stdin)

sys.exit(p.wait())

