// Test: String Manipulation Methods
// Category: core/datatypes
// Type: positive
// Expected: 1

let str = "Hello, World! This is a test string."

// Case conversion
let upper = str.upper()        // "HELLO, WORLD! THIS IS A TEST STRING."
let lower = str.lower()        // "hello, world! this is a test string."
let title = str.title()        // "Hello, World! This Is A Test String."

// Trimming
let trim = "   hello   ".trim()  // "hello"
let ltrim = "   hello".ltrim()   // "hello"
let rtrim = "hello   ".rtrim()   // "hello"

// Searching
let contains = str.contains("World")  // true
let starts = str.starts_with("Hello") // true
let ends = str.ends_with("string.")   // true
let index = str.index_of("World")     // 7
let last_index = str.last_index_of("s") // 23

// Splitting and joining
let words = str.split()  // ["Hello,", "World!", "This", "is", "a", "test", "string."]
let csv = "a,b,c,d".split(",")  // ["a", "b", "c", "d"]
let joined = ["a", "b", "c"].join("-")  // "a-b-c"

// Replacement
let replaced = str.replace("World", "Universe")  // "Hello, Universe! This is a test string."
let regex_replaced = str.replace(/\s+/, " ")  // Replace multiple spaces with one

// Padding
let padded = "42".pad_start(5, "0")  // "00042"
let padded_end = "42".pad_end(5, "!")  // "42!!!"

// Final check
1
