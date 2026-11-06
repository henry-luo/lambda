// S10.2.2v2/S11.1.4: explicit vector masks and type relations share runtime kernels.
fn power(base: any, exponent: any) any { base ** exponent }
fn same_reference(left: any, right: any) any { left === right }
fn below(left: type, right: type) any { left <: right }
pn main() {
    print([power(2, 3), power(4.0, 0.5), power(-2, 3)]); print("\n")
    print(power([2, 3, 4], 2)); print("\n")
    print([1, 2, 3] eq [1, 0, 3]); print("\n")
    print([1, 2, 3] lt 3); print("\n")
    print(2 ge [1, 2, 3]); print("\n")
    print([same_reference(1, 1), same_reference({a: 1}, {a: 1})]); print("\n")
    print([below(int, number), below(number, int), below(int, int | string)]); print("\n")
    let failed: any = shl(1, -1)
    print([power(failed, 2) is error, (failed eq [1]) is error,
           same_reference(failed, 1) is error]); print("\n")
}
