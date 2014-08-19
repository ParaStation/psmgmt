#!/usr/bin/env python

import string

from random import randint


# Number of lines
N = 72

c = string.ascii_letters + string.digits + ',.-\t$@!~<>?/:;\\'
f = ""

for i in range(N):
	n = randint(0, 79)

	for j in range(n):
		f += c[randint(0, len(c)-1)]
	f += "\n"

open("input.txt", "w").write(f)


