// D7.2.2/D7.4.2: escaping named functions keep checked boxed admission.
fn invoke(callback: any, value: any) any { callback(value) }
fn increment(value: int) int { value + 1 }
fn doubled(value: float) float { value * 2.0 }
fn observed_float(value: float) float { value }
fn factorial(value: int) int { if (value <= 1) 1 else value * factorial(value - 1) }
fn select_named() { doubled }
fn select_nested() {
    fn local_increment(value: int) int { value + 3 }
    local_increment
}
pn procedure_identity(value: any) any { value }
pn invoke_procedure(callback: any, value: any) any { callback(value) }

pn main() {
    print(invoke(increment, 4)); print("\n")
    print(invoke(doubled, 3)); print("\n")
    print(invoke(select_named(), 2.5)); print("\n")
    print(invoke(select_nested(), 7)); print("\n")
    print(invoke(factorial, 5)); print("\n")
    let rejected = invoke(increment, "bad")
    print(rejected is error); print("\n")
    print(invoke(procedure_identity, 1) is error); print("\n")
    print(invoke_procedure(procedure_identity, 8)); print("\n")
    let callbacks = [increment, doubled]
    print(invoke(callbacks[0], 9)); print("\n")
    let saved = invoke(doubled, 1e-310)
    let churn = invoke(doubled, 1e-300)
    print([saved == 2e-310, churn == 2e-300]); print("\n")
    print(observed_float(1.25)); print("\n")
    let escaped_float = observed_float
    print(escaped_float(2.5)); print("\n")
}
