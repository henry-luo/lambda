import nav: lambda.navigation

let nodes=[nav.node("start",[0.0,0.0]),nav.node("near",[1.0,0.0]),nav.node("detour",[1000.0,0.0]),nav.node("goal",[2.0,0.0])];
let edges=[nav.edge("start","near",1.0),nav.edge("near","goal",20.0),nav.edge("start","detour",0.1),nav.edge("detour","goal",0.0)];
let graph=nav.graph(nodes,edges);
let route=nav.path(graph,"start","goal")^;
let zero=nav.path(graph,"start","goal",{heuristic:'zero'})^;
let budget=nav.path(graph,"start","goal",{max_expansions:1,partial:true})^;
let filtered=nav.graph([nav.node("s",[0,0]),nav.node("middle",[1,0]),nav.node("g",[2,0])],[nav.edge("s","middle",1.0),nav.edge("middle","g",1.0,{groups:2})]);
let missing=nav.path(filtered,"s","g",{filter:1})^;
let partial=nav.path(filtered,"s","g",{filter:1,partial:true})^;
let grid=nav.grid([5,5],{blocked:[[2,0],[2,1],[2,2],[2,3]]});
let around=nav.path(grid,[0,0],[4,0])^;
let corner=nav.path(nav.grid([2,2],{diagonals:true,blocked:[[1,0],[0,1]]}),[0,0],[1,1])^;
let filtered_corner=nav.path(nav.grid([2,2],{diagonals:true,groups:[1,2,1,1]}),[0,0],[1,1],{filter:1})^;
let space=nav.path(nav.grid([2,2,2],{diagonals:true,origin:[4,5,6]}),[0,0,0],[1,1,1])^;
let checks={
    valid:nav.validate(graph)^ and nav.validate(grid)^,
    admissible:route.status=="complete" and route.nodes==["start","detour","goal"] and abs(route.cost-0.1)<1e-12,
    points:route.points==[[0.0,0.0],[1000.0,0.0],[2.0,0.0]],
    zero:zero.nodes==route.nodes and zero.cost==route.cost,
    budget:budget.status=="budget_exhausted" and budget.expanded==1 and budget.partial,
    unreachable:missing.status=="unreachable" and len(missing.nodes)==0 and missing.cost==null and not missing.partial,
    partial:partial.status=="partial" and partial.nodes==["s","middle"] and partial.cost==1.0,
    around:around.status=="complete" and around.cost==12.0 and len(around.nodes)==13 and around.nodes[0]==[0,0] and around.nodes[12]==[4,0],
    weighted_grid:nav.path(nav.grid([3,2],{costs:[1,50,1,1,1,1]}),[0,0],[2,0])^.cost==4.0,
    map_source:nav.path({type:'graph',nodes:nodes,edges:edges},"start","goal")^.nodes==route.nodes,
    corner:corner.status=="unreachable",
    filter_clearance:filtered_corner.cost==2.0,
    space:space.status=="complete" and abs(space.cost-sqrt(3.0))<1e-12 and space.points==[[4.0,5.0,6.0],[5.0,6.0,7.0]],
    zero_cost:nav.path(nav.grid([2,2],{costs:[0.0,0.0,0.0,0.0]}),[0,0],[1,1])^.cost==0.0,
    reverse:nav.path(nav.graph([nav.node("a",[0,0]),nav.node("b",[1,0])],[nav.edge("a","b",2.5,{bidirectional:true})]),"b","a")^.cost==2.5,
    quota:nav.path(graph,"start","goal",{max_expansions:0})^.status=="budget_exhausted",
    already_there:nav.path(graph,"start","start",{max_expansions:0,max_edge_tests:0})^.status=="complete",
    edge_quota:nav.path(graph,"start","goal",{max_edge_tests:0})^.edge_tests==0,
    no_mutation:route==nav.path(graph,"start","goal")^,
    errors:[
      nav.validate(nav.graph([nav.node("a",[0,0]),nav.node("a",[1,0])],[])) ^ { true } ~ { false },
      nav.validate(nav.graph([nav.node("a",[0,0])],[nav.edge("a","missing")])) ^ { true } ~ { false },
      nav.validate(nav.graph(nodes,[nav.edge("start","goal",-1.0)])) ^ { true } ~ { false },
      nav.validate(nav.graph(nodes,edges,{version:2})) ^ { true } ~ { false },
      nav.validate(nav.grid([0,2])) ^ { true } ~ { false },
      nav.validate(nav.grid([65536,2])) ^ { true } ~ { false },
      nav.validate(nav.grid([2,2],{blocked:[[2,0]]})) ^ { true } ~ { false },
      nav.validate(nav.grid([2,2],{costs:[1.0]})) ^ { true } ~ { false },
      nav.validate(nav.grid([2,2],{cell_size:[0.0,1.0]})) ^ { true } ~ { false },
      nav.path(graph,"start","missing") ^ { true } ~ { false },
      nav.path(grid,[0,0],[5,0]) ^ { true } ~ { false },
      nav.path(graph,"start","goal",{heuristic:7}) ^ { true } ~ { false },
      nav.path(graph,"start","goal",{partial:1}) ^ { true } ~ { false },
      nav.path(graph,"start","goal",{max_expansions:-1}) ^ { true } ~ { false }
    ] == fill(14,true)
};
[for (key,passed in checks where passed!=true) string(key)]
