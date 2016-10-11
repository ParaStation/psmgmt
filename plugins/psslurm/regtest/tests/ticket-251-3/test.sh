#!/bin/bash

srun --mpi=none -n 1 /usr/bin/env | grep PMI_RANK

# Important: explicit return value
exit 0

