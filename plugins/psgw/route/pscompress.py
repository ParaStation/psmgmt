#!/usr/bin/env python
#
# pscompress.py - fold node lists using range syntax
#
# Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
#

import sys
import re


def fmt_range(range):
    if int(range[0]) == int(range[1]):
        return range[0]
    if int(range[0]) + 1 == int(range[1]):
        return "%s,%s" % (range[0], ("%%0%dd" % len(range[0])) % int(range[1]))
    return "%s-%s" % range


def fmt_ranges(ranges):
    if len(ranges) == 1 and int(ranges[0][0]) == int(ranges[0][1]):
        return ranges[0][0]
    return "[%s]" % ",".join(fmt_range(range) for range in ranges)


def compress(s):

    # preprocess: a1b -> a[1-1]b etc.
    list_in = [
        [
            [(part, part)] if i % 2 else part
            for i, part in enumerate(re.split(r"(\d+)", element))
        ]
        for element in s
    ]

    while True:
        result = []
        for element in list_in:

            # start with first element
            if not result:
                result.append(element)
                continue

            # compare
            diffs = [i for i, (a, b) in enumerate(zip(element, result[-1])) if a != b]
            if len(diffs) == 1 and diffs[0] % 2:
                # exactly one range differs
                i = diffs[0]
                cur, test = result[-1][i], element[i]
                cur_from, cur_to = cur[-1][0], cur[-1][1]
                test_from, test_to = test[0][0], test[0][1]
                if ("%%0%dd" % len(cur_from)) % (int(cur_to) + 1) == test_from:
                    # ranges fit together, merge
                    cur[-1:] = [(cur_from, test_to)] + test[1:]
                else:
                    # cannot merge ranges, concat
                    cur.extend(test)
                continue

            # flush
            result.append(element)

        # loop runs until nothing more can be compressed
        if result == list_in:
            break

        # next round
        list_in = result

    # postprocess
    return ",".join(
        "".join(fmt_ranges(part) if i % 2 else part for i, part in enumerate(element))
        for element in result
    )


def main():
    if len(sys.argv) > 1:
        print(compress(sys.argv[1:]))


if __name__ == "__main__":
    main()
