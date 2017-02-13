#!/bin/bash

srun -n $((${PSTEST_PARTITION_CPUS}/2)) fail.py

