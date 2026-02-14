(let a=0.5, let b=2, 
a + 3 * b, a + 1, 0.5 + 3 * 2,
(2.5 > 1))
"str" ++ "ing"
'symbol' ++ '-xyz'
"Numeric exprs: ============"
let a=0.5, b=2, c = 1e2
100.5; 3 + 7.4; 23 - c; 
3 * 4; 2.5 * 2; 2.5 / 2; 5 / 2; 5 div 2; 3^3; 17 % 9
"Inf/NaN:"
1 + inf; 1 + -inf; 1/0; 1/inf; 1 + 0/0; 1 + nan; inf * 0; inf * 2; inf - inf
// todo: decimal exprs
"Comparison exprs: ============"
(2.5 > 1); (2.5 < a); (b >= 1); (2.5 <= c); (2.5 == 1); (2.5 != 1);
"Logic exprs: ============"
(2.5 > 1 and 2.5 < a); (b >= 1 or 2.5 <= a); not(2.5 == 1); not(2.5 != 1);
'Type exprs: ============'
null; int; float; string; bool; symbol; any; function; type;
type(null); type(123); type(1.23); type("str"); type(true); type('symbol'); type(int);
type(1 to 10); type([1, 2, 3]); type((3, 4, 5)); type({a: 1, b: 2}); type(<elmt a: 1, b: 2>);
'Len exprs: ============'
len(null); len(123); len(1.23); len("str"); len(true); len('symbol'); len(int);
len(1 to 10); len([1, 2, 3]); len((3, 4, 5)); len({a: 1, b: 2}); len(<elmt a: 1, b: 2; "text">);
"Dynamic container exprs: ============"
let m = {'a': 1, "b": c + 2, d:[true, null], e: <elmt a: 1, b: 2>, f:int}
[1, a, 3];  (3, b, 5);  m; 
<elmt a: 1, b: 2, c:{d:1+5.0, e:type(1.0)}>
{c:1, m, g:2} // todo: <elmt c:1, m, d:2>
"Member exprs: ============"
[m.a,  m.b,  m.c,  m.d,  m.d[0],  len(m.d),  m.e,  m.e.a,  m.e.b,  m.f,  m.g,
m["a"],  m["b"],  m["c"],  m["d"],  m["e"],  m["f"],  m["g"]]
"elmt:"
let elm = <elmt a: 1, b:{d:t'2025-12-01'}, c: 3; "text"; 'symbol'; [1,2,3]>
[elm.a,  elm.b,  elm.c,  elm.d,  elm[-1], elm[0],  elm[1],  elm[2],  elm[3]]
"Index exprs: ============"
let arr = [1, "str", true]
[arr[-1],  arr[0],  arr[1],  arr[2],  arr[3], arr[1.0], arr[1.5]];

// todo: negative cases, e.g. 1 + "str", 1 + null, etc.
