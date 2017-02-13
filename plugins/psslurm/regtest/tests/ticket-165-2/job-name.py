
import sys
import os

d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())])

if "Name" in d.keys():
	print(d["Name"])
else:
	print(d["JobName"])

