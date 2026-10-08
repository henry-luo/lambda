// Default-expression closures need their own plans and capture the callee's earlier slots.
fn invoke(x, callback = (value) => value + 1) => callback(x)
fn captured(x, callback = (value) => value + x) => callback(5)
fn factory(x, callback = (value) => (delta) => value + delta + x) => callback(5)(7)
fn nested(x, callback = (value) => { let values = [value, x]; sum(values) }) => callback(5);
[invoke(3), invoke(3, (value) => value * 2), captured(9), factory(10), nested(12)]
