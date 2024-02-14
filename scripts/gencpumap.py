#!/usr/bin/python
#
#               ParaStation
#
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# author
# Stephan Krempel (krempel@par-tec.com)
#
"""
Calculates psid cpumap from /proc/cpuinfo.

Does only work if the following keys are present there:
    - processor
    - physical id
    - core id

"""


import sys
from collections import namedtuple
from operator import attrgetter

infile = "/proc/cpuinfo"

try:
    infile = sys.argv[1]
except IndexError:
    pass

with open(infile, encoding="UTF-8") as f:
    output = f.read()

HWThread = namedtuple("HWThread", ["thread", "socket", "core"])

thread = -1
socket = -1
siblings = -1
core = -1
cores = -1

threads = []

for line in output.splitlines():
    if ":" in line:
        key, value = line.split(":", 1)
        key = key.strip()

        if key == "processor":
            if thread >= 0:
                threads.append(HWThread(thread, socket, core))
            thread = int(value.strip())

        if key == "physical id":
            socket = int(value.strip())

        if key == "core id":
            core = int(value.strip())

        if key == "siblings":
            value = int(value.strip())
            if siblings != -1 and siblings != value:
                print(f"Multiple numers of siblings found: {siblings} != {value}")
                exit(-1)
            siblings = value

        if key == "cpu cores":
            value = int(value)
            if cores != -1 and cores != value:
                print(f"Multiple numers of cores found: {cores} != {value}")
                exit(-1)
            cores = value

if thread >= 0:
    threads.append(HWThread(thread, socket, core))

sockets = socket + 1  # last thread is always at last core at socket

print("Found:")
print(" %2d sockets" % (sockets))
print(" %2d cores per socket" % (cores))
print(" %2d threads per socket" % (siblings))

totalCores = cores * sockets
threadsPerCore = siblings / cores
totalThreads = siblings * sockets

if len(threads) != totalThreads:
    print(
        "Number of thread entries found does not match calculated"
        " number of threads (%d != %d)" % (len(threads), totalThreads)
    )
    exit(-1)


print("Thus:")
print(" %2d cores in total" % (totalCores))
print(" %2d threads per core" % (threadsPerCore))
print(" %2d threads in total" % (totalThreads))

print("Original order:")
for t in threads:
    print(t)


threads.sort(key=attrgetter("core"))
threads.sort(key=attrgetter("socket"))

sthreads = list(None for i in range(totalThreads))
i = 0
for entry in threads:
    sthreads[i] = entry
    i += totalCores
    if i / totalThreads >= 1:
        i += 1
    i %= totalThreads

print("Sorted:")
for t in sthreads:
    print(t)

print("psmgmt Mapping:")
for t in sthreads:
    print(t.thread, end=" ")
print()
