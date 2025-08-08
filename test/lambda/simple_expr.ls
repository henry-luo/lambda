(let a=0.5, let b=2, 
a + 3 * b, a + 1, 0.5 + 3 * 2,
(2.5 > 1))
"str" + "ing"
"Numeric exprs: ============"
let a=0.5, b=2, c = 1e2
100.5; 3 + 7.4; 23 - c; 
3 * 4; 2.5 * 2; 2.5 / 2; 5 / 2; 5_/2; 3^3; 17 % 9
// 1 + inf; 1 + -inf; 1/0; 1/inf; 1 + 0/0; 1 + nan; inf * 0; inf * 2;  inf - inf;
// todo: decimal exprs
"Comparison exprs: ============"
(2.5 > 1); (2.5 < a); (b >= 1); (2.5 <= c); (2.5 == 1); (2.5 != 1);
"Logic exprs: ============"
(2.5 > 1 and 2.5 < a); (b >= 1 or 2.5 <= a); not(2.5 == 1); not(2.5 != 1);