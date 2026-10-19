// S3.4/S3.5, S5.1.1v2/S5.1.5, S6.1.2v2: errors retain their own payload.
fn retained(value: any) => (value is error) and value.message == "retained"
fn dynamic(value: any) any => value
fn negated(value: any) bool | error => not value
fn equal(a: any, b: any) bool | error => a == b
fn conditional(value: any) int | error => if (value) 1 else 2
fn closure(value: any) => () => value

let e = error("retained")
let a = [e]
let m = {a: e}
let el = <p e>
let f = closure(e);
[
    // Error operands never choose a default or a branch.
    [retained(e or 9), retained(e and 9), retained(not e),
        retained(if (e) 1 else 2), retained(if (e) 1)],
    [retained(false or e), retained(true and e),
        false and e, true or e, not null, not false, not "", not 0],
    [null or 3, false or 4, "" or 5, 0 or 6,
        e ^ { 7 }, (e ^ { null }) or 8],
    // Direct comparisons preserve errors even against null and numeric poison.
    [retained(e == e), retained(e != null), retained(null == e),
        retained(e < null), retained(null <= e), retained(e > nan), retained(nan >= e),
        retained(e === 1), retained(e == true), retained(e != false),
        retained(e == "x"), retained(e == 1)],
    // Shared containers must take the same recursive walk as independent ones.
    [retained(a == a), retained(a != a), retained(m == m), retained(el == el),
        retained([[e]] == [[e]]), retained({a: [e]} == {a: [e]})],
    [([1,e] == [2,e]) == false, retained([e,1] == [e,2]),
        ([e] == [e,1]) == false, (a == 1) == false,
        retained([1,e] != [1,e])],
    // Error-capable functions must agree with direct expression evaluation.
    [retained(negated(e)), retained(equal(a,a)), retained(conditional(e)),
        retained(dynamic(e) or "default"), retained(not dynamic(e)),
        retained(if (dynamic(e)) 1 else 2), retained(dynamic(e) == 1)],
    [retained(f == f), retained(closure(e) == closure(e)),
        (closure(1) == closure(2)) == false],
    // Storing an error does not poison unrelated operations; sort keeps total order.
    [len([1,e,3]) == 3, retained(sort([e,1,null])[2]),
        [1,2] == [1,2], [nan] == [nan], [1] != [2], 1 == 1.0],
    [e is error, type(e) == error, e.code == 318, e.message == "retained"]
]
