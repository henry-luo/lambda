// Comprehensive comparison operations test - all comparison operators with various types

"===== COMPREHENSIVE COMPARISON OPERATIONS TEST ====="

'Error Cases - Type Mismatches (should produce errors):'
'Testing invalid comparisons...'
"154."; ("hello" < 5)
"155."; (true >= false)
"156."; (null == 3)       // null equality returns false (not error) to enable null checking
"157."; (5 > "world")
"158."; (false <= true)
"159."; ("test" != null)  // null inequality returns true (not error) to enable null checking

'Additional Error Cases - Invalid Type Comparisons:'
"160."; (true == 1)
"161."; (false == 0)
"162."; (1 == true)
"163."; (0 == false)
"164."; (true != 1)
"165."; (false != 0)
"166."; ("string" == true)
"167."; ("string" == false)
"168."; (true == "string")
"169."; (false == "string")

'Error Cases - String Comparisons (non-equality):'
"170."; ("hello" < "world")
"171."; ("abc" > "def")
"172."; ("test" <= "other")
"173."; ("first" >= "second")

'Error Cases - Boolean with String Operations:'
"174."; ("str" and true)
"175."; ("str" and false)
"176."; (true and 'str')
"177."; (false and 'str')
"178."; ('str' or true)
"179."; ('str' or false)
"180."; (true or 'str')
"181."; (false or 'str')
"182."; ('hello' and 'world')

'Error Cases - Null Comparisons:'
"183."; (null < 5)
"184."; (null > 5)
"185."; (null <= 5)
"186."; (null >= 5)
"187."; (5 < null)
"188."; (5 > null)
"189."; (5 <= null)
"190."; (5 >= null)
"191."; (null < null)
"192."; (null > null)
"193."; (null <= null)
"194."; (null >= null)

'Error Cases - Null with Boolean Operations:'
"195."; [null and true]
"196."; [null and false]
"197."; [true and null]
"198."; [false and null]
"199."; [null or true]
"200."; [null or false]
"201."; [true or null]
"202."; [false or null]
"203."; [null and null]
"204."; [null or null]

'Error Cases - Mixed Invalid Types:'
"205."; (true < false)
"206."; (false > true)
"207."; (true <= false)
"208."; (false >= true)
"209."; ('string' and 5)
"210."; (5 and 'string')
"211."; ('string' or 5)
"212."; (5 or 'string')
"213."; (true and 3.14)
"214."; (3.14 and true)
"215."; (false or 2.71)
"216."; (2.71 or false)

'Conditional Expressions with Comparisons:'
"217."; if (5 > 3) 'five is greater' else 'five is not greater'
"218."; if (5 == 5.0) 'equal' else 'not equal'
"219."; if (not (5 < 3)) 'not less than' else 'less than'
"220."; if ((5 <= 5) and (10 >= 10)) 'both true' else 'not both true'

'Variable Comparisons:'
let x = 10
let y = 20  
let z = 10.0

"221."; (x == 10)
"222."; (y > x)
"223."; (x == z)
"224."; (x < y)
"225."; (y >= x)
"226."; (not (x > y))
"227."; ((x == z) and (y > x))

'Complex Arithmetic in Comparisons:'
"228."; ((5 + 3) == 8)
"229."; ((10 - 2) > 7)
"230."; ((3 * 4) <= 12)
"231."; ((15 / 3) == 5)
"232."; (not ((2 ^ 3) != 8))

'Chained Comparisons Logic:'
"233."; ((5 < 10) and (10 < 15))
"234."; ((5 <= 10) and (10 <= 10))
"235."; (not (5 > 10) or (15 > 10))
"236."; (((5 == 5) and (10 != 5)) and (15 > 10))

'Edge Cases with Very Large Numbers:'
"237."; (9223372036854775807 == 9223372036854775807)
// "238."; (9223372036854775806 < 9223372036854775807)
// "239."; (-9223372036854775808 < -9223372036854775807)

'Float Precision Comparisons:'
"240."; ((0.1 + 0.2) == 0.3)
"241."; ((1.0 / 3.0) != 0.33333)
"242."; (3.14159 > 3.14158)
"243."; (2.718281828 <= 2.718281829)

"End of Comparison Expression Tests."
