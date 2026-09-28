// S11.4.3 (LR12-36): an operand typed `any` admits an error without declaring
// one. A reject-error system function still makes that error its value, in
// every call form, and the caller goes on (S7.7.1: no interior skip). Both
// tiers had returned it from the enclosing function; the JIT's `|>` form read
// `err |> len` as 0, and its typed-array argument path returned early.
fn mk(k) string | error => if (k > 0) error("e") else "a,b"
// an explicit `any` parameter, called and piped
fn direct(m: any) { let n = len(m); [n, "reached"] }
fn piped(m: any) { let n = m |> len; [n, n is error, "reached"] }
fn piped_call(m: any) { let p = m |> split(","); [p, "reached"] }
// a piped join into a typed parameter, a declaration and a return: the JIT
// had judged these by the pipe's static `int` and read the error as 0
fn inc(x: int) => x + 1
fn piped_arg(m: any) { [inc(m |> len), "reached"] }
fn piped_decl(m: any) { let n: int = m |> len; [n, "reached"] }
pn piped_ret(m: any) int { return m |> len }
// a dynamic read of a container that holds the error
fn member(r) { let n = len(r.x); [n, "reached"] }
fn slot(xs) { let s = string(xs[1]); [s, "reached"] }
// rescued, handled, and propagated on request
fn rescued(m: any) { let n = len(m) or 0; [n, "reached"] }
fn handled(m: any) { len(m) ^ { 0 - 1 } }
fn propagated(m: any) { let p = split(m, ",")^; [p, "reached"] }
fn propagated_pipe(m: any) { let p = (m |> split(","))^; [p, "reached"] }
// an unannotated parameter never admits it: that call's value is the error
fn untyped(m) { let n = len(m); [n, "reached"] }
// a typed-array argument's error is the call's value too (S7.7.3)
pn scale(v: float[], s: float) float[] { return [v[0] * s, v[1] * s] }
pn add(a: float[], b: float[]) float[] { return [a[0] + b[0]] }
pn arr_arg(x: float[]) { let r = add(scale(x, 2.0), [1.0]) or [0.0]; [r, "reached"] }
pn main() {
    let e = mk(1)
    print([direct(mk(0)), direct(e)])
    print("\n")
    print([piped(mk(0)), piped(e)])
    print("\n")
    print([piped_call(mk(0)), piped_call(e)])
    print("\n")
    print([piped_arg(mk(0)), piped_arg(e), piped_decl(mk(0)), piped_decl(e)])
    print("\n")
    print([piped_ret(mk(0)), piped_ret(e)])
    print("\n")
    print([member({x: "abc"}), member({x: e})])
    print("\n")
    print([slot([1, 22]), slot([1, e])])
    print("\n")
    print([rescued(mk(0)), rescued(e)])
    print("\n")
    print([handled(mk(0)), handled(e)])
    print("\n")
    print([propagated(mk(0)), propagated(e) or "propagated"])
    print("\n")
    print([propagated_pipe(mk(0)), propagated_pipe(e) or "propagated"])
    print("\n")
    print([untyped(mk(0)), untyped(e)])
    print("\n")
    print([arr_arg([1.5, 2.0]), arr_arg([])])
    print("\n")
}
