// D5.2.3, D5.3.3: dynamic arguments must outlive a moving argument List.
fn inspect(value, a, b, c) map {
    let before = string(value)
    let keep = [for (i in 0 to 1200) {index:i, values:[float(i), float(i)*0.3], label:string(i)}];
    {stable:before==string(value), value:value, count:len(keep)}
}
fn identity(value, a, b, c) {
    let keep = [for (i in 0 to 1200) {index:i, label:string(i)}];
    if (len(keep)>0) value else c
}
fn optional(value, a, b, c?:int) => inspect(value,a,b,c)
fn rest(value, ...) => inspect(value,1,2,3)
let dispatch = {inspect:inspect, identity:identity, optional:optional, rest:rest}
let numbers = [4316279033673738697u64, 9007199254740993i64, 1.0e-310];
[for (value in numbers,
    let probe=dispatch.inspect(value,1,2,3),
    let again=dispatch.inspect(value,1,2,3),
    let opt=dispatch.optional(value,1,2),
    let tail=dispatch.rest(value,1,2,3,4))
    [probe.stable,probe.value==value,probe.count==1201,probe==again,
     dispatch.identity(value,1,2,3)==value,opt.stable,opt.value==value,
     tail.stable,tail.value==value]]
