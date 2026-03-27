# Test generators - Phase A

# === 1. Basic counter generator ===
def counter(start, stop):
    i = start
    while i < stop:
        yield i
        i += 1

gen = counter(0, 5)
print(next(gen))   # 0
print(next(gen))   # 1
print(next(gen))   # 2

# === 2. list() from generator ===
print(list(counter(3, 7)))   # [3, 4, 5, 6]

# === 3. for loop over generator ===
total = 0
for v in counter(1, 6):
    total += v
print(total)   # 15

# === 4. Generator with return ===
def until_zero(lst):
    for x in lst:
        if x == 0:
            return
        yield x

print(list(until_zero([5, 3, 1, 0, 9])))   # [5, 3, 1]

# === 5. send() / accumulator ===
def accumulator():
    total = 0
    while True:
        value = yield total
        if value is None:
            return
        total += value

acc = accumulator()
next(acc)         # prime: state=1, returns 0 (discarded)
print(acc.send(10))  # total=10 → yields 10
print(acc.send(20))  # total=30 → yields 30
print(acc.send(5))   # total=35 → yields 35

# === 6. yield from ===
def chain(a, b):
    yield from a
    yield from b

print(list(chain([1, 2], [3, 4])))   # [1, 2, 3, 4]

# === 7. Nested generator use ===
def squares(n):
    i = 0
    while i < n:
        yield i * i
        i += 1

print(list(squares(5)))   # [0, 1, 4, 9, 16]

# === 8. Generator exhaustion: for-loop handles StopIteration internally ===
exhausted = []
for v in counter(0, 2):
    exhausted.append(v)
print(exhausted)  # [0, 1]
