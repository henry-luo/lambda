type Context = {column: int, indent: int}
type Result = {text: string, count: int}
fn result(text: string, count: int) Result => {text: text, count: count}
fn count(ctx: Context, remaining: int) Result {
    if (remaining <= 0) result("x", ctx.column + ctx.indent)
    else count({column: ctx.column + 1, indent: ctx.indent}, remaining - 1)
}
fn forward(ctx: Context) Result => count(ctx, 4)
fn render(ctx: Context) Result {
    let child = forward(ctx)
    result(child.text, child.count + ctx.indent)
}
fn swap(left: Context, right: Context, remaining: int) Result {
    if (remaining <= 0) result("s", left.column * 10 + right.column)
    else swap(right, left, remaining - 1)
}
fn chain(ctx: Context, remaining: int) Result {
    if (remaining <= 0) result("", ctx.column)
    else {
        let child = step(ctx)
        chain({column: child.count, indent: ctx.indent}, remaining - 1)
    }
}
fn step(ctx: Context) Result => result("x", ctx.column + 1)
let callback = forward;
[count({column: 2, indent: 3}, 0), count({column: 2, indent: 3}, 4),
 render({column: 2, indent: 3}), callback({extra: true, indent: 3, column: 2}),
 swap({column: 2, indent: 0}, {column: 7, indent: 0}, 3),
 chain({column: 2, indent: 3}, 4)]
