// S11.1/S11.4: membership retains structural graphs and first-class type identity.
type Pair = {left: int, right: int}
type Named = <node id: string>
type Ints = int[]
fn fits(value: any, pattern: any) { value is pattern }

pn main() {
    print([t'2025-04-26' is date, t'10:30:00' is date,
        t'10:30:00' is time, t'2025-04-26' is datetime]); print("\n")
    print([[1, 2] is int[], [1, "two"] is int[],
        {left: 1, right: 2} is Pair, {left: 1} is Pair]); print("\n")
    print([<node id: "a"> is Named, <node id: 1> is Named,
        1 is (none | 1), 2 is (none | 1)]); print("\n")
    print([fits(3, int), fits("three", int), fits([1, 2], Ints),
        fits(3, 3), fits(3, 4)]); print("\n")
    let failed = shl(1, -1)
    print([failed is error, failed is int, fits(failed, error), fits(failed, int)]); print("\n")
    print([3 is float, 1.0 is decimal, (1 to 3) is array]); print("\n")
}
