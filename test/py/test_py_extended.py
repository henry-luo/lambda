# Extended Python test for Lambda transpiler

# --- Arithmetic ---
print(2 + 3)        # 5
print(10 - 4)       # 6
print(3 * 7)        # 21
print(15 // 4)      # 3
print(17 % 5)       # 2
print(2 ** 10)      # 1024

# --- Float arithmetic ---
print(1.5 + 2.5)    # 4.0
print(10.0 / 3.0)   # 3.3333...

# --- String operations ---
s = "hello"
print(s.upper())    # HELLO
print(s.replace("l", "r"))  # herro
print(len(s))       # 5

# --- Boolean logic ---
print(True and False)   # False
print(True or False)    # True
print(not True)         # False

# --- Comparisons ---
print(5 > 3)       # True
print(3 >= 3)      # True
print(2 < 1)       # False
print(1 == 1)      # True
print(1 != 2)      # True

# --- List operations ---
nums = [10, 20, 30, 40, 50]
print(nums[0])      # 10
print(nums[2])      # 30
print(len(nums))    # 5

# --- Nested functions ---
def double(x):
    return x * 2

def apply_twice(f, x):
    return f(f(x))

print(double(5))          # 10
# print(apply_twice(double, 3))  # 12  (higher-order, skip for now)

# --- For loop with accumulator ---
total = 0
for i in range(1, 11):
    total = total + i
print(total)              # 55

# --- While loop ---
count = 0
n = 1
while n < 100:
    n = n * 2
    count = count + 1
print(count)              # 7 (2,4,8,16,32,64,128)
print(n)                  # 128

# --- Conditional expressions ---
x = 10
msg = "even" if x % 2 == 0 else "odd"
print(msg)                # even

# --- Nested if/elif/else ---
score = 85
if score >= 90:
    grade = "A"
elif score >= 80:
    grade = "B"
elif score >= 70:
    grade = "C"
else:
    grade = "F"
print(grade)              # B

# --- Multiple assignment ---
a = 100
b = 200
a = a + b
print(a)                  # 300

# --- Fibonacci ---
def fib(n):
    if n <= 1:
        return n
    a = 0
    b = 1
    i = 2
    while i <= n:
        c = a + b
        a = b
        b = c
        i = i + 1
    return b

print(fib(10))            # 55
print(fib(20))            # 6765
