#!/usr/bin/env python3
#
#               ParaStation
#
# Copyright (C) 2025 ParTec AG, Munich
"""
query and modify various CPU frequencies by using /sys file-system
"""

import os
import re
import sys
from argparse import ArgumentParser

def read_string_from_file(filename):
    """
    Read a utf-8 encoded string from a file and trim newline character
    """
    try:
        with open(filename, encoding="utf-8") as sys_file:
            return sys_file.read().replace("\n", "")
    except FileNotFoundError:
        print(f"error: file '{filename}' not found")
        sys.exit(1)
    except PermissionError:
        print(f"error: no permission to read '{filename}'")
        sys.exit(1)
    except UnicodeDecodeError:
        print(f"error: file '{filename}' could not be decoded with UTF-8")
        sys.exit(1)
    except IOError:
        print(f"error: an I/O error occurred while reading '{filename}'")
        sys.exit(1)


def write_string_to_file(filename, data):
    """
    Write a utf-8 encoded string to file
    """
    try:
        with open(filename, "w", encoding="utf-8") as sys_file:
            return sys_file.write(data)
    except FileNotFoundError:
        print(f"error: file '{filename}' not found")
        sys.exit(1)
    except PermissionError:
        print(f"error: no permission to write '{filename}'")
        sys.exit(1)
    except UnicodeDecodeError:
        print(f"error: data could not be encoded in UTF-8 for '{filename}'")
        sys.exit(1)
    except IOError:
        print(f"error: an I/O error occurred while writing '{filename}'")
        sys.exit(1)
    except OSError:
        print(f"error: unable to write {data} to '{filename}' invalid argument")
        sys.exit(1)


def get_cpu_freq(cpu_sys_path, index):
    """
    Read various CPU frequencies from given /sys path
    """
    for i in index:
        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/cpuinfo_min_freq"
        avail_min_freq = read_string_from_file(filename)

        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/cpuinfo_max_freq"
        avail_max_freq = read_string_from_file(filename)

        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_min_freq"
        cur_min_freq = read_string_from_file(filename)

        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_max_freq"
        cur_max_freq = read_string_from_file(filename)

        print(
            f"cpu {i} avail_min_freq {avail_min_freq} avail_max_freq "
            f"{avail_max_freq} cur_min_freq {cur_min_freq} cur_max_freq "
            f"{cur_max_freq}"
        )

def get_avail_cpu_freq(cpu_sys_path, index):
    """
    Query available CPU frequencies. Depending on the hardware this optional
    information might not be available.
    """
    for i in index:
        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_available_frequencies"
        avail_freq = read_string_from_file(filename)

        print(f" cpu {i} avail_freq {avail_freq}")


def get_gov(cpu_sys_path, index, cur_gov, avail_gov):
    """
    query current and available governors for selected CPUs
    """
    for i in index:
        if cur_gov is not None:
            filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_governor"
            cur_gov = read_string_from_file(filename)
            print(f" cpu {i} cur_gov {cur_gov}")

        if avail_gov is not None:
            filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_available_governors"
            avail_gov = read_string_from_file(filename)
            print(f" cpu {i} avail_gov {avail_gov}")


def set_gov(cpu_sys_path, index, new_gov):
    """
    set governor for selected CPUs
    """
    for i in index:
        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/scaling_governor"
        write_string_to_file(filename, new_gov[0])


def set_freq(cpu_sys_path, index, min_freq, max_freq):
    """
    set minimum and maximum scaling frequencies for selected CPUs
    """
    if min_freq is not None:
        freq = min_freq
        name = "scaling_min_freq"
    else:
        freq = max_freq
        name = "scaling_max_freq"

    for i in index:
        filename = f"{cpu_sys_path}/cpu{i}/cpufreq/{name}"
        write_string_to_file(filename, freq[0])


def get_all_cpus(cpu_sys_path):
    """
    query and parse all available CPUs
    """
    cpus = []
    reg_ex = re.compile(r"cpu(\d+)")
    for nfile in os.scandir(cpu_sys_path):
        if nfile.is_dir():
            match = reg_ex.match(nfile.name)
            if match is not None:
                cpus.append(match.group(1))

    cpus.sort()
    return cpus


def get_cpus(all_cpus, args):
    """
    match detected CPUs with user selected range
    """
    if args.cpus is None:
        return all_cpus

    if args.cpus[0].startswith('0x') or args.cpus[0].startswith('0X'):
        try:
            cpu_mask = int(args.cpus[0], 16)
            cpus = []
            for i in range(len(all_cpus)):
                if cpu_mask & (1 << i):
                    cpus.append(str(i))
        except ValueError:
            print(f"Invalid hex CPU specification: {args.cpus[0]}")
            sys.exit(1)

    elif len(args.cpus) == 1 and "," in args.cpus[0]:
        cpus = [item for item in args.cpus[0].split(",") if item]
    else:
        cpus = args.cpus

    for i in cpus:
        if i not in all_cpus:
            print(f"cpu {i} not in range of valid CPUs {0}-{len(all_cpus) - 1}")
            sys.exit(1)

    return cpus


def parse_args(parser):
    """
    parse and handle command line arguments
    """
    args = parser.parse_args()

    if args.cpu_sys_path is not None:
        cpu_sys_path = args.cpu_sys_path
    else:
        cpu_sys_path = "/sys/devices/system/cpu"

    # get available cpus
    all_cpus = get_all_cpus(cpu_sys_path)

    if not all_cpus:
        print(f"error: no CPUs found in {cpu_sys_path}")
        sys.exit(1)

    if args.list_cpus is not None:
        print(str(len(all_cpus)) + ": " + ",".join(all_cpus))
        return

    # get all CPUs
    cpus = get_cpus(all_cpus, args)

    if args.get_cur_gov is not None:
        get_gov(cpu_sys_path, cpus, args.get_cur_gov, None)
    elif args.get_avail_gov is not None:
        get_gov(cpu_sys_path, cpus, None, args.get_avail_gov)
    elif args.get_freq is not None:
        get_cpu_freq(cpu_sys_path, cpus)
    elif args.get_avail_freq is not None:
        get_avail_cpu_freq(cpu_sys_path, cpus)
    elif args.set_gov is not None:
        set_gov(cpu_sys_path, cpus, args.set_gov)
    elif args.set_min_freq is not None:
        set_freq(cpu_sys_path, cpus, args.set_min_freq, None)
    elif args.set_max_freq is not None:
        set_freq(cpu_sys_path, cpus, None, args.set_max_freq)
    else:
        parser.print_help()


def main():
    """
    main function of CPU frequency script
    """
    parser = ArgumentParser(
        description="""Change and query CPU frequency and
                                        CPU governor"""
    )
    parser.add_argument(
        "--cpus", metavar="<CPUs>", nargs="+", help="list of CPUs to use"
    )
    parser.add_argument(
        "--list-cpus", metavar="", nargs="*", help="show avaiable CPUs")
    parser.add_argument(
        "--set-gov", metavar="<governor>", nargs="+", help="set new governor")
    parser.add_argument("--get-cur-gov", nargs="*", help="get current governor")
    parser.add_argument(
        "--get-avail-gov", nargs="*", help="get available governors")
    parser.add_argument(
        "--get-freq", nargs="*", help="get min/max CPU frequencies")
    parser.add_argument(
        "--set-min-freq", metavar="<frequency>", nargs="+",
        help="set minimum frequency")
    parser.add_argument(
        "--set-max-freq", metavar="<frequency>", nargs="+",
        help="set maximum frequency")
    parser.add_argument(
        "--get-avail-freq", nargs="*", help="get available CPU frequencies")
    parser.add_argument(
        "--cpu-sys-path", type=str, help="CPU frequency path in /sys filesystem")

    parse_args(parser)


if __name__ == "__main__":
    main()
