import nav: lambda.navigation

let settings={low:[0,-1,0],high:[4,4,4],cell_size:1,cell_height:0.125,radius:0.25,height:1.5,max_step:0.375,max_slope_degrees:27.4};
let triangles=[[[0,0,0],[0,0,4],[4,0,4]],[[0,0,0],[4,0,4],[4,0,0]]];
let raw=nav.path(nav.bake(triangles,settings),[1.25,0,1.25],[2.75,0,2.75])^;
let cells=[for (z in [1,2], x in [1,2]) {x:x,z:z,levels:[0,4],cost:1,groups:4294967295}];
let source_hash="0000000000000000000000000000000000000000000000000000000000000000";
let stored={type:'bake',version:1,format:'cells',frame:'y_up',settings:raw.profile,settings_hash:raw.settings_hash,source_hash:source_hash,cells:cells};
let path=nav.path(stored,[1.25,0,1.25],[2.75,0,2.75])^;
let empty=nav.path({*:stored,cells:[]},[1,0,1],[3,0,3])^;
let checks={
    valid:nav.validate(stored)^,
    route:path.status=="complete" and path.points==raw.points and path.cost==raw.cost and path.length==raw.length,
    metadata:path.source_hash==source_hash and path.settings_hash==raw.settings_hash and path.profile.max_slope_radians==raw.profile.max_slope_radians,
    empty:empty.status=="unreachable" and empty.projection_tests==0 and len(empty.points)==0,
    errors:[for (invalid in [{*:stored,frame:'z_up'},{*:stored,settings_hash:source_hash},{*:stored,source_hash:"bad"},
        {*:stored,cells:[cells[0],cells[0]]},{*:stored,cells:[{*:cells[0],levels:[0,1]}]},{*:stored,cells:[{*:cells[0],groups:0}]}])
        nav.validate(invalid) ^ {true} ~ {false}]==fill(6,true)
};
[for (key,passed in checks where passed!=true) string(key)]
