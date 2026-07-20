# Native-int fast paths must preserve Python's arbitrary-precision result when
# their int56 representation overflows.
print((2 ** 54) * 4)
print(-(2 ** 55) - 1)
print((2 ** 55) + 1)
