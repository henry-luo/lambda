// Test: Set Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Set literals (if supported, otherwise create from arrays)
let set1 = new Set([1, 2, 3, 4, 5])
set1
let set2 = new Set([4, 5, 6, 7, 8])
set2
set1.add(6)         // Add element
let has = set1.has(3)  
has
let size = set1.size   
size
set1.delete(1)       // Remove element
let union = new Set([...set1, ...set2])  
union
let intersection = new Set([...set1].filter(x => set2.has(x)))  
intersection
let difference = new Set([...set1].filter(x => !set2.has(x)))   
difference
let array_from_set = [...set1]  
array_from_set
1
