(a=12, a+10)
let b=" world", c=12;
"hello" + b
if (c > 10) { "great" }

// 'b' should overide global 'b'
(a=0.5, b=2, a + 3 * b, 100.5, 3 + 7.4, 5 / 2, 5_/2, 3**3, 17 % 9, (2.5 > 1), 
 (5 <= 5.0), ((7-3.5) > 5), "hello" + "world",
 if (100>90) "great" else "not great")

// test for statement
for (b in [123]) { b+1 }
for a in ["a", "b", "c"] { "ha!" }
for (a in ["a"]) { "wah!" }

let map = {a:123, b:-456, c:0.5, d:true, e:false, f:null, 
    g:"hello", h:'world', i:t'2025-05-01', j:b'\xA0FE', 
    k:(120, 1e-2), l:[121], m:[true, 124], n:{a:'hello', b:0.5}}
map.a; map.b; map.c; map.d; map.e; [map.f]; map.g; map.h; map.i; map.j;

let d:float = 123
(d+4)
