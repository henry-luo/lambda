(let a=12, a+10)
let b=" world", c=12;
"hello"++b
if c > 10 { 'great' }

// 'b' should overide global 'b'
(let a=0.5, let b=2, a + 3 * b, 100.5, 3 + 7.4, 5 / 2, 5_/2, 3^3, 17 % 9, (2.5 > 1), 
  (5 <= 5.0), ((7-3.5) > 5), "hello"++"world",
  if (100>90) 'great' else 'not great')

"Test for statement:"
for b in [123] { b+1 }
for a in ["a", "b", "c"] { "ha!" }
for a in ["a"] { 'wah!' }

"Test map:"
let m = {a:123, b:-456, c:0.5, d:true, e:false, f:null, 
    g:"hello", h:'world', i:t'2025-05-01', j:b'\xA0FE', 
    k:(120, 1e-2), l:[121], m:[true, 124], n:{a:'hello', b:0.5}}
m.a; m.b; m.c; m.d; m.e; [m.f]; m.g; m.h; m.i; m.j; m.k; m.l; m.m; m.n;

"Test unary operators:"
let d:float = 123
(d+4, not true, not(1>2), -(-2),+(-2))

1+"str"; 1/0; -1/0; 0/0;

(234)  // list with one item

"Test element:"
<elmt a:1+2;  4+5;  "hello"++" world";  1+2 is number;  1+2 is int;  3.5/0 is float>

let nested = {a:678, {b:123, c:456}, d:789}
nested.a;  nested.b;  nested.c;  nested.d;

let mp2 = {d:78}
<elmt a:12, {b:34}, c:56, {mp2}>  // dynamic attrs
<elmt ;{mp2}>  // mp2 now is content

"Test range exprs:"
1 to 0;  // should be []
12 to 12;  1 to 3;  (1, 2 to 3, 4);  
[1, (2 to 5), 6]
for r in 1 to 5 { r+1 }
for (r in 10 to 15) r+1
type(1 to 3);  1 to 3 is array;  2 in (1 to 3);  4 in (1 to 3);  "a" in (1 to 3);

({a: 1, b: 2, c: 3}.b)
(<e a:"str", b:2>.a)

(let y = (let x=5, x + 3.14), y * 2) 

"Test TYPE_ANY support:"
let v = sum([1, 2]);
{a: v, b: 3}

"Test array:"
// test const array
let test_array = [2.8, 1.2, 1.9, 1.5, 2.1]
for (x in test_array) x
// test list flattening in array
[1, ("great", "!"), 0.5]
// test for loop in array
[for (x in test_array) x * 2]
[if (test_array) (1, 2, "great") else 0]

"Test datetime support:"
t'2025-01-01'
let dt: datetime = t'2025-01-02'
dt
let mp = {a:t'2025-01-03', b:3.14}
mp
mp.a
let arr = [t'2025-01-04', t'2025-01-05']
arr
arr[0]
