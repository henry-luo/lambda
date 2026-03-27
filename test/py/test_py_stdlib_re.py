# test_py_stdlib_re.py — Phase C: re module

import re

# re.search
m = re.search("(\\d+)-(\\w+)", "abc-42-hello-xyz")
print(m.group(0))
print(m.group(1))
print(m.group(2))

# re.findall without groups
result = re.findall("\\d+", "a1b22c333")
print(result)

# re.sub
print(re.sub("\\d+", "X", "a1b2c3"))

# re.split
print(re.split("[,;]", "a,b;c,d"))
