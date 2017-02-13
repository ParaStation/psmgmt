#!/bin/bash

SSH="/usr/bin/ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no"

H1=$(srun -N 1 -n 1 -r 0 /bin/hostname)
H2=$(srun -N 1 -n 1 -r 1 /bin/hostname)

echo ${H1}
${SSH} ${H1} /bin/hostname
echo ${H2}
${SSH} ${H2} /bin/hostname

# Important: explicit return value
exit $?

