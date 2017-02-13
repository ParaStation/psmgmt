#!/bin/bash

srun -n ${PSTEST_PARTITION_CPUS} -l hostname

# Important: explicit return value
exit $?

