'=== cross-family ==='
'a' == "a"
'a' != "a"
1 == "1"
{a: 1} == [1]
null == "null"
true == 1

'=== preserved families ==='
1 == 1.0
[1, 2, 3] == (1 to 3)
[1, 2] == [1.0, 2.0]

'=== maps ==='
{a: 1, b: 2} == {b: 2, a: 1}
{a: 1, b: 2} == {a: 1, b: 3}

'=== arraynum shape ==='
reshape([1, 2, 3, 4], [2, 2]) == reshape([1, 2, 3, 4], [4, 1])
reshape([1, 2, 3, 4], [2, 2]) == [[1, 2], [3, 4]]
