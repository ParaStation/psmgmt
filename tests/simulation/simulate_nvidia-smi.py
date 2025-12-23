#!/usr/bin/env python3
# pylint: disable=invalid-name
#
# ParaStation
#
# Copyright (C) 2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

"""
This script simulates certain options of nvidia-smi. It may be useful in
a virtual machine without any real Nvidia GPUs attached.
"""

import sys
from datetime import datetime
import json
import os
import argparse

STATE_FILE = "/dev/shm/simulate_nvidia-smi.state"

A100 = [ # NVIDIA A100 mock configuration
    {
        "index": 0,
        "name": "NVIDIA A100-SXM4-40GB",
        "uuid": "GPU-12345678-1234-5678-9abc-def012345678",
        "bus_id": "00000000:03:00.0",
        "temp": 43,
        "perf": "P0",
        "power": 57,
        "power_cap": 400,
        "mem_used": 0,
        "mem_total": 40960,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 210,
        "current_mem": 1000,
        "default_gr": 210,
        "default_mem": 1000,
        "supported_clocks" : [
            (1410, 1935),
            (1210, 1935),
            (1260, 1710),
            (1160, 1710),
            (1063, 1444),
            (955, 1444),
            (955, 1297),
            (800, 1297),
            (210, 1000)
        ]
    },
    {
        "index": 1,
        "name": "NVIDIA A100-SXM4-40GB",
        "uuid": "GPU-87654321-4321-8765-cba9-876543210fed",
        "bus_id": "00000000:44:00.0",
        "temp": 44,
        "perf": "P0",
        "power": 57,
        "power_cap": 400,
        "mem_total": 40960,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 210,
        "current_mem": 1000,
        "default_gr": 210,
        "default_mem": 1000,
        "supported_clocks" : [
            (1410, 1935),
            (1210, 1935),
            (1260, 1710),
            (1160, 1710),
            (1063, 1444),
            (955, 1444),
            (955, 1297),
            (800, 1297),
            (210, 1000)
        ]
    },
    {
        "index": 2,
        "name": "NVIDIA A100-SXM4-40GB",
        "uuid": "GPU-abcdef12-3456-7890-fedc-ba0987654321",
        "bus_id": "00000000:84:00.0",
        "temp": 44,
        "perf": "P0",
        "power": 60,
        "power_cap": 400,
        "mem_total": 40960,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 210,
        "current_mem": 1000,
        "default_gr": 210,
        "default_mem": 1000,
        "supported_clocks" : [
            (1410, 1935),
            (1210, 1935),
            (1260, 1710),
            (1160, 1710),
            (1063, 1444),
            (955, 1444),
            (955, 1297),
            (800, 1297),
            (210, 1000)
        ]
    },
    {
        "index": 3,
        "name": "NVIDIA A100-SXM4-40GB",
        "uuid": "GPU-11223344-5566-7788-9900-aabbccddeeff",
        "bus_id": "00000000:C4:00.0",
        "temp": 45,
        "perf": "P0",
        "power": 62,
        "power_cap": 400,
        "mem_total": 40960,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 210,
        "current_mem": 1000,
        "default_gr": 210,
        "default_mem": 1000,
        "supported_clocks" : [
            (1410, 1935),
            (1210, 1935),
            (1260, 1710),
            (1160, 1710),
            (1063, 1444),
            (955, 1444),
            (955, 1297),
            (800, 1297),
            (210, 1000)
        ]
    }
]

V100 = [ # NVIDIA Tesla V100 mock configuration
    {
        "index": 4,
        "name": "Tesla V100-SXM2-16GB ",
        "uuid": "GPU-cc7c0e9c-238d-8c2b-417d-af79efa57335",
        "bus_id": "00000000:60:00.0",
        "temp": 43,
        "perf": "P0",
        "power": 45,
        "power_cap": 300,
        "mem_used": 0,
        "mem_total": 16384,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 135,
        "current_mem": 877,
        "default_gr": 135,
        "default_mem": 877,
        "supported_clocks" : [
            (1530, 877),
            (1522, 877),
            (1507, 877),
            (1470, 877),
            (1462, 877),
            (937, 300),
            (930, 300),
            (202, 877),
            (195, 877),
            (180, 877),
            (142, 1024),
            (135, 1024)
        ]
    },
    {
        "index": 5,
        "name": "Tesla V100-SXM2-16GB ",
        "uuid": "GPU-7fe7259d-d4e1-723c-868b-f2fd0c20229d",
        "bus_id": "00000000:61:00.0",
        "temp": 44,
        "perf": "P0",
        "power": 47,
        "power_cap": 300,
        "mem_total": 16384,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 135,
        "current_mem": 877,
        "default_gr": 135,
        "default_mem": 877,
        "supported_clocks" : [
            (1530, 877),
            (1522, 877),
            (1507, 877),
            (1470, 877),
            (1462, 877),
            (937, 300),
            (930, 300),
            (202, 877),
            (195, 877),
            (180, 877),
            (142, 1024),
            (135, 1024)
        ]
    },
    {
        "index": 6,
        "name": "Tesla V100-SXM2-16GB ",
        "uuid": "GPU-c57e8e0d-cdc9-deff-1e0f-595856de90c8",
        "bus_id": "00000000:88:00.0",
        "temp": 44,
        "perf": "P0",
        "power": 50,
        "power_cap": 300,
        "mem_total": 16384,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 135,
        "current_mem": 877,
        "default_gr": 135,
        "default_mem": 877,
        "supported_clocks" : [
            (1530, 877),
            (1522, 877),
            (1507, 877),
            (1470, 877),
            (1462, 877),
            (937, 300),
            (930, 300),
            (202, 877),
            (195, 877),
            (180, 877),
            (142, 1024),
            (135, 1024)
        ]
    },
    {
        "index": 7,
        "name": "Tesla V100-SXM2-16GB ",
        "uuid": "GPU-56ce31f1-16ce-b601-dcfb-2e5a7aa8817e",
        "bus_id": "00000000:89:00.0",
        "temp": 45,
        "perf": "P0",
        "power": 50,
        "power_cap": 300,
        "mem_total": 16384,
        "util": 0,
        "compute": "Default",
        "mig": "Disabled",
        "current_gr": 135,
        "current_mem": 877,
        "default_gr": 135,
        "default_mem": 877,
        "supported_clocks" : [
            (1530, 877),
            (1522, 877),
            (1507, 877),
            (1470, 877),
            (1462, 877),
            (937, 300),
            (930, 300),
            (202, 877),
            (195, 877),
            (180, 877),
            (142, 1024),
            (135, 1024)
        ]
    }
]

# GPU configuration
GPUS = []
GPUS.extend(A100)
GPUS.extend(V100)

DEF_HEADER = (
"+-----------------------------------------------------------------------------"
    "------------+\n"
"| NVIDIA-SMI 580.95.05              Driver Version: 580.95.05      CUDA "
"Version: 13.0     |\n"

"+-----------------------------------------+------------------------+----------"
"------------+\n"
"| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile"
" Uncorr. ECC |\n"
"| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util"
"  Compute M. |\n"
"|                                         |                        |         "
"      MIG M. |\n"
"|=========================================+========================+========="
"=============|")

DEF_FOOTER = (
"+------------------------------------------------------------------------------"
"-----------+\n"
"| Processes:                                                               "
"               |\n"
"|  GPU   GI   CI              PID   Type   Process name                    "
"    GPU Memory |\n"
"|        ID   ID                                                           "
"    Usage      |\n"
"|=========================================================================="
"===============|\n"
"|  No running processes found                                              "
"               |\n"
"+--------------------------------------------------------------------------"
"---------------+")

def load_state():
    """
    Load changed frequecies
    """
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE, 'r', encoding='utf-8') as f:
                state = json.load(f)
            for (idx_str), clocks in state.items():
                idx = int(idx_str)
                if idx < len(GPUS):
                    GPUS[idx]['current_gr'] = clocks['current_gr']
                    GPUS[idx]['current_mem'] = clocks['current_mem']
        except PermissionError:
            print(f"error: no permission to read '{STATE_FILE}'")
            sys.exit(1)
        except UnicodeDecodeError:
            print(f"error: file '{STATE_FILE}' could not be decoded with UTF-8")
            sys.exit(1)
        except IOError:
            print(f"error: an I/O error occurred while reading '{STATE_FILE}'")
            sys.exit(1)


def save_state():
    """
    Save changed frequecies
    """
    state = {}
    for gpu in GPUS:
        idx = str(gpu['index'])
        state[idx] = {'current_gr': gpu['current_gr'],
                      'current_mem': gpu['current_mem']}
    try:
        with open(STATE_FILE, 'w', encoding='utf-8') as f:
            json.dump(state, f)
    except PermissionError:
        print(f"error: no permission to write '{STATE_FILE}'")
        sys.exit(1)
    except UnicodeDecodeError:
        print(f"error: data could not be encoded in UTF-8 for '{STATE_FILE}'")
        sys.exit(1)
    except IOError:
        print(f"error: an I/O error occurred while writing '{STATE_FILE}'")
        sys.exit(1)


def print_gpu_info():
    """
    Print generic gpu info for all GPUs
    """
    for i, gpu in enumerate(GPUS):
        if i != 0:
            print('+-----------------------------------------+--------'
                  '----------------+----------------------+')
        print(f"|   {gpu['index']}  {gpu['name']}          On  |   {gpu['bus_id']} Off "
              f"|                   {gpu['util']}  |")
        print(f"| N/A   {gpu['temp']}C    {gpu['perf']}             {gpu['power']}W /"
              f"{gpu['power_cap']}W   |      0MiB /  {gpu['mem_total']}MiB  "
              f"|     0%      {gpu['compute']}  |")
        print(f"|                                         |              "
              f"|{gpu['mig']} |                      |")

    print("+-----------------------------------------+--------------------"
          "----+----------------------+")


def handle_default():
    """
    Default output without arguments
    """
    dt = datetime.now()
    print(dt.strftime("%a %b %d %H:%M:%S %Y"))
    print(DEF_HEADER)
    print_gpu_info()
    print(DEF_FOOTER)


def handle_list_gpus():
    """
    List all available GPUs
    """
    for gpu in GPUS:
        print(f"GPU {gpu['index']}: {gpu['name']} (UUID: {gpu['uuid']})")


def handle_query_supported_clocks(ids, fields_str, format_type):
    """
    Query all supported graphics and memory clocks of selected GPUs
    """
    if format_type is not None and format_type != 'csv':
        print(f"Unsupported format {format_type}")
        sys.exit(1)

    fields = [f.strip() for f in fields_str.split(',')]
    for field in fields:
        if field not in ['gpu_uuid', 'gr', 'mem']:
            print(f"Unsupported field {field}")
            sys.exit(1)

    print("gpu_uuid, graphics [MHz], memory [MHz]")
    for i in ids:
        if i >= len(GPUS):
            continue
        gpu = GPUS[i]
        for gr, mem in gpu['supported_clocks']:
            print(f"{gpu['uuid']}, {gr} MHz, {mem} MHz")


def handle_query_gpu(ids, fields_str, format_type):
    """
    Query graphics and memory application frequecies
    """
    if format_type is not None and format_type != 'csv':
        print(f"Unsupported format {format_type}")
        sys.exit(1)

    supported_fields =  ['index', 'gpu_uuid', 'clocks.current.graphics',
                         'clocks.current.memory']

    fields = [f.strip() for f in fields_str.split(',')]
    header_parts = []
    for field in fields:
        if field not in supported_fields:
            print(f"Unsupported field {field}")
            sys.exit(1)

        if 'clock' in field:
            header_parts.append(f"{field} [MHz]")
        else:
            header_parts.append(field)
    print(', '.join(header_parts))

    for i in ids:
        if i >= len(GPUS):
            continue
        gpu = GPUS[i]
        values = []
        for field in fields:
            if field == 'index':
                values.append(str(gpu['index']))
            elif field == 'gpu_uuid':
                values.append(str(gpu['uuid']))
            elif field in ('clocks.current.graphics'):
                values.append(f"{gpu['current_gr']} MHz")
            elif field in ('clocks.current.memory'):
                values.append(f"{gpu['current_mem']} MHz")
        print(', '.join(values))


def handle_pm(ids):
    """
    Enable persistent mode
    """
    for i in ids:
        if i >= len(GPUS):
            continue
        gpu = GPUS[i]
        print(f"Enabled persistence mode for GPU {gpu['bus_id']}")


def handle_lock_gpu_clocks(ids, param):
    """
    Lock graphics clock to given frequecy
    """
    try:
        vals = [int(v.strip()) for v in param.split(',')]
        if len(vals) == 1:
            min_gr = max_gr = vals[0]
        elif len(vals) == 2:
            min_gr, max_gr = vals
            if min_gr > max_gr:
                raise ValueError
        else:
            raise ValueError

        for i in ids:
            if i >= len(GPUS):
                continue
            gpu = GPUS[i]
            sup_gra_clocks = sorted(set(gr for gr, mem in
                                            gpu['supported_clocks']))
            if max_gr not in sup_gra_clocks:
                print(f"Unsupported graphics clock: {max_gr}")
                sys.exit(1)

            gpu['current_gr'] = max_gr
            print(f"GPU clocks locked to {min_gr},{max_gr} "
                  f"for GPU {gpu['bus_id']}")
            save_state()
    except ValueError:
        print(f"Invalid --lock-gpu-clocks format {str(param)}")
        sys.exit(1)


def handle_lock_memory_clocks(ids, param):
    """
    Lock memory clock to given frequecy
    """
    try:
        vals = [int(v.strip()) for v in param.split(',')]
        if len(vals) == 1:
            min_mem = max_mem = vals[0]
        elif len(vals) == 2:
            min_mem, max_mem = vals
            if min_mem > max_mem:
                raise ValueError
        else:
            raise ValueError

        for i in ids:
            if i >= len(GPUS):
                continue
            gpu = GPUS[i]
            sup_mem_clocks = sorted(set(mem for gr, mem in
                                        gpu['supported_clocks']))
            if max_mem not in sup_mem_clocks:
                print(f"Unsupported memory clock: {max_mem}")
                sys.exit(1)

            gpu['current_mem'] = max_mem
            print(f"Memory clocks locked to {min_mem},{max_mem} "
                  f"for GPU {gpu['bus_id']}")
        save_state()
    except ValueError:
        print(f"Invalid --lock-memory-clocks format {str(param)}")
        sys.exit(1)



def handle_reset_gpu_clocks(ids):
    """
    Reset graphics clock to default frequecies
    """
    for i in ids:
        if i >= len(GPUS):
            continue
        gpu = GPUS[i]
        gpu['current_gr'] = gpu['default_gr']
        print(f"Reset GPU clocks for GPU {gpu['bus_id']}")
    save_state()


def handle_reset_memory_clocks(ids):
    """
    Reset memory clock to default frequecies
    """
    for i in ids:
        if i >= len(GPUS):
            continue
        gpu = GPUS[i]
        gpu['current_mem'] = gpu['default_mem']
        print(f"Reset memory clocks for GPU {gpu['bus_id']}")
    save_state()


def handle_args(args): # pylint: disable=too-many-branches
    """
    Handle all command line arguments
    """

    if args.id:
        try:
            ids = [int(g) for g in args.id.split(',')]
        except ValueError:
            ids = list(range(len(GPUS)))
    else:
        ids = list(range(len(GPUS)))

    if args.list_gpus:
        handle_list_gpus()
    elif args.query_supported_clocks:
        handle_query_supported_clocks(ids, args.query_supported_clocks,
                                      args.format)
    elif args.query_gpu:
        handle_query_gpu(ids, args.query_gpu, args.format)
    elif args.pm is not None:
        handle_pm(ids)
    elif args.lock_gpu_clocks is not None:
        handle_lock_gpu_clocks(ids, args.lock_gpu_clocks)
    elif args.lock_memory_clocks is not None:
        handle_lock_memory_clocks(ids, args.lock_memory_clocks)
    elif args.lock_memory_clocks_deferred is not None:
        handle_lock_memory_clocks(ids, args.lock_memory_clocks_deferred)
    elif args.reset_gpu_clocks:
        handle_reset_gpu_clocks(ids)
    elif args.reset_memory_clocks:
        handle_reset_memory_clocks(ids)
    else:
        handle_default()


def main():
    """
    main function of nvidia-smi simulator
    """

    # load saved frequecies
    load_state()

    parser = argparse.ArgumentParser(prog='nvidia-smi',
                                     description='Simulate certain options'
                                                 'of nvidia-smi')
    parser.add_argument('-i', '--id', type=str,
                        help='Comma-separated list of GPU indices or UUIDs')
    parser.add_argument('--format', type=str,
                        help='Output format (supported = csv)')

    group = parser.add_mutually_exclusive_group()
    group.add_argument('--list-gpus', action='store_true',
                       help='List all GPUs')
    group.add_argument('--query-supported-clocks', type=str,
                       help='Query supported clocks')
    group.add_argument('--query-gpu', type=str,
                       help='Query current clocks')
    group.add_argument('-pm', type=int, choices=[1],
                       help='Enable persistence mode (only 1 supported)')
    group.add_argument('--lock-gpu-clocks', type=str,
                       help='Lock graphics clocks to a range or fixed value')
    group.add_argument('--lock-memory-clocks', type=str,
                       help='Lock memory clocks to a range or fixed value')
    group.add_argument('--lock-memory-clocks-deferred', type=str,
                       help='Lock memory clocks to a range or fixed value'
                       ' (deferred)')
    group.add_argument('--reset-gpu-clocks', action='store_true',
                       help='Reset graphics clocks to default')
    group.add_argument('--reset-memory-clocks', action='store_true',
                       help='Reset memory clocks to default')

    args = parser.parse_args()
    handle_args(args)

    return 0


if __name__ == "__main__":
    sys.exit(main())
