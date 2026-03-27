# Test arbitrary-precision integers (bigint) - Phase C

# === 1. Large integer literal (exceeds int64) ===
big = 99999999999999999999999999999
print(big + 1)   # 100000000000000000000000000000

# === 2. Power overflow -> bigint ===
print(2 ** 100)   # 1267650600228229401496703205376

# === 3. Large addition ===
a = 2 ** 64
b = 2 ** 64
print(a + b)   # 36893488147419103232

# === 4. Large multiplication ===
print(a * 2)   # 36893488147419103232

# === 5. Factorial 20 (fits int64) ===
def factorial(n):
    r = 1
    for i in range(2, n + 1):
        r *= i
    return r

print(factorial(20))   # 2432902008176640000

# === 6. Factorial 25 (overflows int64) ===
print(factorial(25))   # 15511210043330985984000000

# === 7. Left shift past 63 bits ===
print(1 << 100)   # 1267650600228229401496703205376

# === 8. Floor division of bigints ===
print((2 ** 100) // (2 ** 50))   # 1125899906842624

# === 9. Modulo of bigint ===
print((2 ** 100) % 3)   # 1

# === 10. Comparisons ===
print(2 ** 100 > 2 ** 99)    # True
print(2 ** 64 == 2 ** 64)    # True
print(2 ** 100 < 2 ** 100)   # False

# === 11. str() conversion ===
print(str(2 ** 64))   # 18446744073709551616

# === 12. abs() of negative bigint ===
print(abs(-(2 ** 100)))   # 1267650600228229401496703205376

# === 13. hex() of bigint ===
print(hex(2 ** 32))   # 0x100000000
