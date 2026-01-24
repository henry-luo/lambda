fn add(a, b) { a + b }
fn add_i(a:int, b:int) { a + b }
fn add_f(a:float, b:float) { a + b }
fn add_f2(a, b:float) { a + b }

add(1, 1)
add_i(1, 2)
// add_i(1, 3.0)  // type error
// add_i(1, 4.6)  // type error
add_f(1, 1.5)
add_f(1.0, 2.0)
add_f2(1.0, 3.5)
add_f2(2, 3.5)

let a=123, b=a*2, c=a+2, d=add(1,1)
(b, c)
add_i(b, c)
add_i(d, 2)
add_f(b, c)
add_f(d, 3)

// let a = fn();
// let a = fn():int;
// let a:int = fn();
// let a:int = fn():int;

// fn concat(a, b) { a + b }
// fn concat(a:string, b:int) { a + str(b) }

{a:123, b:-456, c:0.5};
10+add(1,1)  // 10*add(1,1)