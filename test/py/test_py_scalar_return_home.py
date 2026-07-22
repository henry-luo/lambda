# Wide scalar callback and generator results must survive outside their frame.
tiny = 5e-324

def key(value):
    return tiny

def values():
    yield tiny

print(map(key, [1, 2]))
print(sorted([2, 1], key=key))
print(next(values()))
