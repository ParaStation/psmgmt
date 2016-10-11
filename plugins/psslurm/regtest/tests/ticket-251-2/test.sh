#!/bin/bash

/usr/bin/env PMI_FD=NOSUCHFD srun --mpi=none -n 1 /usr/bin/env | grep PMI_FD

# Important: explicit return value
exit 0

