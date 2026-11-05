// S11.4.8v2/S7.7.2v2: defaults, imports, empty sites and retained failures.
import source: ._mir_mvp_binder_module
fn defaults(T: type, value: T = 1) T => value
fn empty(value: (int as T)[]) type => T
fn rejected(T: type, value) {
    let result = source.identity(T, value)
    return result is error
}
fn wrong(value: as T) T => 0
fn local(T: type, value: T, replacement: any) any {
    var kept: T = value
    kept = replacement
    return kept
}
let failed_return = wrong("text")
let failed_store = local(int, 1, "wrong")
let dynamic = {identity: source.identity};
[
    defaults(int) == 1,
    type(defaults(float)) == float,
    source.identity(float, 1) == 1.0,
    dynamic.identity(string, "text") == "text",
    source.selected(1) == int,
    source.selected("text") == string,
    rejected(int, "bad"),
    failed_return is error,
    failed_store is error,
    empty([]) == int
]
