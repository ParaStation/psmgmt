#!/usr/bin/env python3
#
# ParaStation
#
# Copyright (C) 2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

"""
This script simulates power and energy consumption based on whether a job
is running on the node. It maintains state in a temporary file to accumulate
energy over time.

Setting the following environment variables in psid may change the
default range in which the random number for power consumption is selected.

For nodes running a job:
PSACCOUNT_SIM_ENERGY_JOB_MIN, PSACCOUNT_SIM_ENERGY_JOB_MAX

Similar for nodes without a job:
PSACCOUNT_SIM_ENERGY_NO_JOB_MIN, PSACCOUNT_SIM_ENERGY_NO_JOB_MAX
"""

import json
import os
import random
import socket
import subprocess  # nosec B404
import time


def main():
    """
    main function doing all the work
    """

    # state file path in /dev/shm
    hostname = socket.gethostname()
    state_file = os.path.join("/dev/shm", f"energy_sim_{hostname}.json")  # nosec B108

    # Load previous state if it exists
    if os.path.exists(state_file):
        with open(state_file, encoding="utf-8") as f:
            state = json.load(f)
        last_time = state["last_time"]
        accu_energy = state["accu_energy"]
    else:
        # Initialize for first run
        last_time = time.time()
        accu_energy = 0.0

    # Determine if the node is busy
    try:
        proc = subprocess.run(  # nosec B603 B607
            ["squeue", "--nodelist", hostname, "-h", "-o", "%T"],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode == 0:
            has_job = any("RUNNING" in line for line in proc.stdout.splitlines())
        else:
            sys.stderr.write("error: squeue failed to get node state\n")
            sys.stderr.write(proc.stderr)
            sys.exit(1)
    except FileNotFoundError:
        sys.stderr.write("error: squeue binary not found\n")
        sys.exit(1)

    # Simulate current power based on node status
    if has_job:
        eng_min = float(os.environ.get("PSACCOUNT_SIM_ENERGY_JOB_MIN", 300))
        eng_max = float(os.environ.get("PSACCOUNT_SIM_ENERGY_JOB_MAX", 500))
    else:
        eng_min = float(os.environ.get("PSACCOUNT_SIM_ENERGY_NO_JOB_MIN", 50))
        eng_max = float(os.environ.get("PSACCOUNT_SIM_ENERGY_NO_JOB_MAX", 100))

    power = round(random.uniform(eng_min, eng_max))  # nosec B311

    # Calculate passed time
    current_time = time.time()
    delta_t = current_time - last_time

    # Accumulate energy based on power
    accu_energy = round(accu_energy + power * delta_t)

    # Print in psaccount energy format
    print(f"power:{power} energy:{accu_energy}")

    # Save the new state
    new_state = {
        "last_time": current_time,
        "accu_energy": accu_energy,
    }
    with open(state_file, "w", encoding="utf-8") as f:
        json.dump(new_state, f)


if __name__ == "__main__":
    main()
