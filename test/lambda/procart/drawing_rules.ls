// exact identity invariants and expected cumulative placements (PG12.5–7).
import s: lambda.procart.scene
fn doc(nodes,defs=<resources>,cfg={}) element => <procart version:2,seed:42,settings:cfg,*[defs,*nodes]>
let resources = <resources <rule name:"branch",
    <variant name:"fork",weight:3,<box id:"piece">; <branch id:"child",scale:0.8,position:[1.0,0.0,0.0],hue:10>>;
    <variant name:"stop",weight:1,<sphere id:"tip",scale:0.4>>>>
let nodes = [<branch id:"first",color:"#cc744f">,<branch id:"second",position:[-2.0,0.0,0.0]>]
let a = s.expand(doc(nodes,resources,{max_depth:8}))^
let b = s.expand(doc(nodes,resources,{max_depth:8}))^
let reordered = s.expand(doc(reverse(nodes),resources,{max_depth:8}))^;
[a.parts == b.parts, map([for (p in a.parts) (p.id,p.key)]) == map([for (p in reordered.parts) (p.id,p.key)]),len(a.parts)>0,
 s.expand(doc(nodes,<resources <rule name:"branch",*reverse(content(content(resources)[0]))>>,{max_depth:8}))^.parts == a.parts];
let repeated = s.expand(doc([<repeat id:"row",count:3,step:{position:[1.0,0.0,0.0],hue:60},<box color:"#ff0000">>]))^
let independent = s.expand(doc([<box id:"copies",count:3,position:[1.0,0.0,0.0]>]))^
let prefix = s.expand(doc([<repeat id:"row",count:4,step:{position:[1.0,0.0,0.0],hue:60},<box color:"#ff0000">>]))^;
[[for (p in repeated.parts) p.matrix[12]] == [0.0,1.0,2.0], [for (p in independent.parts) p.matrix[12]] == [1.0,1.0,1.0],
 repeated.parts == slice(prefix.parts,0,3),len(unique(repeated.parts |> ~.key))==3,len(unique(repeated.parts |> ~.id))==3];
let hue_repeat = s.expand(<procart version:2,seed:0,color:"#ff0000",<resources>;
 <repeat count:3,step:{hue:60},<box>>>)^
let zero = s.expand(doc([<box>,<branch>],resources,{max_depth:0}))^
let tiny = s.expand(doc([<group scale:0.1,<box>>],<resources>,{min_size:0.2}))^;
[[for (p in hue_repeat.parts) p.color] == ["#ff0000","#ffff00","#00ff00"],zero.stats.objects==1,zero.stats.depth_retired==1,tiny.stats.objects==0,tiny.stats.size_retired==1];
[(s.expand(doc([<missing>])) ^ {null})==null,
 (s.expand(doc([<box id:"duplicate">,<sphere id:"duplicate">])) ^ {null})==null,
 (s.expand(doc([<repeat count:3,step:{seed:4},<box>>])) ^ {null})==null]
