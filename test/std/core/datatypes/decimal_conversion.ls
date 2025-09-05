// Test: Decimal Type Conversion
// Category: core/datatypes
// Type: positive
// Expected: 1

// String to decimal conversion
let from_string = decimal("123.456")
from_string
let from_int = decimal(42)
from_int
let from_float = decimal(3.14)
from_float
let price = decimal("19.99")
price
let price_str = string(price)
price_str
let price_float = float(price)
price_float
let price_int = int(price)
price_int
let scientific = decimal("1.23e-4")
scientific
let with_commas = decimal("1234.56")  // Note: Commas are not supported in decimal strings
with_commas
let large_decimal = decimal("79228162514264337593543950335")
large_decimal
let small_decimal = decimal("0.0000000000000000000000000001")
small_decimal
1
