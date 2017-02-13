#!/bin/bash

/usr/bin/env PMI_SIZE=NOSUCHSIZE srun -n 1 /usr/bin/env | grep PMI_SIZE

# Important: explicit return value
exit $?

