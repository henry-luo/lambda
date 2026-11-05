// D6.3.3: private composite values belong to the module's evaluator instance.
let rows = [{value: 11}, {value: 29}]
let nested = [[1.25, 2.5], [3.75]]
let label = "stage" ++ "d"
pub fn records() => rows
pub fn forwarded() => records()
pub fn reader() => () => rows
pub fn propagated() any^ => records()^
pub fn captured_reader() {
    let held = [{value: 41}];
    () => held
}
pub fn nested_value() => nested[1][0]
pub fn text() => label
