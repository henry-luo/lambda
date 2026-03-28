# test_py_stdlib_json.py — Phase C: json module

import json

# json.loads
d = json.loads('{"a": 1, "b": [2, 3]}')
print(d["a"])
print(d["b"])

# json.dumps
s = json.dumps({"x": 42})
print(s)

# roundtrip
data = [1, 2, 3]
print(json.loads(json.dumps(data)))
