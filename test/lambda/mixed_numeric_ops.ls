// Test mixed numeric type operations (int, int64, float)
// This tests the fix for float × int multiplication

"===== MIXED NUMERIC OPERATIONS TESTS ====="

// 1. Float × Int multiplication
let f = 2.5
let i = 4
"1. float * int:"; (f * i)
"2. int * float:"; (i * f)

// 2. Literal combinations
"3. 3.0 * 7:"; (3.0 * 7)
"4. 7 * 3.0:"; (7 * 3.0)
"5. 2.5 * 10:"; (2.5 * 10)
"6. 10 * 2.5:"; (10 * 2.5)

// 3. Int64 × Float multiplication (using smaller values to avoid overflow)
let i64 = 10000000  // int64 but small enough
let f2 = 1.5
"7. int64 * float:"; (i64 * f2)
"8. float * int64:"; (f2 * i64)

// 4. Int × Int64 multiplication  
let small = 100
let large = 1000000  // int64 but manageable
"9. int * int64:"; (small * large)
"10. int64 * int:"; (large * small)

// 5. Mixed subtraction
"11. float - int:"; (10.5 - 3)
"12. int - float:"; (10 - 2.5)
"13. int64 - float:"; (10000000 - 0.5)
"14. float - int64:"; (1.5 - 1000000)

// 6. Mixed division
"15. float / int:"; (10.0 / 4)
"16. int / float:"; (10 / 4.0)

// 7. Chain operations
"17. float * int + int:"; (2.5 * 4 + 10)
"18. int * float * int:"; (2 * 3.5 * 4)

// 8. In expressions with variables
let a = 42
let b = 2.0
"19. a * b:"; (a * b)
"20. b * a:"; (b * a)

// 9. Negative numbers
"21. -2.5 * 3:"; (-2.5 * 3)
"22. 3 * -2.5:"; (3 * -2.5)
"23. -10 * 1.5:"; (-10 * 1.5)

// 10. Zero cases
"24. 0 * 3.5:"; (0 * 3.5)
"25. 3.5 * 0:"; (3.5 * 0)
"26. 0.0 * 100:"; (0.0 * 100)

"===== END MIXED NUMERIC OPERATIONS TESTS ====="
