#!/bin/bash

/usr/bin/env PMI_RANK=NOSUCHRANK srun -n 1 /usr/bin/env | grep PMI_RANK

# Important: explicit return value
exit $?

