// Test 1: Basic int64 literals
9223372036854775807
-9223372036854775808

// Test 2: int64 conversion from int32
int64(42)
int64(-123)

// Test 3: int64 conversion from string
int64('1234567890123456789')
int64('-9876543210987654321')  // negative overflow

int('1234567890123456789')
int('-9876543210987654321') 
int(123)
int(3.5)
int(-123)
let dec = 123456789012345678901234567890n
int(dec)

// // Test 4: int64 conversion from float
int64(3.14)
int64(-2.71)

int64(100) + int64(200)
int64(500) - int64(300)
int64(10) * int64(20)
