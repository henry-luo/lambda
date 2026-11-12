// A projected var write-back keeps the returned map live across GC (D5.4.4, D6.2.1).
import helper: .mod_gc_var_result

pn main() {
    var owner = {child:{value:1}}
    let saved = owner
    let result = helper.change(owner.child)^
    let real = helper.scalar(owner.child, 0)^
    let wide = helper.scalar(owner.child, 1)^
    return {snapshot:saved.child.value, current:owner.child.value, answer:result.answer, nested:result.nested,
        real:real == 1.25, integer:wide == 9223372036854775807i64}
}
