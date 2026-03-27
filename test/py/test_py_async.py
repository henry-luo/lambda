# Test async/await coroutines - Phase D

import asyncio

# === 1. Basic async def + await + return ===
async def greet(name):
    await asyncio.sleep(0)
    return "hello " + name

async def main1():
    result = await greet("world")
    print(result)

asyncio.run(main1())

# === 2. asyncio.run returns coroutine return value ===
async def simple():
    return 42

print(asyncio.run(simple()))

# === 3. Multiple sequential awaits ===
async def step(n):
    await asyncio.sleep(0)
    return n + 1

async def pipeline():
    a = await step(1)
    b = await step(a)
    c = await step(b)
    print(c)

asyncio.run(pipeline())

# === 4. asyncio.gather — parallel coroutines ===
async def double(n):
    await asyncio.sleep(0)
    return n * 2

async def run_gather():
    results = await asyncio.gather(double(1), double(2), double(3))
    print(results)

asyncio.run(run_gather())

# === 5. async def with no await (implicit return None) ===
async def noop():
    pass

result = asyncio.run(noop())
print(result)

# === 6. Nested coroutines ===
async def inner(x):
    await asyncio.sleep(0)
    return x * 10

async def outer(x):
    val = await inner(x)
    return val + 1

async def main6():
    r = await outer(5)
    print(r)

asyncio.run(main6())
