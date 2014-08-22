#!/bin/bash

srun --exclusive -n1 sleep 10 &

sleep 2

srun --exclusive -n1 hostname &

wait

exit 0

