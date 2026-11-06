// S11.4.8v2/TG18: defaults observe admitted predecessors without binding T.
fn observed(T: type, value: T, witness = type(value)) type => witness
fn reference_default(T: type, value: T = 1, witness = type(value)) type => witness
fn joined(left: number as T, value: T, right: number as T,
        witness = type(value)) type => witness
fn default_site(value: number as T = 1) type => T
fn optional_site(value?: number as T) type => T
fn nested_site(values: (number as T)[] = [1, 2]) type => T
fn repeated(first: number as T = 1, second: number as T = 2) {
    [T, type(first), type(second)]
}
let selected = repeated(second: 2.5);
[
    observed(float, 1) == float,
    observed(int, 1.0) == int,
    reference_default(float) == float,
    joined(1, 2, 3.5) == float,
    default_site() == number,
    default_site(1) == int,
    optional_site() == number,
    nested_site() == number,
    repeated() == [number, int, int],
    selected == [float, int, float]
]
