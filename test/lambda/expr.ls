(a=12, a+10)
let b=" world", c=12;
"hello" + b
if c > 10 { "great" }

// 'b' should overide global 'b'
(a=0.5, b=2, a + 3 * b, 100.5, 3 + 7.4, 5 / 2, 5_/2, 3^3, 17 % 9, (2.5 > 1), 
  (5 <= 5.0), ((7-3.5) > 5), "hello" + "world",
  if (100>90) "great" else "not great")

// // test for statement
for b in [123] { b+1 }
for a in ["a", "b", "c"] { "ha!" }
for a in ["a"] { "wah!" }

let m = {a:123, b:-456, c:0.5, d:true, e:false, f:null, 
    g:"hello", h:'world', i:t'2025-05-01', j:b'\xA0FE', 
    k:(120, 1e-2), l:[121], m:[true, 124], n:{a:'hello', b:0.5}}
m.a; m.b; m.c; m.d; m.e; [m.f]; m.g; m.h; m.i; m.j; m.k; m.l; m.m; m.n;

let d:float = 123
(d+4, not true, not(1>2), -(-2),+(-2))

1+"str"; 1/0; -1/0; 0/0;

(234)  // list with one item

<elmt a:1+2;  4+5;  "hello" + " world";  1+2 is number;  1+2 is int;  3.5/0 is float>

let nested ={a:678, {b:123, c:456}, d:789}
nested.a;  nested.b;  nested.c;  nested.d;

<elmt a:12, &{b:34}, c:56>

// range exprs
1 to 0;  // should be error
12 to 12;  1 to 3;  (1, 2 to 3, 4);  
[1, (2 to 5), 6]
for r in 1 to 5 { r+1 }
for (r in 10 to 15) r+1
type(1 to 3);  1 to 3 is array;  2 in (1 to 3);  4 in (1 to 3);  "a" in (1 to 3);

({a: 1, b: 2, c: 3}.b)
(<e a:"str", b:2>.a)