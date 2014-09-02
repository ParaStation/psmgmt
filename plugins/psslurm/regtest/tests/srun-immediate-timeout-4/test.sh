#!/bin/bash

for i in $(seq 1 ${PSTEST_PARTITION_CPUS}); do
	srun --exclusive -n1 sleep 10 &
done

sleep 2

srun --exclusive -n1 hostname &

wait

