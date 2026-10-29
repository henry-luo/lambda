// S1.6 / S4.1.5 / PG3: native paths preserve code points, signed bits and positional folds.
import r: lambda.procart.rng
let key=r.key(42)
let paths=[[],[5],[-7,0,9007199254740991],[-1i64,0xffffffffffffffffu64],["oak","树🌿é"],["","\u0000"],["same","same"]]
let expected=[for (p in paths) r.derive_reference(key,p)]
let keys=r.derive_many(key,paths)
let counters=[0,1,2,-1,9007199254740991]
let draws=r.units_for(keys,counters);
[keys==expected,all([for (i,p in paths) r.derive(key,p)==expected[i]]),
 math.rng_keys(key,[[1,2],[3,4]])==[r.derive_reference(key,[1,2]),r.derive_reference(key,[3,4])],
 math.rng_keys(key,[[],[]])==[key,key],r.derive(key,{a:1})==r.derive_reference(key,{a:1}),
 draws==[for (k in expected) for (i in counters) r.unit(r.at(k,i))],
 r.units(key,10000)==r.units_reference(key,10000),r.derive_many(key,[])==[],r.units_for([],[1,2])==[],
 r.units_for([key],[])==[],r.units(key,0)==[]];
[(math.rng_keys(1,paths)^ { true })==true,(math.rng_keys(key,[[true]])^ { true })==true,
 (math.rng_uniform([key],[1.5])^ { true })==true,(math.rng_uniform([1],[0])^ { true })==true]
