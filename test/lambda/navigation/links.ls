import nav: lambda.navigation

let vertices=[[0,0],[1,0],[1,1],[0,1],[3,0],[4,0],[4,1],[3,1]];
let triangles=[[0,1,2],[0,2,3],[4,5,6],[4,6,7]];
let bridge=nav.link("bridge",[0.75,0.5],[3.25,0.5],{kind:"jump",duration:0.4,max_projection_distance:0});
let source=nav.mesh(vertices,triangles,{links:[bridge]});
let route=nav.path(source,[0.25,0.25],[3.75,0.75])^;
let reverse=nav.path(source,[3.75,0.75],[0.25,0.25])^;
let back=nav.path(nav.mesh(vertices,triangles,{links:[{*:bridge,bidirectional:true}]}),[3.75,0.75],[0.25,0.25])^;
let filtered=nav.path(nav.mesh(vertices,triangles,{links:[{*:bridge,groups:2}]}),[0.25,0.25],[3.75,0.75],{filter:1})^;
let cheapest=nav.path(nav.mesh(vertices,triangles,{links:[{*:bridge,cost:100},{*:bridge,id:"shortcut",cost:0}]}),[0.25,0.25],[3.75,0.75])^;
let limited=nav.path(source,[0.25,0.25],[3.75,0.75],{max_portal_tests:0})^;
let floor=[[[0,0,0],[0,0,7],[3,0,7]],[[0,0,0],[3,0,7],[3,0,0]],[[4,0,0],[4,0,7],[7,0,7]],[[4,0,0],[7,0,7],[7,0,0]]];
let settings={low:[0,-1,0],high:[7,6,7],cell_size:1,cell_height:0.125,radius:0.25,height:1.5,max_step:0.375,representation:'polygons'};
let jump=nav.link("gap",[1.5,0,2],[5.5,0,2],{kind:"jump",height:1.4,width:0.4});
let baked=nav.bake(floor,settings,{links:[jump]});
let narrow=nav.path(baked,jump.start,jump.end,{height:1.3})^;
let low=nav.path(baked,jump.start,jump.end,{radius:0.1})^;
let crossing=nav.path(baked,jump.start,jump.end,{radius:0.1,height:1.3})^;
let cell_crossing=nav.path(nav.bake(floor,{*:settings,representation:'cells'},{links:[jump]}),jump.start,jump.end,{radius:0.1,height:1.3})^;
let cells=[for (z in 1 to 5, x in [1,5]) {x:x,z:z,levels:[0,6],cost:1,groups:4294967295}];
let stored={type:'bake',version:1,format:'cells',frame:'y_up',settings:crossing.profile,settings_hash:crossing.settings_hash,
    source_hash:"0000000000000000000000000000000000000000000000000000000000000000",cells:cells,links:[jump]};
let reopened=nav.path(stored,jump.start,jump.end,{radius:0.1,height:1.3})^;
let partial=nav.path(nav.bake(floor,settings,{links:[{*:jump,disabled:true}]}),jump.start,jump.end,{partial:true})^;
let checks={
    valid:nav.validate(source)^,
    forward:route.status=="complete" and len(route.links)==1 and route.links[0].id=="bridge" and route.links[0].kind=="jump" and route.links[0].duration==0.4 and route.links[0].reverse==false,
    endpoints:len(route.points)==4 and route.links[0].start_point==1 and route.links[0].end_point==2 and route.points[1]==bridge.start and route.points[2]==bridge.end and all([for (id in route.corridor) id>=0 and id<4]),
    costs:abs(route.cost-(2.5+2*sqrt(0.3125)))<1e-12 and cheapest.links[0].id=="shortcut" and cheapest.links[0].cost==0,
    directions:reverse.status=="unreachable" and len(reverse.links)==0 and back.status=="complete" and back.links[0].reverse==true and back.links[0].start==bridge.end,
    filters:filtered.status=="unreachable" and narrow.status=="unreachable" and low.status=="unreachable" and crossing.status=="complete" and crossing.links[0].id=="gap",
    archive:reopened.status=="complete" and reopened.points==crossing.points and reopened.links==crossing.links and reopened.cost==crossing.cost,
    cell_route:cell_crossing.status=="complete" and cell_crossing.links==crossing.links and cell_crossing.cost==crossing.cost and cell_crossing.points==crossing.points,
    partial:partial.status=="partial" and len(partial.links)==0 and partial.path_end==[2.0,0.0,2.0],
    budget:limited.status=="budget_exhausted" and len(limited.links)==0 and len(limited.points)==0,
    invalid:[for (bad in [nav.mesh(vertices,triangles,{links:[bridge,bridge]}),nav.mesh(vertices,triangles,{links:[bridge],max_attachment_tests:0}),
        nav.mesh(vertices,triangles,{links:[{*:bridge,width:-1}]}),nav.mesh(vertices,triangles,{links:[{*:bridge,start_polygon:99}]}),nav.grid([2,2],{links:[bridge]})])
        nav.validate(bad) ^ {true} ~ {false}]==fill(5,true)
};
[for (key,passed in checks where passed!=true) string(key)]
