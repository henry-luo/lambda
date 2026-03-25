# F3: Dict methods
d = {"a": 1, "b": 2, "c": 3}
d2 = d.copy()
print(d2)
print(d.setdefault("b", 99))
print(d.setdefault("d", 42))
print(d)
