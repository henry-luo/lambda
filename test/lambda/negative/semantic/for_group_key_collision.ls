// @expect-error: E209
// @description: FC9/S14.1.2v2: group key names and the `into` name share one
// post-group scope, so two keys that infer the same name collide.

let pairs = [{a: {region: "w"}, b: {region: "e"}}];
[for (p in pairs group by p.a.region, p.b.region into g) len(g)]
