#!/bin/bash

export OMP_NUM_THREADS=$((${PSTEST_PARTITION_CPUS}/2))
srun -n 4 hostname

