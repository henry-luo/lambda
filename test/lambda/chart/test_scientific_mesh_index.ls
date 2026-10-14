import mesh: lambda.chart.mesh

// a spectrogram-sized grid exercises preparation and the resulting sampling index.
let xs=[for (i in 0 to 13) float(i)];
let ys=[for (i in 0 to 17) float(i)];
let grid={kind:"grid",x:xs,y:ys,vertex_fields:{value:[for (y in ys) [for (x in xs) x+2*y]]}};
let prepared=mesh.prepare(grid);
let samples=mesh.sample(prepared,[[0.5,0.5],[6.5,8.5],[12.5,16.5]],{fields:["value"]});

fn triangles(points,masked=[]) => {kind:"mesh",
    vertices:[for (triangle in points) for (p in triangle) {x:p[0],y:p[1]}],
    faces:[for (i,triangle in points) {id:i,vertices:[3*i,3*i+1,3*i+2],masked:contains(masked,i)}]}
let corners=[[[-10,-10],[-9,-10],[-10,-9]],[[10,10],[9,10],[10,9]]];
let outer=[[-4,-4],[4,-4],[0,4]];
let inner=[[-1,-1],[1,-1],[0,1]];
let nested=triangles([*corners,outer,inner]);
let crossing=triangles([*corners,[[-4,0],[4,0],[0,4]],[[-3,2],[3,2],[0,-2]]]);
let touching=triangles([*corners,outer,[[0,-4],[-2,-6],[2,-6]]]);
let separate=triangles([*corners,[[0,0],[4,0],[0,4]],[[4,4],[4,1],[1,4]]]);
let wide=mesh.prepare(triangles([for (y in 0 to 7) [[0,y],[10,y],[0,y+0.25]]]));
let masked=mesh.prepare(triangles([*corners,outer,inner],[3]));
let sheared=mesh.prepare({kind:"curvilinear",x:[for (y in 0 to 4) [for (x in 0 to 4) x+0.2*y]],
    y:[for (y in 0 to 4) [for (x in 0 to 4) float(y)]]});
let checks={
    grid:len(prepared.vertices)==252 and len(prepared.faces)==442 and len(prepared.edges)==693,
    samples:(samples |> ~.values.value) == [1.5,23.5,45.5],
    identities:(samples |> ~.cell_id) == [[0,0],[8,6],[16,12]],
    adjacency:all([for (i,neighbors in prepared.adjacency) all([for (j in neighbors) contains(prepared.adjacency[j],i)])]),
    nested:mesh.prepare(nested) is error,
    crossing:mesh.prepare(crossing) is error,
    winding:mesh.prepare({*:nested,faces:[for (f in nested.faces) {*:f,vertices:reverse(f.vertices)}]}) is error,
    touching:not (mesh.prepare(touching) is error),
    bbox_only:not (mesh.prepare(separate) is error),
    wide:len(wide.faces)==8,
    masked:not (masked is error) and masked.faces[3].masked,
    sheared:len(sheared.faces)==32
};
[for (label,passed in checks where passed!=true) string(label)]
