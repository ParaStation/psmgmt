#!/usr/bin/env python
#
#               ParaStation
#
# Copyright (C) 2015 Forschungszentrum Juelich GmbH
#
# @author
# Norbert Eicker (n.eicker@fz-juelich.de)
#

from __future__ import print_function

from argparse import ArgumentParser
import os, socket, struct

def getLoggerPID(parser):
    args = parser.parse_args()

    pids = []
    a = os.popen("pgrep psilogger").readlines()
    for p in a: pids.append(int(p))

    if (args.pid):
        if (int(args.pid) in pids): return int(args.pid)

        parser.print_usage()
        print(parser.prog+":", "error:", "pid", int(args.pid), "not found")

        exit(1)

    if (len(pids) == 1): return pids[0]

    parser.print_usage()
    print(parser.prog+":", "error: ", end='')
    if (len(pids) == 0):
        print("no logger detected")
    else:
        print("multiple loggers detected:", pids)

    exit(1)


#
# Main program
#

parser = ArgumentParser(description='Get debugging info from psilogger.')
parser.add_argument('pid', metavar='<pid>', type=int, nargs='?',
                    help='PID of psilogger to debug')

pid = getLoggerPID(parser)

if (pid == 0):
    parser.print_usage()
    exit(1)

sName = '\0psilogger_'+str(pid)+'.sock'

# We have to fill up the name with \0 since Linux interprets the whole sun_path
lName = sName
while (len(lName) < 108): lName += chr(0)

try:
    s = socket.socket(socket.AF_UNIX)
except socket.error as msg:
    print(parser.prog+":", "error:", "cannot create socket:", msg)
    exit(1)

try:
    s.connect(lName)
except socket.error as msg:
    print(parser.prog+":", "error:", "cannot connect to", repr(sName)+":", msg)
    exit(1)

# Just some dummy request for the time being
s.sendall(struct.pack('i', 1234))

while True:
    data = s.recv(1024)
    if not data: break

    print(data.decode("ASCII"), end='')

s.close()
