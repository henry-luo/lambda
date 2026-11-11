// S1.6 / S4.1.5: batched ranks preserve the scalar keyed shuffle and its ordering.
import rng: lambda.procart.rng
fn reference(k:u64,items) array^ => [for (e in sort([for (i,x in items) {r:rng.unit(rng.at(k,i)),x:x}],(e)=>e.r)^) e.x]
let cases=[[],[7],[for (i in 0 to 31) i%5],['a','b','a','c'],[{id:1},{id:2}],
    [for (i in 0 to 255) i]];
all([for (seed in -3 to 4,items in cases,let k=rng.key(seed)^)
    rng.shuffle(k,items)==reference(k,items)^])^
