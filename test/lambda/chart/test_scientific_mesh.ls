import mesh: lambda.chart.mesh
let grid={kind:"grid",x:[0,2,3],y:[0,1],vertex_fields:{value:[[0,2,3],[2,4,5]]},cell_fields:{heat:[[10,20]]}};
let prepared=mesh.prepare(grid);
let sampled=mesh.sample(prepared,[[1,0.25],[2.5,0.5],[4,0]],{fields:["value"]});
let points=[{id:"a",x:0,y:0,value:0},{id:"b",x:1,y:0,value:1},{id:"c",x:1,y:1,value:3},{id:"d",x:0,y:1,value:2},{id:"e",x:0.4,y:0.3,value:1}];
let triangulated=mesh.triangulate(points);
let checks={
    grid:len(prepared.vertices)==6 and len(prepared.faces)==4 and len(prepared.edges)==9,
    diagonal:prepared.faces[0].vertices==[0,1,4] and prepared.faces[1].vertices==[0,4,3],
    interpolation:abs(sampled[0].values.value-1.5)<1e-12 and abs(sampled[1].values.value-3.5)<1e-12,
    outside:sampled[2]==null,
    cell:mesh.sample(prepared,[[2.5,0.5]],{association:"cell",fields:["heat"]})[0].values.heat==20,
    identities:prepared.faces[0].id==[[0,0],0] and sampled[0].cell_id==[0,0],
    delaunay:len(triangulated.faces)==4 and all([for (f in triangulated.faces,
        let p=[for (i in f.vertices) mesh.point(triangulated.vertices[i])])
        all([for (i,v in triangulated.vertices where not contains(f.vertices,i)) math.incircle(p[0],p[1],p[2],mesh.point(v))<=0])]),
    cocircular:len(mesh.triangulate(slice(points,0,4)).faces)==2,
    reversed:mesh.sample({kind:"mesh",vertices:slice(points,0,3),faces:[{id:"reverse",vertices:[2,1,0]}]},[[0.75,0.5]],{fields:["value"]})[0].face_id=="reverse",
    mask:mesh.sample({*:grid,cell_mask:[[true,false]]},[[1,0.5]],{fields:["value"]})==[null],
    scalar_mask:mesh.sample({*:grid,vertex_fields:{value:[[0,null,3],[2,4,5]]}},[[1,0.25]],{fields:["value"]})==[null],
    empty:len(mesh.prepare({kind:"grid",x:[],y:[]}).faces)==0,
    bad_coordinate:mesh.prepare({*:grid,x:[0,0,3]}) is error,
    bad_shape:mesh.prepare({*:grid,vertex_fields:{value:[[0]]}}) is error,
    bad_mask:mesh.prepare({*:grid,cell_mask:[[1,false]]}) is error,
    bad_index:mesh.prepare({kind:"mesh",vertices:points,faces:[{vertices:[0,1,99]}]}) is error,
    duplicate:mesh.triangulate([*points,points[0]]) is error,
    collinear:mesh.triangulate([{x:0,y:0},{x:1,y:1},{x:2,y:2}]) is error,
    overlap:mesh.prepare({kind:"mesh",vertices:points,faces:[{vertices:[0,1,2]},{vertices:[0,1,4]}]}) is error,
    limit:mesh.prepare(grid,{max_vertices:5}) is error,
    invalid_empty:mesh.prepare({kind:"grid",x:[],y:[]},{max_faces:0}) is error
    ,curvilinear:abs(mesh.sample({kind:"curvilinear",x:[[0,2],[0.5,2.5]],y:[[0,0],[1,1]],vertex_fields:{v:[[0,2],[2.5,4.5]]}},[[1,0.5]],{fields:["v"]})[0].values.v-2)<1e-12
    ,concave:mesh.prepare({kind:"curvilinear",x:[[0,2],[0,0.5]],y:[[0,0],[2,0.5]]}) is error
    ,stable_delaunay:sort(mesh.triangulate(reverse(points)).faces |> ~.id)==sort(triangulated.faces |> ~.id)
    ,collapsed:mesh.prepare({kind:"mesh",vertices:[{x:0,y:0},{x:1,y:0},{x:0,y:1},{x:0,y:0}],faces:[{vertices:[0,1,2]}]}) is error
    ,malformed_prepared:mesh.prepare({kind:"prepared_mesh",vertices:[],faces:[]}) is error
};
[for (label,passed in checks where passed!=true) string(label)]
