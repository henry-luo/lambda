import nav: lambda.navigation

let floor_triangles=[[[0,0,0],[0,0,7],[7,0,7]],[[0,0,0],[7,0,7],[7,0,0]]];
let settings={low:[0,-1,0],high:[7,6,7],cell_size:1,cell_height:0.125,radius:0.25,height:1.5,max_step:0.375};
let baked=nav.bake(floor_triangles,settings);
let path=nav.path(baked,[1.25,0,1.25],[5.75,0,5.75])^;
let projected=nav.path(baked,[0.5,0,3],[5,0,3])^;
let limited=nav.path(baked,[1.25,0,1.25],[5.75,0,5.75],{max_waypoint_tests:0})^;
let unproven=nav.path(baked,[1.25,0,1.25],[5.75,0,5.75],{max_projection_tests:1})^;
let empty=nav.path(nav.bake([],settings),[0,0,0],[1,0,0])^;
let checks={
    valid:nav.validate(baked)^,
    complete:path.status=="complete" and path.domain=="voxel_cells" and path.profile.version==1 and path.profile.frame=="y_up" and path.profile.radius==0.25 and path.profile.height==1.5 and path.profile.low==settings.low and path.profile.high==settings.high and len(path.nodes)==9 and len(path.portals)==0,
    points:len(path.points)>2 and all([for (p in path.points) p[0]>=1 and p[0]<=6 and p[1]==0 and p[2]>=1 and p[2]<=6]),
    projected:projected.status=="complete" and abs(projected.start_distance-0.5)<1e-12 and projected.projected_start==[1.0,0.0,3.0],
    waypoints:limited.status=="budget_exhausted" and len(limited.points)==0 and len(limited.nodes)==0 and limited.projection_tests==50 and limited.waypoint_tests==0,
    projection:unproven.status=="budget_exhausted" and unproven.start_cell==null and unproven.projection_tests==1,
    empty:empty.status=="unreachable" and len(empty.points)==0 and empty.projection_tests==0,
    incompatible:[for (options in [{radius:0.3},{height:1.6},{max_step:0.25}]) nav.path(baked,[2,0,2],[4,0,4],options) ^ {true} ~ {false}]==fill(3,true),
    invalid:[for (invalid in [nav.bake(floor_triangles,{*:settings,cell_size:0}),nav.bake([[[0,0,0],[1,0,0],[2,0,0]]],settings),nav.bake(floor_triangles,{*:settings,max_work:2})]) nav.validate(invalid) ^ {true} ~ {false}]==fill(3,true)
};
[for (key,passed in checks where passed!=true) string(key)]
