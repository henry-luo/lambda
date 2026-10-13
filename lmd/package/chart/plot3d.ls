// Scientific data becomes ordinary scene geometry; Radiant owns visibility and depth (D7.5.3).
import scene: lambda.scene3d
import camera: lambda.scene3d.camera
import rgba: lambda.slide.color
import mesh: .mesh
import parse: .parse
import dataflow: .dataflow
import scale: .scale
import renderer: .mark_renderer
import mark: .mark
import records: .records
import util: .util
import svg: .svg
import text: .text
import cfg: .config
import legend: .legend
import sizing: .sizing
import collision: .collision
import dom

pub fn enabled(spec) => spec.coordinate.type=="cartesian3d" or spec.coordinate=="cartesian3d"
fn option(options,key,fallback) => if (options[key]!=null) options[key] else fallback
fn identity(row,index) => if (row.id!=null) row.id else index
fn point(row,encoding) => [for (axis in ["x","y","z"]) parse.channel_value(encoding[axis],row,row[axis])]
fn encoded(spec) => {*:spec,encoding:{*:parse.attributes(spec.encoding),*:map([for (axis in ["x","y","z"])
    for (part in [axis,if (spec.encoding[axis]!=null) spec.encoding[axis] else {field:axis,dtype:"quantitative",zero:false}]) part])}}
fn triangle(id,row,points) => {id:id,row:row,points:points,kind:"triangle"}
fn quad_triangles(id,row,points) => [for (t in [[0,1,2],[0,2,3]]) triangle(id,row,[for (i in t) points[i]])]
fn box_corners(lo,hi) => [for (z in 0 to 1) for (y in 0 to 1) for (x in 0 to 1)
    [if (x==0) lo[0] else hi[0],if (y==0) lo[1] else hi[1],if (z==0) lo[2] else hi[2]]]
let box_sides=[[0,4,6,2],[1,3,7,5],[0,1,5,4],[2,6,7,3],[0,2,3,1],[4,5,7,6]];
fn box(id,row,lo,hi,sides=[0,1,2,3,4,5]) => (let vertices=box_corners(lo,hi),
    [for (side in sides) for (t in quad_triangles(id,row,[for (i in box_sides[side]) vertices[i]])) t])

fn topology(spec) {
    let options=spec.mark;
    let prepared=mesh.prepare(spec.data,{*:options,dimension:if (spec.data.kind=="mesh") 3 else 2});
    let field=spec.encoding.color.field;
    let association=option(options,"association","vertex");
    let z=spec.encoding.z.field;
    let fields=[if (field!=null and association=="vertex") field,if (spec.data.kind!="mesh") z] |: ~!=null;
    let vertices=prepared.vertices;
    let points=[for (v in vertices) point(v,spec.encoding)];
    let valid_faces=prepared.faces |: mesh.face_active(prepared,~,fields) and
        (association=="vertex" or field==null or mesh.finite(~[field])) and
        all([for (i in ~.vertices) all(points[i] |> mesh.finite(~))]);
    if (prepared is error) prepared
    else if (not contains(["vertex","face","cell"],association)) error("chart.figure: invalid mesh association")
    else if (field!=null and not contains(if (association=="vertex") prepared.vertex_fields else prepared.face_fields,field)) error("chart.figure: unknown scalar field")
    else if (options.kind=="surface" and not contains(["grid","curvilinear"],prepared.source_kind)) error("chart.figure: surface requires grid topology")
    else if (options.shading!=null and options.shading!="flat") error("chart.figure: surface shading currently requires flat scalar colors")
    else if (options.kind=="wireframe") [for (face in valid_faces where options.xray!=true)
        {*:triangle(face.id,face,[for (i in face.vertices) points[i]]),depth_only:true},for (edge in prepared.edges,
        let active=[for (use in edge.uses where contains(valid_faces |> ~.id,prepared.faces[use.face].id)) use],
        let diagonal=len(edge.uses)==2 and prepared.faces[edge.uses[0].face].cell_id!=null and
            prepared.faces[edge.uses[0].face].cell_id==prepared.faces[edge.uses[1].face].cell_id
        where len(active)>0 and (options.diagonals==true or not diagonal))
        {id:edge.vertices,kind:"segment",points:[for (i in edge.vertices) points[i]],row:{*:prepared.faces[active[0].face],
            *:if (field!=null and association=="vertex") map([field,avg([for (i in edge.vertices) float(vertices[i][field])])]) else {}}}]
    else [for (face in valid_faces,
        let row={*:face,*:if (field!=null and association=="vertex") map([field,avg([for (i in face.vertices) float(vertices[i][field])])]) else {}})
        {*:triangle(face.id,row,[for (i in face.vertices) points[i]]),samples:[for (i in face.vertices) vertices[i]],fields:prepared.vertex_fields}]
}

fn voxels(spec) {
    let data=spec.data;
    let axes=[data.x,data.y,data.z];
    let dimensions=[for (a in axes) len(a)-1];
    let count=dimensions[0]*dimensions[1]*dimensions[2];
    let maximum=option(spec.mark,"max_voxels",100000);
    let shape=data.occupied is array and len(data.occupied)==dimensions[2] and
        all([for (plane in data.occupied) plane is array and len(plane)==dimensions[1] and
            all([for (row in plane) row is array and len(row)==dimensions[0] and all(row |> ~ is bool)])]);
    if (data.kind!="voxels" or not mesh.positive_int(maximum) or count>maximum or not shape or
        not all([for (axis in axes) axis is array and len(axis)>=2 and all([for (i,v in axis) mesh.finite(v) and (i==0 or v>axis[i-1])])]))
        error("chart.figure: invalid voxel edges, occupancy, or limit")
    else if (not all([for (key,values in data.cell_fields) values is array and len(values)==dimensions[2] and
        all([for (plane in values) plane is array and len(plane)==dimensions[1] and all([for (row in plane) row is array and len(row)==dimensions[0]])])]))
        error("chart.figure: invalid voxel field shape")
    else [for (k,plane in data.occupied) for (j,row in plane) for (i,occupied in row where occupied,
        let id=[k,j,i],let value={id:id,*:map([for (key,values in data.cell_fields) for (part in [string(key),values[k][j][i]]) part])},
        let neighbors=[[i-1,j,k],[i+1,j,k],[i,j-1,k],[i,j+1,k],[i,j,k-1],[i,j,k+1]],
        let sides=[for (s,n in neighbors where n[0]<0 or n[1]<0 or n[2]<0 or n[0]>=dimensions[0] or n[1]>=dimensions[1] or n[2]>=dimensions[2] or not data.occupied[n[2]][n[1]][n[0]]) s])
        for (t in box(id,value,[data.x[i],data.y[j],data.z[k]],[data.x[i+1],data.y[j+1],data.z[k+1]],sides)) t]
}

fn record_primitives(spec) {
    let kind=spec.mark.kind;
    let data=spec.data;
    let components=option(spec.mark,"components",["u","v","w"]);
    let group_fields=[spec.encoding.detail.field,spec.encoding.color.field] |: ~!=null;
    let keyed=[for (i,row in data) {id:identity(row,i),row:row,point:point(row,spec.encoding),group:records.key(row,group_fields)}];
    if (not (data is array)) error("chart.figure: record mark requires an array")
    else if (kind=="vector" and (not (components is array) or len(components)!=3)) error("chart.figure: vector requires three component fields")
    else if (kind=="line") [for (group in records.group_by(keyed,["group"]),
        let rows=if (spec.encoding.order.field!=null) sort(group.rows,(value)=>value.row[spec.encoding.order.field]) else group.rows)
        for (i in 1 to (len(rows)-1),let a=rows[i-1].point,let b=rows[i].point
            where all([*a,*b] |> mesh.finite(~))) {id:[rows[i-1].id,rows[i].id],row:rows[i].row,kind:"segment",points:[a,b]}]
    else [for (v in keyed where all([for (i,value in v.point) mesh.finite(value) or (kind=="bar" and i<2 and
        contains(["nominal","ordinal"],spec.encoding[["x","y"][i]].dtype) and (value is string or value is symbol))]))
        if (kind=="bar") {id:v.id,row:v.row,kind:"box",points:[v.point,[for (axis in ["x","y","z"])
            parse.channel_value(spec.encoding[axis++"2"],v.row,if (axis=="z") 0 else v.row[axis++"2"])]]}
        else if (kind=="vector") {id:v.id,row:v.row,kind:"vector",points:[v.point,
            [for (i,field in components) v.point[i]+v.row[field]*option(spec.mark,"length_scale",1.0)]]}
        else {id:v.id,row:v.row,kind:"point",points:[v.point]}]
}

fn leaves(spec,parent=null) {
    let parsed=if (spec is element) parse.parse_top(spec) else spec;
    let resolved=dataflow.resolve(parsed,parent,null);
    let current=encoded({*:parse.attributes(parent),*:parsed,*:resolved,encoding:{*:parse.attributes(parent.encoding),*:parse.attributes(parsed.encoding)},
        coordinate:if (parsed.coordinate!=null) parsed.coordinate else parent.coordinate});
    if (resolved.data is error) resolved.data
    else if (parsed.layer!=null) [for (child in parsed.layer) for (leaf in leaves(child,current)) leaf]
    else if (not contains(["point","line","bar","vector","surface","wireframe","mesh","voxel"],current.mark.kind)) error("chart.figure: unsupported 3D mark")
    else if (current.mark.opacity!=null and current.mark.opacity!=1 or current.encoding.opacity!=null) error("chart.figure: scientific 3D geometry must be opaque")
    else if (current.mark.lighting!=null and not contains(["none","lambert"],current.mark.lighting)) error("chart.figure: unsupported lighting")
    else [ {*:current,primitives:if (current.mark.kind=="voxel") voxels(current)
        else if (contains(["surface","mesh","wireframe"],current.mark.kind)) topology(current) else record_primitives(current)} ]
}

fn scaled(p,mappings,ratios) => (let q=[for (i,v in p) scale.scale_apply(mappings[i],v)*ratios[i]],[q[0],q[2],0-q[1]])
fn primitive_points(primitive,mappings,ratios) {
    if (primitive.kind!="box") [for (p in primitive.points) scaled(p,mappings,ratios)] else {
        let lower=[for (i,v in primitive.points[0]) scale.scale_apply(mappings[i],v)];
        let upper=[for (i,v in primitive.points[1]) if (v==null and mappings[i].kind=="band") lower[i]+mappings[i].bandwidth else scale.scale_apply(mappings[i],v)];
        [for (p in [lower,upper]) [p[0]*ratios[0],p[2]*ratios[2],0-p[1]*ratios[1]]]
    }
}
fn triangles(primitive,mappings,ratios,cam,context,options) {
    let points=primitive_points(primitive,mappings,ratios);
    let width=mark.appearance(context,"size",primitive.row,option(options,"stroke_width",2.0));
    let size=mark.appearance(context,"size",primitive.row,option(options,"size",36.0));
    if (not all([for (p in points) all(p |> mesh.finite(~))])) []
    else if (primitive.kind=="triangle") [points]
    else if (primitive.kind=="box") (let q=box(primitive.id,primitive.row,points[0],points[1]),q |> ~.points)
    else if (primitive.kind=="point") camera.billboard(cam,points[0],size)
    else [*camera.ribbon(cam,points[0],points[1],width),
        *if (primitive.kind=="vector") (
            let tip=camera.project(cam,points[1]),let tail=camera.project(cam,points[0]),let length=math.hypot(tip.x-tail.x,tip.y-tail.y),
            let head=option(options,"arrow_size",8.0),let dx=(tip.x-tail.x)/length,let dy=(tip.y-tail.y)/length,
            if (not tip.visible or length==0) [] else [[points[1],camera.unproject(cam,tip.x-head*dx-head*dy/2,tip.y-head*dy+head*dx/2,tip.depth),
                camera.unproject(cam,tip.x-head*dx+head*dy/2,tip.y-head*dy-head*dx/2,tip.depth)]]) else []]
}

fn color_records(spec) {
    let field=spec.encoding.color.field;
    if (field==null) [] else if (contains(["grid","curvilinear"],spec.data.kind))
        [for (row in (if (spec.mark.association=="cell" or spec.mark.association=="face") spec.data.cell_fields[field] else spec.data.vertex_fields[field]))
            for (value in row where mesh.finite(value)) map([field,value])]
    else if (spec.data.kind=="mesh" or spec.data.kind=="prepared_mesh")
        if (spec.mark.association=="face" or spec.mark.association=="cell") spec.data.faces else spec.data.vertices
    else spec.primitives |> ~.row
}

fn build_faces(primitives,views,contexts,mappings,ratios,cam) => [for (p in primitives,let v=views[p.view],let ctx=contexts[p.view],
    let color=if (p.depth_only==true) "#ffffff" else mark.appearance(ctx,"color",p.row,option(v.mark,"color","#4e79a7")),let channels=rgba.parse(color,"chart.figure color") ^ {^})
    for (t in triangles(p,mappings,ratios,cam,ctx,v.mark))
        {id:p.id,row:p.row,view:p.view,points:t,color:channels,data_points:p.points,kind:p.kind,depth_only:p.depth_only==true,samples:p.samples,fields:p.fields}]

pub fn retarget(prepared,pose) {
    if (prepared is error) prepared else if (pose==null) prepared else {
        let cam=camera.prepare(pose,prepared.width,prepared.height);
        if (cam is error) cam else {*:prepared,camera:cam,faces:build_faces(prepared.primitives,prepared.views,prepared.contexts,prepared.mappings,prepared.ratios,cam)}
    }
}

pub fn prepare(raw,viewport=null) {
    let spec=if (raw is element) parse.parse_top(raw) else raw;
    let context=sizing.viewport(viewport);
    let width=option(spec,"width",option(context,"width",640));
    let height=option(spec,"height",option(context,"height",440));
    let views=leaves(spec);
    let failure=if (views is error) views else util.first_error(views |> ~.primitives);
    if (context is error) context else if (failure is error) failure else {
    let primitives=[for (vi,v in views) for (p in v.primitives) {*:p,view:vi}];
    let axes=["x","y","z"];
    let rows=[for (p in primitives) for (q in p.points) {x:q[0],y:q[1],z:q[2]}];
    let encoding=views[0].encoding;
    let all_bars=all(views |> ~.mark.kind=="bar");
    let mappings=[for (axis in axes) scale.position_scale({*:encoding[axis],field:axis},rows,-1.0,1.0,if (all_bars and axis!="z") "bar" else "point",axis=="x")];
    let aspect=option(spec.coordinate,"aspect","auto");
    let ratios=if (aspect=="equal") [for (m in mappings) abs(m.domain[1]-m.domain[0])]
        else if (aspect is array) aspect else [1.0,1.0,1.0];
    let normalized=[for (r in ratios) r/max(ratios)];
    let cam=camera.prepare(spec.coordinate.camera,width,height);
    let maximum=option(spec,"max_triangles",100000);
    if (cam is error) cam
    else if (util.first_error(mappings) is error) util.first_error(mappings)
    else if (not (aspect is array) and not contains(["auto","equal"],aspect) or len(ratios)!=3 or not all(ratios |> mesh.finite(~) and ~>0)) error("chart.figure: invalid aspect ratio")
    else if (aspect=="equal" and any(mappings |> ~.kind!="linear")) error("chart.figure: equal aspect requires linear axes")
    else if (any([for (i,m in mappings) not contains(["linear","log","sqrt","pow"],m.kind) and not (all_bars and i<2 and m.kind=="band")]))
        error("chart.figure: positions require continuous scales, or categorical bar axes")
    else if (not mesh.positive_int(maximum) or sum([for (p in primitives) if (p.kind=="point") 16 else if (p.kind=="box") 12 else if (p.kind=="vector") 3 else if (p.kind=="segment") 2 else 1])>maximum)
        error("chart.figure: invalid or exceeded triangle limit")
    else if (any([for (p in primitives) not all([for (pi,q in p.points) all([for (i,v in q)
        mesh.finite(v) or (p.kind=="box" and mappings[i].kind=="band" and (v!=null or pi==1))])])])) error("chart.figure: invalid geometry endpoint")
    else {
        let contexts=[for (v in views) renderer.mark_context([*(v.primitives |> ~.row),*color_records(v)],v.encoding,null,null,{},null)];
        let faces=build_faces(primitives,views,contexts,mappings,normalized,cam);
        let colors_error=util.first_error(faces |> ~.color);
        if (len(faces)>maximum) error("chart.figure: triangle limit exceeded")
        else if (colors_error is error) colors_error
        else if (any(faces |> ~.color[3]!=1.0)) error("chart.figure: translucent scientific colors are unsupported")
        else {spec:spec,width:width,height:height,views:views,contexts:contexts,mappings:mappings,ratios:normalized,camera:cam,faces:faces,primitives:primitives}
    }
    }
}

fn scene_mesh(faces,lighting,depth_only=false) {
    let positions=[for (face in faces) for (p in face.points) for (v in p) v];
    let normals=[for (face in faces,let normal=camera.cross(camera.subtract(face.points[1],face.points[0]),camera.subtract(face.points[2],face.points[0])),
        let unit=if (camera.dot(normal,normal)>0) camera.unit(normal) else [0.0,1.0,0.0]) for (p in face.points) for (v in unit) v];
    scene.mesh(scene.geometry(positions,{normals:normals,colors:[for (face in faces) for (p in face.points) for (v in slice(face.color,0,3)) v]}),
        scene.material(if (lighting=="lambert") 'lambert' else 'basic',{side:'double',depth_only:depth_only}))
}
fn guides(prepared) {
    let font=text.style({},"label",11);
    let cam=prepared.camera;
    let ratios=prepared.ratios;
    let ticks=[for (i,axis in ["x","y","z"]) for (value in scale.scale_ticks(prepared.mappings[i],5),
        let p=[for (j in 0 to 2) if (j==i) scale.scale_apply(prepared.mappings[i],value)*ratios[j] else 0-ratios[j]],
        let projected=camera.project(cam,[p[0],p[2],0-p[1]]) where projected.visible)
        {axis:axis,value:value,x:projected.x,y:projected.y}];
    let plans=legend.plans(prepared.views[0].encoding,prepared.contexts[0],cfg.resolve_theme(prepared.spec.config));
    let metrics=text.measure(ticks |> util.format_value(~.value),font);
    let selected=collision.select([for (i,tick in ticks) {*:tick,optional:true,gap:3.0,bounds:text.bounds(metrics[i],"middle",0,tick.x,tick.y+14)}]);
    <svg class:"chart-3d-guides",width:prepared.width,height:prepared.height,viewBox:"0 0 "++string(prepared.width)++" "++string(prepared.height),
        style:"position:absolute;left:0;top:0;pointer-events:none",
        for (tick in selected) <text x:tick.x,y:tick.y+14,'text-anchor':"middle",*:text.attributes(font),util.format_value(tick.value,null)>;
        if (prepared.spec.title!=null) <text x:prepared.width/2,y:20,'text-anchor':"middle",*:text.attributes(text.style({},"title",16,600)),prepared.spec.title>;
        for (plan in plans) <g transform:svg.translate(prepared.width-plan.width-8,34),legend.render_plan(plan)>
    >
}
pub fn present(prepared) {
    if (prepared is error) prepared else {
        let corners=box_corners([0-prepared.ratios[0],0-prepared.ratios[2],0-prepared.ratios[1]],[prepared.ratios[0],prepared.ratios[2],prepared.ratios[1]]);
        let edges=util.unique_vals([for (side in box_sides) for (i,a in side) mesh.edge_key(a,side[(i+1)%4])]);
        let spines=[for (edge in edges) for (t in camera.ribbon(prepared.camera,corners[edge[0]],corners[edge[1]],0.7))
            {points:t,color:[0.6,0.6,0.6,1.0]}];
        let image=scene.scene([scene.camera({*:prepared.camera,type:symbol(prepared.camera.type)}),scene.light('ambient',{intensity:0.5}),scene.light('directional',{position:[3,5,4],intensity:0.7}),
            if (len(spines)>0) scene_mesh(spines,"none"),
            for (vi,v in prepared.views) for (depth in [true,false],let faces=prepared.faces |: ~.view==vi and ~.depth_only==depth where len(faces)>0)
                scene_mesh(faces,v.mark.lighting,depth)],
            {width:prepared.width,height:prepared.height,background:option(prepared.spec.config,"background","#ffffff")});
        let normalized=scene.normalize(image) ^ {^};
        if (normalized is error) normalized else <div class:"lambda-chart-3d",width:prepared.width,height:prepared.height,role:"img",'aria-label':option(prepared.spec,"title","3D scientific chart"),
            style:"position:relative;width:"++string(prepared.width)++"px;height:"++string(prepared.height)++"px",
            normalized;guides(prepared)>
    }
}
pub fn figure(spec,viewport=null) => present(prepare(spec,viewport))

pub fn interactive(spec,viewport=null,options={}) {
    let prepared=prepare(spec,viewport);
    if (prepared is error) prepared else apply(<scientific_view prepared:prepared,options:options>)
}

// Prepared analysis is owned by the applied value; camera handlers only rebuild projected geometry (S9.1.4/S12.1.3).
view <scientific_view> state pose:null, gesture:null, picked:null {
    <div class:"lambda-chart-3d-interactive",tabindex:"0",style:"display:inline-block;touch-action:none",
        'data-chart-picked':if (picked!=null) format(picked.source_id,{type:"json",compact:true}) else "",
        present(retarget(~.prepared,pose))>
}
on pointerdown(evt) {
    let host=dom.closest(evt.target,".lambda-chart-3d-interactive");
    if (host!=null and (evt.button==0 or evt.button==1)) {
        gesture={x:evt.clientX,y:evt.clientY,pointer:evt.pointerId,pan:evt.shiftKey==true or evt.button==1,initial:pose}
        if (evt.pointerId!=null) { host.set_pointer_capture(evt.pointerId) }
        return 'prevent-default'
    }
    'pass'
}
on pointermove(evt) {
    if (gesture!=null and evt.pointerId==gesture.pointer) {
        let current=if (pose!=null) pose else ~.prepared.camera;
        let dx=evt.clientX-gesture.x;
        let dy=evt.clientY-gesture.y;
        pose=camera.control(current,if (gesture.pan) {type:"pan",dx:dx,dy:dy} else {type:"orbit",yaw:0-dx*0.008,pitch:0-dy*0.008})
        gesture={*:gesture,x:evt.clientX,y:evt.clientY}
        return 'prevent-default'
    }
    'pass'
}
on pointerup(evt) { if (gesture!=null and gesture.pointer==evt.pointerId) { gesture=null } }
on pointercancel(evt) { if (gesture!=null and gesture.pointer==evt.pointerId) { pose=gesture.initial;gesture=null } }
on lostpointercapture(evt) { gesture=null }
on wheel(evt) {
    let current=if (pose!=null) pose else ~.prepared.camera;
    pose=camera.control(current,{type:"zoom",factor:1.001**max(-1000,min(1000,evt.deltaY))})
    'prevent-default'
}
on dblclick(evt) { pose=null;gesture=null }
on keydown(evt) {
    if (evt.key=="r" or evt.key=="Home") { pose=null;gesture=null;return 'prevent-default' }
    let current=if (pose!=null) pose else ~.prepared.camera;
    if (evt.key=="+" or evt.key=="-" or evt.key=="=") {
        pose=camera.control(current,{type:"zoom",factor:if (evt.key=="-") 1.1 else 1/1.1});return 'prevent-default'
    }
    if (contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"],evt.key)) {
        pose=camera.control(current,{type:"pan",dx:if (evt.key=="ArrowLeft") 10 else if (evt.key=="ArrowRight") -10 else 0,
            dy:if (evt.key=="ArrowUp") 10 else if (evt.key=="ArrowDown") -10 else 0});return 'prevent-default'
    }
    'pass'
}
on click(evt) {
    let host=dom.closest(evt.target,".lambda-chart-3d-interactive");
    let bounds=dom.bounding_box(host);
    let prepared=retarget(~.prepared,pose);
    let hit=pick(prepared,(evt.clientX-bounds.left)*prepared.width/bounds.width,(evt.clientY-bounds.top)*prepared.height/bounds.height);
    picked=hit
    if (hit!=null) { emit("chart_pick",hit) }
}

pub fn pick(prepared,x,y) {
    if (prepared is error) prepared else if (not mesh.finite(x) or not mesh.finite(y)) error("chart.figure: picking requires finite CSS coordinates")
    else {
        let ray=camera.ray(prepared.camera,x,y);
        let hits=[for (i,face in prepared.faces,let hit=camera.triangle_hit(ray,face.points[0],face.points[1],face.points[2])
            where hit!=null and camera.project(prepared.camera,hit.position).visible)
            {*:hit,source_id:face.id,record:face.row,view:face.view,triangle:i,depth_only:face.depth_only,
                values:map([for (field in face.fields where all([for (sample in face.samples) mesh.finite(sample[field])]))
                    for (part in [field,sum([for (j,sample in face.samples) hit.weights[j]*float(sample[field])])]) part]),
                data_position:if (face.kind=="triangle") [for (axis in 0 to 2) sum([for (j,p in face.data_points) hit.weights[j]*p[axis]])] else null}];
        let nearest=sort(hits,(hit)=>[hit.distance,hit.depth_only])[0];
        if (nearest.depth_only==true) null else nearest
    }
}
