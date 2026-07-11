// Comprehensive decimal arithmetic test - following Lambda syntax patterns

"===== BASIC DECIMAL LITERALS ====="
3.14159m
2.71828m
0.0m
-1.5m
123456789.987654321m

"===== DECIMAL ADDITION ====="
3.14159m + 2.71828m
0.0m + (-1.5m)
123456789.987654321m + 3.14159m
3.14159m + 5
10 + 2.71828m
(-1.5m) + 0
3.14159m + 1.5
2.5 + 2.71828m

"===== DECIMAL SUBTRACTION ====="
3.14159m - 2.71828m
2.71828m - 3.14159m
0.0m - (-1.5m)
3.14159m - 3
5 - 2.71828m
3.14159m - 1.14159
5.5 - (-1.5m)

"===== DECIMAL MULTIPLICATION ====="
3.14159m * 2.71828m
(-1.5m) * (-1.5m)
0.0m * 123456789.987654321m
3.14159m * 2
3 * (-1.5m)
2.71828m * 0
3.14159m * 0.5
2.0 * 2.71828m

"===== DECIMAL DIVISION ====="
3.14159m / 2.71828m
123456789.987654321m / 3.14159m
(-1.5m) / (-1.5m)
3.14159m / 2
10 / 2.71828m
3.14159m / 2.0
6.28318 / 3.14159m

"===== DECIMAL MODULO ====="
7.5m % 2.3m
3.14159m % 1.0m
7.5m % 3
10 % 2.3m

"===== DECIMAL POWER ====="
2.71828m ** 2
(-1.5m) ** 3
3.14159m ** 0
2.71828m ** 0.5m
3.14159m ** 0.0m
2 ** 2.71828m
3.14159m ** 1.5

"===== MIXED TYPE OPERATIONS ====="
let int_val = 42, float_val = 3.14, decimal_val = 2.718281828m
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
(3.14159m + 2.71828m) * (3.14159m - 2.71828m)
3.14159m * 3.14159m + 2.71828m * 2.71828m
(3.14159m + 2.71828m) / (3.14159m * 2.71828m)
3.14159m ** 2 + 2 * 3.14159m * 2.71828m + 2.71828m ** 2
(decimal_val + int_val) * float_val
(float_val * decimal_val) / int_val
int_val ** 2 + decimal_val ** 2 + float_val ** 2

"===== PRECISION TEST ====="
let precise_decimal = 0.1m + 0.2m, float_addition = 0.1 + 0.2
precise_decimal
float_addition
(precise_decimal == 0.3m)
(float_addition == 0.3)

"===== FINANCIAL CALCULATION ====="
let price = 19.99m, tax_rate = 0.08m, quantity = 3
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
let pi_approx = 3.141592653589793238462643383279m
let e_approx = 2.718281828459045235360287471353m
let golden_ratio = 1.618033988749894848204586834366m
pi_approx
e_approx
golden_ratio
pi_approx * 2
e_approx ** 2
golden_ratio**2 - golden_ratio - 1

"===== BOUNDARY VALUES ====="
let max_precision = 1.234567890123456789012345678901234m
let min_nonzero = 0.00000000000000000000000000000000000001m
max_precision
min_nonzero
max_precision + min_nonzero
max_precision - min_nonzero

"===== COMPARISONS ====="
(3.14159m == 3.14159m)
(3.14159m == 2.71828m)
(3.14159m > 2.71828m)
(2.71828m < 3.14159m)
(3.14159m >= 3.14159m)
(2.71828m <= 3.14159m)
"-----"
(decimal_val > int_val)
(decimal_val < float_val)
(int_val >= decimal_val)

"===== ZERO OPERATIONS ====="
let zero = 0.0m
3.14159m + zero
3.14159m - zero
3.14159m * zero
zero * 3.14159m
zero / 3.14159m
zero + zero
zero - zero

"===== DECIMAL NEGATION ====="
-3.14159m
-0.0m
let dval = 3.14159m
-dval
let neg_dval = -1.5m
-neg_dval
-(3.14159m + 2.71828m)
(3.14159m + 2.71828m) - 1.0m
(1.5m) - (0.5m)
(-1.5m) - (-0.5m)

"Decimal arithmetic test completed successfully"
