// settings are admitted globally; artistic retirement differs from hard work errors (PG12.7).
import s: lambda.procart.scene
fn doc(nodes,cfg={}) element => <procart version:2,seed:42,settings:cfg,*[<resources>,*nodes]>
let configured = s.expand(doc([<sphere>,<sphere segments:6>],{defaults:{sphere:{rings:4,segments:8}}}))^
let count_vertices = sort([for (g in configured.resources) len(g.positions) div 3]);
[count_vertices == [35,45],configured.stats.vertices==80,
 (s.expand(doc([<box>,<sphere>],{max_objects:1})) ^ {null})==null,
 (s.expand(doc([<sphere>],{max_vertices:100})) ^ {null})==null,
 (s.expand(doc([<repeat count:3, <group>>],{max_steps:3})) ^ {null})==null];
let retired = s.expand(doc([<box scale:2.0>,<box>],{max_size:2.0}))^;
[retired.stats.objects==1,retired.stats.size_retired==1,
 (s.expand(doc([<box>],{max_depth:-1})) ^ {null})==null,
 (s.expand(doc([<box>],{max_size:0.1,min_size:0.2})) ^ {null})==null,
 (s.expand(doc([<box>],{defaults:{sphere:{points:[]}}})) ^ {null})==null,
 (s.expand(doc([<box>],{max_objects:100001})) ^ {null})==null,
 (s.expand(doc([<box>],{unknown:1})) ^ {null})==null];
let a = s.expand(doc([<box>,<sphere position:[1.0,0.0,0.0]>]))^
let b = s.expand(parse(format(doc([<box>,<sphere position:[1.0,0.0,0.0]>]),'mark')^,'mark')^)^;
[a.parts == b.parts,a.resources == b.resources]
;
fn reject(request) bool => false
fn empty_parts(request) map => {resources:[],parts:[]}
let registry={'test@1':{validate:reject,generate:empty_parts}}
let rejected=<procart version:2,seed:0,<resources <recipe id:"test-shape",kind:"tree",generator:"test",version:1,parameters:{}>>;
 <tree recipe:"test-shape">>;
[(s.expand(rejected,registry) ^ {null})==null,
 (s.expand(doc([<box mystery:1>])) ^ {null})==null,
 (s.expand(doc([<box scale:0.0000001>])) ^ {null})==null,
 (s.expand(doc([<sphere segments:2>])) ^ {null})==null]
