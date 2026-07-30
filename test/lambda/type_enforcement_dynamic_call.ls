// Dynamic calls still honour optional/default/variadic parameter contracts.
fn optional_value(a: int, b?: int) any { [a, b] }
fn default_value(a: int, b: int = a + 1) any { a * b }
fn variadic_value(a: int, ...) any { [a, len(varg())] }

fn dynamic_optional() any { optional_value }
fn dynamic_default() any { default_value }
fn dynamic_variadic() any { variadic_value }

{
    optional: dynamic_optional()(1),
    defaulted: dynamic_default()(3),
    variadic: dynamic_variadic()(1, 2, 3)
}
