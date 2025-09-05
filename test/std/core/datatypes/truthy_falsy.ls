// Test: Truthy and Falsy Values
// Category: core/datatypes
// Type: positive
// Expected: 1

// Falsy values
let f1 = !!false      
f1
let f2 = !!0          
f2
let f3 = !!""         
f3
let f4 = !!null       
f4
let f5 = !!undefined  
f5
let f6 = !!NaN        
f6
let t1 = !!true           
t1
let t2 = !!1              
t2
let t3 = !!-1             
t3
let t4 = !!"hello"        
t4
let t5 = !![]            
t5
let t6 = !!{}            
t6
let t7 = !![0]           
t7
let t8 = !!function(){}  
t8
let result1 = "" || "default"     
result1
let result2 = 0 || 42            
result2
let result3 = null ?? "fallback"  
result3
1
