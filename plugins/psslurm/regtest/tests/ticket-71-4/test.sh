#!/bin/bash

export OMP_NUM_THREADS=${PSTEST_PARTITION_CPUS}
srun -n 2 hostname

