#!/usr/bin/env python

import re
import sys

if (len(sys.argv) < 2):
    sys.stderr.write("Usage: ./test.py in.pattern < file.in\n")
    sys.exit(1)

# the following is not correct. we have to trim off the tailing '\n'

r = re.compile(sys.argv[1])
for line in sys.stdin:
    if r.search(line):
        sys.stdout.write(line)
