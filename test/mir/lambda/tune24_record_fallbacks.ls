type Small = {value: int}
type Wide = {value: float}
fn extra(value: int) Small => {value: value, extra: "kept"}
fn floating(value: float) Wide => {value: value}
fn empty_args() Small => {value: 42}
fn captured(value: int) {
    fn local() Small => {value: value}
    local()
}
let extra_result = extra(3)
let tiny = floating(1e-310)
let clobber = floating(2e-310);
[extra_result.extra, extra_result.value, tiny.value == 1e-310,
 clobber.value == 2e-310, empty_args().value, captured(7).value]
