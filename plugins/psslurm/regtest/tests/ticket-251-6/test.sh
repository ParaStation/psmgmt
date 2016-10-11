#!/bin/bash

/usr/bin/env PMI_SIZE=NOSUCHSIZE srun --mpi=none -n 1 /usr/bin/env | grep PMI_SIZE

# Important: explicit return value
exit 0

