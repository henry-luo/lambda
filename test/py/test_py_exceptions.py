# Test basic try/except
try:
    x = 10 / 0
except:
    print("caught division error")

# Test try with raise
def divide(a, b):
    if b == 0:
        raise "division by zero"
    return a / b

try:
    result = divide(10, 0)
except:
    print("caught raised error")

# Test normal flow (no exception)
try:
    x = 42
    print(x)
except:
    print("should not print")

# Test try/finally
try:
    y = 100
except:
    print("no error expected")
finally:
    print("finally ran")

print(y)

# Test except catches raised string
try:
    raise "custom error message"
except:
    print("caught custom")

# Test nested try
try:
    try:
        raise "inner error"
    except:
        print("inner caught")
    print("outer ok")
except:
    print("outer caught")
