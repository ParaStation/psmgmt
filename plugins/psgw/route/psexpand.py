#!/usr/bin/env python
#
# expand.py - fold node lists using range syntax
#
# Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
#

import sys
import re
from itertools import product

try:
    from itertools import zip_longest
except ImportError:  # Python 2
    from itertools import izip_longest as zip_longest


RANGE_RE = re.compile(r"^(?P<from>[0-9]+)-(?P<to>[0-9]+)$")


def expand_range(s):
    m = re.match(RANGE_RE, s)
    if m:  # valid range, unroll
        for h in range(int(m.group("from")), int(m.group("to")) + 1):
            width = len(m.group("from"))
            yield ("%%0%dd" % width) % h
    elif s:  # not a valid range, echo it if not empty
        yield s


def expand_rangeset(literal, rangeset):
    if rangeset is None:  # no rangeset
        yield literal
    elif rangeset:  # non-empty rangeset
        for range in rangeset.split(","):
            for element in expand_range(range):
                yield "%s%s" % (literal, element)


def _grouper(iterable, n, fillvalue=None):
    # itertools recipe: return tuples of length n
    args = [iter(iterable)] * n
    return zip_longest(*args, fillvalue=fillvalue)


# splitting on GROUP_RE yields an alternating stream of literals and
# rangesets
GROUP_RE = re.compile(r"\[([^]]*)\]")


def expand_group(s):
    for p in product(
        *(
            expand_rangeset(literal, rangeset)
            for literal, rangeset in _grouper(re.split(GROUP_RE, s), 2)
        )
    ):
        yield "".join(p)


# a group contains either a complete [...] range, or anything but a comma
LIST_RE = re.compile(r"^(?P<elem>(?:\[[^]]*\]|[^,])*)")


def expand_list(s):
    m = re.match(LIST_RE, s)
    while m:
        group = m.group("elem")
        for result in expand_group(group):
            yield result
        s = s[m.end() :]
        if not s:
            # nothing left: need to break, to avoid an endless loop,
            # as the regexp happily matches an empty string
            break
        # there should be a ',' at the front, remove it
        assert s[0] == ","
        s = s[1:]
        m = re.match(LIST_RE, s)


def main():
    if len(sys.argv) > 1:
        for result in expand_list(sys.argv[1]):
            print(result)


if __name__ == "__main__":
    main()
