#!/bin/bash

srun -l -n 24 --cpu_bind=verbose,rank true

# Important: explicit return value
exit $?

