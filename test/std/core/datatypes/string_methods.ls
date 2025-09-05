// Test: String Manipulation Methods
// Category: core/datatypes
// Type: positive
// Expected: 1

let str = "Hello, World! This is a test string."
str
let upper = str.upper()        
upper
let lower = str.lower()        
lower
let title = str.title()        
title
let trim = "   hello   ".trim()  
trim
let ltrim = "   hello".ltrim()   
ltrim
let rtrim = "hello   ".rtrim()   
rtrim
let contains = str.contains("World")  
contains
let starts = str.starts_with("Hello") 
starts
let ends = str.ends_with("string.")   
ends
let index = str.index_of("World")     
index
let last_index = str.last_index_of("s") 
last_index
let words = str.split()  
words
let csv = "a,b,c,d".split(",")  
csv
let joined = ["a", "b", "c"].join("-")  
joined
let replaced = str.replace("World", "Universe")  
replaced
let regex_replaced = str.replace(/\s+/, " ")  
regex_replaced
let padded = "42".pad_start(5, "0")  
padded
let padded_end = "42".pad_end(5, "!")  
padded_end
1
