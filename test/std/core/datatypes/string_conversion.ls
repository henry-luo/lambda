// Test: String Type Conversion and Formatting
// Category: core/datatypes
// Type: positive
// Expected: 1

// String to other types
let num_str = "42"
num_str
let num = num_str as int           
num
let float_num = "3.14" as float    
float_num
let bool_true = "true" as bool     
bool_true
let bool_false = "false" as bool   
bool_false
let int_str = 42 as string         
int_str
let float_str = 3.14 as string     
float_str
let bool_str = true as string      
bool_str
let null_str = null as string      
null_str
let name = "Alice"
name
let age = 30
age
let formatted = `Name: ${name}, Age: ${age}`  
formatted
let price = 19.99
price
let price_str = price.format("0.00")  
price_str
let now = date()
now
let date_str = now.format("YYYY-MM-DD")  
date_str
let padded = "42".pad_start(5)     
padded
let right_aligned = "42".rjust(5)   
right_aligned
let left_aligned = "42".ljust(5)    
left_aligned
let centered = "42".center(6, "-")  
centered
1
