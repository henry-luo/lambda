// Test: Cross-Type Equality
// Layer: 1 | Category: boundary | Covers: == and != across all type pairs

// ===== Same-type equality (==) =====
42 == 42
3.14 == 3.14
true == true
false == false
null == null

// ===== Same-type inequality (!=) =====
42 != 43
3.14 != 2.71
true != false

// ===== Int × Float (numeric promotion) =====
42 == 42.0
0 == 0.0
-5 == -5.0
42 != 43.0
0.0 != 1

// ===== Int × Other types =====
42 == "42"
42 == true
42 == null
42 == [42]
42 == {v: 42}
0 == null
0 == false
1 == true

// ===== Float × Other types =====
3.14 == "3.14"
3.14 == true
3.14 == null
0.0 == false
0.0 == null
1.0 == true

// ===== String × Other types =====
"true" == true
"false" == false
"null" == null
"" == null
"0" == 0
"1" == true
"hello" == [1]
"hello" == {a: 1}

// ===== Bool × Other types =====
true == 1
false == 0
true == null
false == null
true == "true"
false == ""

// ===== Null × Other types =====
null == false
null == 0
null == ""
null == 0.0
null == []
null == {}

// ===== Array equality =====
[1, 2] == [1, 2]
[] == []
[1] == [1, 2]
[1] == 1

// ===== Map equality =====
{a: 1} == {a: 1}
{} == {}
{} == []
{} == null

// ===== Cross-type inequality (!=) =====
42 != "42"
42 != true
42 != null
"hello" != null
"" != null
null != 0
null != false
null != ""
null != null
[1] != [1]
{a: 1} != {a: 1}

// ===== Symmetry verification =====
// Verify a == b and b == a give same result
let eq1 = (42 == 42.0); let eq2 = (42.0 == 42); (eq1 == eq2)
let eq3 = (42 == null); let eq4 = (null == 42); (eq3 == eq4)
let eq5 = ("" == null); let eq6 = (null == ""); (eq5 == eq6)
