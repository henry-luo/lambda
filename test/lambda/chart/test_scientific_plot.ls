import chart: lambda.chart.chart
import mesh: lambda.chart.mesh
import contour: lambda.chart.mesh_contour
fn elements(node) => if (node is element) [node,for (child in content(node)) for (v in elements(child)) v] else []
fn paths(node,label) => elements(node) |: ~.class==label
let data={kind:"grid",x:[0,1,2],y:[0,1],vertex_fields:{value:[[0,1,2],[0,1,2]]}};
let spec={width:320,height:220,data:data,encoding:{color:{field:"value",dtype:"quantitative"}}};
let prepared=mesh.prepare(data);
let lines=contour.contours(prepared,"value",[0.5,1.0,1.5]);
let constant=contour.contours(mesh.prepare({*:data,vertex_fields:{value:[[1,1,1],[1,1,1]]}}),"value",[1]);
let checks={
    mesh:len(paths(chart.render_spec({*:spec,mark:{kind:"mesh"}}),"mesh-fragment"))==4,
    edges:len(paths(chart.render_spec({*:spec,mark:{kind:"wireframe"}}),"wireframe-fragment"))==7,
    lines:len(lines)==3 and all([for (line in lines) all([for (p in line.points) abs(p[0]-line.level)<1e-12])]),
    constant:len(constant)==1 and constant[0].closed and len(constant[0].points)==7,
    contour_svg:len(paths(chart.render_spec({*:spec,mark:{kind:"contour",levels:[0.5,1,1.5]}}),"contour-fragment"))==3,
    bands:len(paths(chart.render_spec({*:spec,mark:{kind:"contour_band",levels:[0,1,2]}}),"contour_band-fragment"))==4,
    filter:chart.render_spec({*:spec,mark:{kind:"mesh"},transform:[{type:"filter",field:"value",gt:0}]}) is error,
    bad_levels:contour.contours(prepared,"value",[1,0]) is error,
    fragment_limit:contour.contours(prepared,"value",[0,1,2],{max_fragments:1}) is error,
    native:chart.render(<chart width:320,height:220,<data values:data>;<mark type:"mesh">;<encoding <color field:"value",dtype:"quantitative">>>) is element
};
[for (label,passed in checks where passed!=true) string(label)]
