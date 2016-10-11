#!/bin/bash

umask 023
umask

srun -n 1 /bin/bash -c umask

# Important: explicit return value
exit $?

