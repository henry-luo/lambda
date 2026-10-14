import chart: lambda.chart.chart
import plot: lambda.chart.plot3d
import camera: lambda.scene3d.camera
fn near(a,b) => abs(a-b)<1e-9
fn elements(node) => if (node is element) [node,for (child in content(node)) for (item in elements(child)) item] else []
let spec={width:320,height:240,coordinate:{type:"cartesian3d",camera:{position:[0,0,4],target:[0,0,0],fov:90}},
    data:[{id:"center",x:0,y:0,z:0},{id:"corner",x:1,y:1,z:1}],mark:{kind:"point"}};
let prepared=plot.prepare(spec);
let cam=camera.prepare({position:[0,0,4],target:[0,0,0],fov:90},320,240);
let ortho=camera.prepare({type:"orthographic",position:[0,0,4],target:[0,0,0],view_height:4},320,240);
let grid={kind:"grid",x:[0,1],y:[0,1],vertex_fields:{height:[[0,1],[1,0]]}};
let surface={*:spec,data:grid,mark:{kind:"surface"},encoding:{z:{field:"height",dtype:"quantitative"},color:{field:"height",dtype:"quantitative"}}};
let triangle_spec={*:spec,data:{kind:"mesh",vertices:[{x:-1,y:0,z:-1,value:0},{x:1,y:0,z:-1,value:0},{x:0,y:0,z:1,value:2}],faces:[{id:"face",vertices:[0,1,2]}]},mark:{kind:"mesh"}};
let triangles=plot.prepare(triangle_spec);
let checks={
    point:len(prepared.faces)==32,
    figure:len(elements(chart.figure(spec)) |: name(~)=='scene3d')==1,
    projection:near(camera.project(cam,[0,0,0]).x,160) and near(camera.project(cam,[0,0,0]).y,120) and near(camera.project(cam,[1,0,0]).x,190),
    orthographic:near(camera.project(ortho,[1,0,0]).x,camera.project(ortho,[1,0,-20]).x),
    clip:len(camera.clip_segment(cam,[0,0,5],[0,0,0]))==2 and camera.project(cam,[0,0,5]).x==null,
    surface:len(plot.prepare(surface).faces)==2 and chart.figure(surface) is element,
    wireframe:len(plot.prepare({*:surface,mark:{kind:"wireframe"}}).faces |: not ~.depth_only)==8,
    mesh:len(triangles.faces)==1,
    pick:plot.pick(triangles,160,120).source_id=="face",
    pick_value:near(plot.pick(triangles,160,120).values.value,1),
    line:len(plot.prepare({*:spec,mark:{kind:"line"}}).faces)==2,
    vector:len(plot.prepare({*:spec,data:[{x:0,y:0,z:0,u:1,v:0,w:1}],mark:{kind:"vector"}}).faces)>0,
    bars:len(plot.prepare({*:spec,data:[{x:0,y:0,z:1,x2:1,y2:1}],mark:{kind:"bar"}}).faces)==12,
    voxels:len(plot.prepare({*:spec,data:{kind:"voxels",x:[0,1,2],y:[0,1],z:[0,1],occupied:[[[true,true]]]},mark:{kind:"voxel"}}).faces)==20,
    svg_contract:chart.render_spec(spec) is error,
    opaque:chart.figure({*:spec,mark:{kind:"point",opacity:0.5}}) is error,
    camera_error:chart.figure({*:spec,coordinate:{type:"cartesian3d",camera:{near:1,far:0.1}}}) is error,
    two_d:chart.figure({width:300,height:200,data:[{x:1,y:2}],mark:{kind:"point"},encoding:{x:{field:"x",dtype:"quantitative"},y:{field:"y",dtype:"quantitative"}}}) is element
    ,band_bars:len(plot.prepare({*:spec,data:[{x:"a",y:"b",z:1}],mark:{kind:"bar"},encoding:{x:{field:"x",dtype:"nominal"},y:{field:"y",dtype:"nominal"}}}).faces)==12
    ,retarget:plot.retarget(prepared,camera.control(prepared.camera,{type:"orbit",yaw:0.2})).primitives==prepared.primitives
    ,ortho_zoom:near(camera.control(ortho,{type:"zoom",factor:2}).view_height,8)
    ,bad_color:chart.figure({*:spec,mark:{kind:"point",color:"not-a-color"}}) is error
    ,budget:plot.prepare({*:spec,max_triangles:1}) is error
    ,line_identity:(plot.prepare({*:spec,mark:{kind:"line"},data:[{x:0,y:0,z:0,g:"a"},{x:1,y:0,z:0,g:"b"},{x:0,y:1,z:0,g:"a"}],encoding:{detail:{field:"g"}}}).primitives |> ~.id)==[[0,2]]
    ,mixed:len(elements(chart.figure({concat:"horizontal",children:[spec,{width:120,height:100,data:[{x:1,y:1}],mark:{kind:"point"}}]})) |: name(~)=='scene3d')==1
    ,frame:chart.figure_frame(spec,250) is element
};
[for (label,passed in checks where passed!=true) string(label)]
