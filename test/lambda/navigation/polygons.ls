import nav: lambda.navigation

let floor_triangles=[[[0,0,0],[0,0,7],[7,0,7]],[[0,0,0],[7,0,7],[7,0,0]]];
let settings={low:[0,-1,0],high:[7,6,7],cell_size:1,cell_height:0.125,radius:0.25,height:1.5,max_step:0.375,representation:'polygons'};
let baked=nav.bake(floor_triangles,settings);
let direct=nav.path(baked,[1.25,0,1.25],[5.75,0,5.75])^;
let legacy=nav.path(nav.bake(floor_triangles,{*:settings,representation:'cells'}),[1.25,0,1.25],[5.75,0,5.75])^;
let limited=nav.path(baked,[1.25,0,1.25],[5.75,0,5.75],{max_portal_tests:0})^;
let cells=[for (z in 1 to 5, x in 1 to 5) {x:x,z:z,levels:[0,6],cost:1,groups:4294967295}];
let stored={type:'bake',version:1,format:'cells',frame:'y_up',settings:direct.profile,settings_hash:direct.settings_hash,
    source_hash:"0000000000000000000000000000000000000000000000000000000000000000",cells:cells};
let reopened=nav.path(stored,[1.25,0,1.25],[5.75,0,5.75])^;
let corner_triangles=[[[0,0,0],[0,0,2],[7,0,2]],[[0,0,0],[7,0,2],[7,0,0]],
    [[0,0,2],[0,0,7],[2,0,7]],[[0,0,2],[2,0,7],[2,0,2]]];
let corner=nav.path(nav.bake(corner_triangles,{*:settings,radius:0}),[0.5,0,6.5],[6.5,0,0.5])^;
let empty=nav.path(nav.bake([],{*:settings}),[0,0,0],[1,0,1])^;
let checks={
    profile:direct.status=="complete" and direct.domain=="voxel_polygons" and direct.profile.representation=="polygons" and direct.region_count==1 and direct.nodes==[0],
    direct:direct.points==[[1.25,0.0,1.25],[5.75,0.0,5.75]] and abs(direct.length-sqrt(40.5))<1e-12 and direct.cost==direct.length and direct.projection_tests==2,
    distinct:legacy.domain=="voxel_cells" and len(legacy.nodes)>1 and legacy.settings_hash!=direct.settings_hash,
    archive:reopened.points==direct.points and reopened.length==direct.length and reopened.settings_hash==direct.settings_hash and reopened.region_count==1,
    corner:corner.status=="complete" and corner.region_count==2 and len(corner.portals)==1 and len(corner.points)==3 and abs(corner.length-2*sqrt(22.5))<1e-12,
    budget:limited.status=="budget_exhausted" and len(limited.nodes)==0 and len(limited.points)==0 and limited.portal_tests==0,
    empty:empty.status=="unreachable" and empty.region_count==0 and len(empty.points)==0,
    invalid:[for (source in [nav.bake(floor_triangles,{*:settings,representation:'unknown'}),
        {*:stored,settings:{*:direct.profile,representation:'cells'}}]) nav.validate(source) ^ {true} ~ {false}]==fill(2,true)
};
[for (key,passed in checks where passed!=true) string(key)]
