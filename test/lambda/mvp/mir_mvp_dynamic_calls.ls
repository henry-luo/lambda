// D7.2.2/D7.4.2: boxed callbacks retain argument boundaries and owned scalar returns.
fn apply_one(callback: any, value: any) any { callback(value) }
fn apply_four(callback: any, a: any, b: any, c: any, d: any) any { callback(a, b, c, d) }
fn make_callback() { (value) => value + 1 }
fn enforce(callback: fn(any) int, value: any) int^ { callback(value) }
pn argument_effect() { print("unexpected argument\n"); return 1 }

pn main() {
    let callback = (value) => value + 2
    print(apply_one(callback, 5)); print("\n")
    print(apply_one(make_callback(), 8)); print("\n")
    let collect = (a, b, c, d) => [a, b, c, d]
    print(apply_four(collect, (1, 2), null, "a", "b")); print("\n")
    let scalar = (value) => value + 0.5
    let saved = apply_one(scalar, 2.0)
    let another = apply_one(scalar, 9.0)
    print([saved, another]); print("\n")
    let tiny = (value) => value * 0.5
    let kept = apply_one(tiny, 1e-310)
    let churn = apply_one(tiny, 1e-300)
    print([kept == 5e-311, churn == 5e-301]); print("\n")
    let wrong_result = (value) => "wrong"
    let contract_error = enforce(wrong_result, 0)
    print(contract_error is error); print("\n")
    let procedure = pn (value) => { print("unexpected procedure\n"); value }
    let colour_error = apply_one(procedure, 1)
    print(colour_error is error); print("\n")
    let non_callable: any = 7
    let rejected = non_callable(1)
    print(rejected is error); print("\n")
    let failed: any = shl(1, -1)
    let forwarded = failed(argument_effect())
    print(forwarded is error); print("\n")
}
