#!/bin/bash

/usr/bin/env PMI_FD=NOSUCHFD srun -n 1 /usr/bin/env | grep PMI_FD

# Important: explicit return value
exit $?

