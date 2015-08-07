#!/usr/bin/env python

import os
import sys
import stat


prog = """

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int get_process_rank()
{
	const char* str;
	char* tailptr;

	str = getenv("SLURM_PROCID");
	if (str) {
		return (int )strtol(str, &tailptr, 0);
	}

	str = getenv("PMI_RANK");
	if (str) {
		return (int )strtol(str, &tailptr, 0);
	}

	return -1;
}


int main(int argc, char** argv)
{
        int rank;
        struct timespec ts;
 
        rank = get_process_rank();

        if (1 == rank) {
                kill(getpid(), %s);
        } else {
                memset(&ts, 0, sizeof(ts));
                ts.tv_sec = 2;
                nanosleep(&ts, NULL);
        }

        return 0;
}
"""

signals = [
	("SIGHUP", "Hangup"),
	("SIGINT", "Interrupt"),
	("SIGQUIT", "Quit"),
	("SIGILL", "Illegal instruction"),
	("SIGTRAP", "Trace/breakpoint trap"),
	("SIGABRT", "Aborted"),
	("SIGFPE", "Floating point exception"),
	("SIGKILL", "Killed"),
	("SIGBUS", "Bus error"),
	("SIGSEGV", "Segmentation fault"),
	("SIGPIPE", "Broken pipe"),
#	("SIGALRM", "Alarm clock"),
	("SIGTERM", "Terminated"),
#	("SIGURG", "Urgent I/O condition"),
#	("SIGSTOP", "Stopped (signal)"),
#	("SIGTSTP", "Stopped"),
#	("SIGCONT", "Continued"),
#	("SIGCHLD", "Child exited"),
#	("SIGTTIN", "Stopped (tty input)"),
#	("SIGTTOU", "Stopped (tty output)"),
	("SIGIO", "I/O possible"),
	("SIGXCPU", "CPU time limit exceeded"),
	("SIGXFSZ", "File size limit exceeded"),
	("SIGVTALRM", "Virtual timer expired"),
	("SIGPROF", "Profiling timer expired"),
	("SIGUSR1", "User defined signal 1"),
	("SIGUSR2", "User defined signal 2")]

for signal, descr in signals:
	k = "ticket-203-%s" % signal

	if not os.path.isdir(k):
		os.mkdir(k)

	open("%s/descr.json" % k, "w").write("""{
	"type":	"batch",
        "submit": "salloc -N 1 -t 1 ./test.sh",
        "eval": ["eval.py"],
        "fproc": null,
        "monitor_hz": 10
}
""")

	open("%s/prog.c" % k, "w").write(prog % signal)

	open("%s/test.sh" % k, "w").write("""#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc prog.c -o output-${JOB_NAME}/prog.exe
srun -n 2 output-${JOB_NAME}/prog.exe

""")
	os.chmod("%s/test.sh" % k, stat.S_IRWXU)

	evl = """#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	rx  = re.compile(r'.*%s.*', re.MULTILINE | re.DOTALL)
	err = helper.job_stderr(p)

	test.check(re.match(rx, err), p)

test.quit()
""" % descr

	open("%s/eval.py" % k, "w").write(evl)
	os.chmod("%s/eval.py" % k, stat.S_IRWXU)

