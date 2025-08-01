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
Change and query graphics and memory frequencies of Nvidia GPUs.
"""

import csv
import io
import re
import sys
import subprocess
import os
from argparse import ArgumentParser


def exec_nvidia_smi(args):
    """
    execute nvidia-smi and return output
    """

    smi = os.environ.get('SIMULATE_SMI_BIN', 'nvidia-smi')

    try:
        result = subprocess.check_output([smi] + args,
                                         universal_newlines=True)
        return result.strip()
    except subprocess.CalledProcessError as e:
        print(f"error nvidia-smi: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print("error: nvidia-smi not found (ensure NVIDIA drivers are "
              "installed)")
        sys.exit(1)


def reset_freq(gpus, freq_type):
    """
    reset graphics and memory clocks to defaults
    """
    if freq_type == "graphics":
        cmd = '--reset-gpu-clocks'
    else:
        cmd = '--reset-memory-clocks'

    output = exec_nvidia_smi([cmd, '--id', str(','.join(gpus))])
    print(output)


def set_freq(gpus, args, freq_type):
    """
    lock graphics or memory clocks
    """

    # Enable persistence mode
    output = exec_nvidia_smi(['-pm', '1', '--id', str(','.join(gpus))])

    if output is None:
        print("enabling persistence mode failed")
        sys.exit(1)

    if freq_type == "graphics":
        cmd = '--lock-gpu-clocks'
    else:
        cmd = '--lock-memory-clocks'

    output = exec_nvidia_smi([cmd, f'{args[0]},{args[0]}', '--id',
                              str(','.join(gpus))])

    if output is not None:
        print(output)


def uuid_to_index(all_gpus, uuid):
    """
    convert gpu uuid to gpu index
    """
    for i in all_gpus:
        if all_gpus[i] == uuid:
            return i
    return None

def get_avail_freq(all_gpus, gpus):
    """
    query supported graphics and memory clocks
    """
    output = exec_nvidia_smi(['--query-supported-clocks', 'gpu_uuid,gr,mem',
                              '--format', 'csv', '--id', str(','.join(gpus))])
    if output is None:
        sys.exit(1)

    reader = csv.DictReader(io.StringIO(output))
    csv_data = list(reader)

    for d in csv_data:
        gpu_id = uuid_to_index(all_gpus, d['gpu_uuid'])
        if gpu_id is None:
            print(f"unable to convert uuid {d['gpu_uuid']}")
            sys.exit(1)

        new_dict = {'index': gpu_id}
        for key, value in d.items():
            if key != 'gpu_uuid':
                clean_key = key.replace('[MHz]', '')
                clean_value = value.replace('MHz', '')
                new_dict[clean_key.strip()] = clean_value.strip()
        print(f"gpu {new_dict['index']} graphics {new_dict['graphics']} "\
              f"memory {new_dict['memory']}")


def get_gpu_freq(gpus):
    """
    query current graphics and memory clocks
    """
    output = exec_nvidia_smi([
        '--id', str(','.join(gpus)),
        '--query-gpu',
        'index,clocks.current.graphics,clocks.current.memory',
        '--format', 'csv'])

    if not output:
        sys.exit(1)

    reader = csv.DictReader(io.StringIO(output))
    csv_data = list(reader)

    for d in csv_data:
        new_dict = {'index': d['index']}
        for key, value in d.items():
            if key != 'index':
                clean_key = key.replace('[MHz]', '').replace('clocks.current.', '')
                clean_value = value.replace('MHz', '')
                new_dict[clean_key.strip()] = clean_value.strip()
        print(f"gpu {new_dict['index']} graphics {new_dict['graphics']} "\
              f"memory {new_dict['memory']}")


def get_gpus(gpu_ids, args):
    """
    match detected GPUs with user selected range
    """
    if args.gpus is None:
        return gpu_ids

    if args.gpus[0].startswith('0x') or args.gpus[0].startswith('0X'):
        try:
            gpu_mask = int(args.gpus[0], 16)
            gpus = []
            for i in range(len(gpu_ids)):
                if gpu_mask & (1 << i):
                    gpus.append(str(i))
        except ValueError:
            print(f"Invalid hex GPU specification: {args.gpus[0]}")
            sys.exit(1)

    elif len(args.gpus) == 1 and "," in args.gpus[0]:
        gpus = [item for item in args.gpus[0].split(",") if item]
    else:
        gpus = args.gpus

    for i in gpus:
        if i not in gpu_ids:
            print(f"gpu {i} not in range of valid GPUs {0}-{len(gpu_ids) - 1}")
            sys.exit(1)
    return gpus


def get_all_gpus():
    """
    query and parse all available GPUs
    """
    gpu_info = exec_nvidia_smi(['--list-gpus'])

    if not gpu_info:
        return None

    all_gpus = {}

    pattern = re.compile(r'GPU\s+(\d+):\s+(.+?)\s+\(UUID:\s+(.+?)\)')
    for line in gpu_info.splitlines():
        match = pattern.match(line)
        if match:
            index = int(match.group(1))
            all_gpus[index] = match.group(3)

    return all_gpus


def parse_args(parser):
    """
    parse and handle command line arguments
    """
    args = parser.parse_args()

    # get list of available gpus
    all_gpus = get_all_gpus()
    gpu_ids = list(map(str, all_gpus.keys()))

    if not all_gpus:
        print("error: no supported GPUs found")
        sys.exit(1)

    if args.list_gpus is not None:
        print(str(len(gpu_ids)) + ": " + ",".join(gpu_ids))
        return

    if args.gpu_info is not None:
        # always shows all available gpus
        print(exec_nvidia_smi(['--list-gpus']))
        return

    # get range of GPUs to work on
    gpus = get_gpus(gpu_ids, args)

    if args.get_freq is not None:
        get_gpu_freq(gpus)
    elif args.get_avail_freq is not None:
        get_avail_freq(all_gpus, gpus)
    elif args.set_gra_freq is not None:
        set_freq(gpus, args.set_gra_freq, "graphics")
    elif args.set_mem_freq is not None:
        set_freq(gpus, args.set_mem_freq, "memory")
    elif args.reset_gra_freq is not None:
        reset_freq(gpus, "graphics")
    elif args.reset_mem_freq is not None:
        reset_freq(gpus, "memory")
    else:
        parser.print_help()


def main():
    """
    main function of GPU frequency script
    """

    parser = ArgumentParser(
        description="""Change and query GPU frequency"""
    )
    parser.add_argument(
        "--gpus", metavar="<GPUs>", nargs="+", help="list of GPUs to use"
    )
    parser.add_argument(
        "--list-gpus", metavar="", nargs="*", help="short list of avaiable GPUs")
    parser.add_argument(
        "--gpu-info", metavar="", nargs="*", help="show GPU info of all GPUs")
    parser.add_argument(
        "--get-freq", nargs="*", help="get current GPU frequencies")
    parser.add_argument(
        "--set-gra-freq", metavar="<graphics>", nargs=1,
        help="set graphics frequency")
    parser.add_argument(
        "--set-mem-freq", metavar="<memory>", nargs=1,
        help="set memory frequency")
    parser.add_argument(
        "--get-avail-freq", nargs="*", help="get available GPU frequencies")
    parser.add_argument(
        "--reset-gra-freq", nargs="*", help="reset graphics frequencies")
    parser.add_argument(
        "--reset-mem-freq", nargs="*", help="reset memory frequencies")

    parse_args(parser)


if __name__ == "__main__":
    main()
