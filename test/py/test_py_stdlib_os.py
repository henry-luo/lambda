# test_py_stdlib_os.py — Phase C: os module

import os

# os.getcwd returns a non-empty string
cwd = os.getcwd()
print(len(cwd) > 0)

# os.path.join
print(os.path.join("a", "b", "c"))
print(os.path.join("/root", "sub", "file.txt"))

# os.path.basename / dirname
print(os.path.basename("/tmp/foo.txt"))
print(os.path.dirname("/tmp/foo.txt"))

# os.path.splitext
root, ext = os.path.splitext("hello.world.txt")
print(root)
print(ext)

# os.sep
print(os.sep)

# os.name
print(os.name)
