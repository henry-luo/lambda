// Comprehensive decimal arithmetic test - following Lambda syntax patterns

"===== BASIC DECIMAL LITERALS ====="
3.14159n
2.71828n
0.0n
-1.5n
123456789.987654321n

"===== DECIMAL ADDITION ====="
3.14159n + 2.71828n
0.0n + (-1.5n)
123456789.987654321n + 3.14159n
3.14159n + 5
10 + 2.71828n
(-1.5n) + 0
3.14159n + 1.5
2.5 + 2.71828n

"===== DECIMAL SUBTRACTION ====="
3.14159n - 2.71828n
2.71828n - 3.14159n
0.0n - (-1.5n)
3.14159n - 3
5 - 2.71828n
3.14159n - 1.14159
5.5 - (-1.5n)

"===== DECIMAL MULTIPLICATION ====="
3.14159n * 2.71828n
(-1.5n) * (-1.5n)
0.0n * 123456789.987654321n
3.14159n * 2
3 * (-1.5n)
2.71828n * 0
3.14159n * 0.5
2.0 * 2.71828n

"===== DECIMAL DIVISION ====="
3.14159n / 2.71828n
123456789.987654321n / 3.14159n
(-1.5n) / (-1.5n)
3.14159n / 2
10 / 2.71828n
3.14159n / 2.0
6.28318 / 3.14159n

"===== DECIMAL MODULO ====="
7.5n % 2.3n
3.14159n % 1.0n
7.5n % 3
10 % 2.3n

"===== DECIMAL POWER ====="
2.71828n ^ 2
(-1.5n) ^ 3
3.14159n ^ 0
2.71828n ^ 0.5n
3.14159n ^ 0.0n
2 ^ 2.71828n
3.14159n ^ 1.5

"===== MIXED TYPE OPERATIONS ====="
let int_val = 42, float_val = 3.14, decimal_val = 2.718281828n
int_val + decimal_val
decimal_val + int_val
float_val + decimal_val
decimal_val + float_val
int_val - decimal_val
decimal_val - int_val
float_val - decimal_val
decimal_val - float_val
int_val * decimal_val
decimal_val * int_val
float_val * decimal_val
decimal_val * float_val
int_val / decimal_val
decimal_val / int_val
float_val / decimal_val
decimal_val / float_val

"===== COMPLEX EXPRESSIONS ====="
(3.14159n + 2.71828n) * (3.14159n - 2.71828n)
3.14159n * 3.14159n + 2.71828n * 2.71828n
(3.14159n + 2.71828n) / (3.14159n * 2.71828n)
3.14159n ^ 2 + 2 * 3.14159n * 2.71828n + 2.71828n ^ 2
(decimal_val + int_val) * float_val
(float_val * decimal_val) / int_val
int_val ^ 2 + decimal_val ^ 2 + float_val ^ 2

"===== PRECISION TEST ====="
let precise_decimal = 0.1n + 0.2n, float_addition = 0.1 + 0.2
precise_decimal
float_addition
(precise_decimal == 0.3n)
(float_addition == 0.3)

"===== FINANCIAL CALCULATION ====="
let price = 19.99n, tax_rate = 0.08n, quantity = 3
let subtotal = price * quantity
let tax_amount = subtotal * tax_rate
let total = subtotal + tax_amount
price
tax_rate
quantity
subtotal
tax_amount
total

"===== MATHEMATICAL CONSTANTS ====="
let pi_approx = 3.141592653589793238462643383279n
let e_approx = 2.718281828459045235360287471353n
let golden_ratio = 1.618033988749894848204586834366n
pi_approx
e_approx
golden_ratio
pi_approx * 2
e_approx ^ 2
golden_ratio^2 - golden_ratio - 1

"===== BOUNDARY VALUES ====="
let max_precision = 1.23456789012345678901234567890123456789n
let min_nonzero = 0.00000000000000000000000000000000000001n
max_precision
min_nonzero
max_precision + min_nonzero
max_precision - min_nonzero

"===== COMPARISONS ====="
(3.14159n == 3.14159n)
(3.14159n == 2.71828n)
(3.14159n > 2.71828n)
(2.71828n < 3.14159n)
(3.14159n >= 3.14159n)
(2.71828n <= 3.14159n)
"-----"
(decimal_val > int_val)
(decimal_val < float_val)
(int_val >= decimal_val)

"===== ZERO OPERATIONS ====="
let zero = 0.0n
3.14159n + zero
3.14159n - zero
3.14159n * zero
zero * 3.14159n
zero / 3.14159n
zero + zero
zero - zero

"===== DECIMAL NEGATION ====="
-3.14159n
-0.0n
let dval = 3.14159n
-dval
let neg_dval = -1.5n
-neg_dval
-(3.14159n + 2.71828n)
(3.14159n + 2.71828n) - 1.0n
(1.5n) - (0.5n)
(-1.5n) - (-0.5n)

"Decimal arithmetic test completed successfully"
