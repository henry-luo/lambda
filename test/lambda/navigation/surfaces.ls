import nav: lambda.navigation

let floor_mesh=nav.mesh([[0,0],[3,0],[3,1],[2,1],[2,3],[3,3],[0,1]],
    [[0,1,2],[0,2,3],[0,3,6],[3,2,5],[3,5,4]]);
let turn=nav.path(floor_mesh,[0.2,0.5],[2.5,2.8])^;
let projected=nav.path(floor_mesh,[-0.5,0.5],[0.5,0.5])^;
let too_far=nav.path(floor_mesh,[-0.5,0.5],[0.5,0.5],{max_projection_distance:0.1})^;
let projection_budget=nav.path(floor_mesh,[0.2,0.5],[2.5,2.8],{max_projection_tests:0})^;
let portal_budget=nav.path(floor_mesh,[0.2,0.5],[2.5,2.8],{max_portal_tests:0})^;
let islands=nav.mesh([[0,0],[1,0],[1,1],[0,1],[3,0],[4,0],[4,1],[3,1]],[[0,1,2],[0,2,3],[4,5,6],[4,6,7]]);
let partial=nav.path(islands,[0.25,0.25],[3.75,0.25],{partial:true})^;
let vertices3d=[[0,0,0],[0,0,1],[1,0,1],[1,0,0], [1,0,0],[1,0,1],[2,1,1],[2,1,0], [2,1,0],[2,1,1],[3,1,1],[3,1,0]];
let ramp=nav.mesh(vertices3d,[[0,1,2],[0,2,3],nav.triangle([4,5,6],{cost:2}),nav.triangle([4,6,7],{cost:2}),[8,9,10],[8,10,11]],{max_slope_degrees:50});
let grounded=nav.path(ramp,[0.25,0,0.5],[2.75,1,0.5])^;
let exact_slope=nav.mesh([[0,0,0],[0,0,1],[1,1,0]],[[0,1,2]]);
let boundary=nav.path(exact_slope,[0.25,0.25,0.25],[0.5,0.5,0.25])^;
let clockwise=nav.mesh([[0,0,0],[1,0,0],[0,0,1]],[[0,1,2]]);
let back=nav.path(clockwise,[0.25,0,0.25],[0.5,0,0.25])^;
let reverse=nav.mesh([[0,0,0],[1,0,0],[0,0,1]],[[0,1,2]],{two_sided:true});
let admitted=nav.path(reverse,[0.25,0,0.25],[0.5,0,0.25])^;
let masked=nav.mesh([[0,0],[1,0],[1,1],[0,1]],[nav.triangle([0,1,2],{groups:2}),nav.triangle([0,2,3],{groups:2})]);
let rejected=nav.path(masked,[0.25,0.25],[0.75,0.25],{filter:1})^;
let empty=nav.path(nav.mesh([],[],{dimensions:3}),[0,0,0],[1,0,0])^;
let checks={
    valid:nav.validate(floor_mesh)^ and nav.validate(ramp)^,
    turned:turn.status=="complete" and abs(turn.length-2*sqrt(1.8*1.8+0.5*0.5))<1e-9 and abs(turn.cost-turn.length)<1e-9,
    corridor:turn.corridor==turn.nodes and len(turn.portals)+1==len(turn.nodes) and len(turn.points)>=3 and turn.corridor_cost>=0,
    projected:projected.status=="complete" and abs(projected.start_distance-0.5)<1e-12 and abs(projected.projected_start[0])<1e-12 and abs(projected.length-0.5)<1e-12,
    distance_bound:too_far.status=="unreachable" and too_far.projected_start==null and len(too_far.points)==0 and too_far.cost==null,
    projection_work:projection_budget.status=="budget_exhausted" and projection_budget.projection_tests==0 and projection_budget.projected_start==null and not projection_budget.partial,
    portal_work:portal_budget.status=="budget_exhausted" and portal_budget.portal_tests==0 and len(portal_budget.points)==0 and len(portal_budget.nodes)==0,
    partial:partial.status=="partial" and partial.partial and abs(partial.projected_goal[0]-3.75)<1e-12 and abs(partial.path_end[0]-1.0)<1e-12 and abs(partial.points[len(partial.points)-1][0]-1.0)<1e-12,
    grounded:grounded.status=="complete" and abs(grounded.length-(1.5+sqrt(2.0)))<1e-12 and abs(grounded.cost-(1.5+2*sqrt(2.0)))<1e-12 and len(grounded.points)>=4,
    back_face:back.status=="unreachable" and admitted.status=="complete",
    inclusive_slope:boundary.status=="complete",
    filter:rejected.status=="unreachable" and rejected.start_polygon==null,
    empty:nav.validate(nav.mesh([],[]))^ and empty.status=="unreachable" and empty.projection_tests==0 and len(empty.points)==0,
    schema_errors:[for (invalid in [nav.mesh([[0,0],[1,0],[0,1]],[[0,1,3]]),
        nav.mesh([[0,0],[1,0],[0,1]],[[0,1,2],[2,0,1]]),
        nav.mesh([[0,0],[1,0],[0,1]],[[0,1,2]],{radius:0.2}),
        nav.mesh([[0,0],[1,0],[0,1]],[[0,1,2]],{max_slope_degrees:90})])
        nav.validate(invalid) ^ {true} ~ {false}] == fill(4,true),
    query_errors:[nav.path(floor_mesh,[0,0],[1,0],{radius:0.2}) ^ {true} ~ {false},
        nav.path(floor_mesh,[0,0,0],[1,0]) ^ {true} ~ {false}] == fill(2,true)
};
[for (key,passed in checks where passed!=true) string(key)]
