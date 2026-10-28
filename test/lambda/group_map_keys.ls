// S5.6.1v2 / S5.6.2: independent maps, reordered fields and nested numeric keys.
let repeated=[for (i in 0 to 9) {id:i,surface:{texture:"leaf",roughness:0.85,metalness:0.0}}]
let groups=[for (p in repeated group by ["geometry",p.surface] as key into batch) batch |> ~.id]
let mixed=[{a:1,b:{x:2,y:[3,4]}},{b:{y:[3.0,4],x:2.0},a:1.0},{a:1,b:{x:2,y:[3,5]}}]
let values=[for (p in mixed group by p as key into batch) len(batch)]
let absent=[for (i in 0 to 3) {a:null,b:i%2}]
let null_groups=[for (p in absent group by p as key into batch) len(batch)];
[groups==[[0,1,2,3,4,5,6,7,8,9]],values==[2,1],null_groups==[2,2]]
