// Chart adapters retain geometry on each record and feed the common scales/guides.
import mesh: .mesh
import contours: .mesh_contour
import mark: .mark
import svg: .svg
import scale: .scale
import util: .util
import stream: .stream
import geometry: .geometry
import text: .text
import collision: .collision

pub fn supports(kind) any | error => contains(["mesh","wireframe","contour","contour_band","streamline"],kind)
pub fn prepare(spec) {
    let options=spec.mark;
    let prepared=mesh.prepare(spec.data,options);
    let association=if (options.association!=null) options.association else "vertex";
    let field=spec.encoding.color.field;
    let fields=if (field!=null) [field] else [];
    let contour=contains(["contour","contour_band"],options.kind);
    let streamline=options.kind=="streamline";
    let aliases=prepared.coordinate_fields;
    let encoding={*:spec.encoding,x:if (spec.encoding.x!=null) spec.encoding.x else {field:if (aliases.x!=null) aliases.x else "x",dtype:"quantitative",zero:false},
        y:if (spec.encoding.y!=null) spec.encoding.y else {field:if (aliases.y!=null) aliases.y else "y",dtype:"quantitative",zero:false}};
    let invalid=if (prepared is error) prepared
        else if (not supports(options.kind)) error("chart: scientific descriptors require a mesh, wireframe, or contour mark")
        else if (not contains(["vertex","face","cell"],association)) error("chart: invalid scientific association")
        else if (field!=null and not streamline and not contains(if (association=="vertex") prepared.vertex_fields else prepared.face_fields,field)) error("chart: unknown scientific scalar field")
        else if (streamline and association!="vertex") error("chart: streamlines require vertex vectors")
        else if (contour and association!="vertex") error("chart: contours require vertex values")
        else if (streamline and options.arrows!=null and (not mesh.finite(options.arrows) or options.arrows<0 or floor(options.arrows)!=options.arrows or options.arrows>100)) error("chart: streamline arrows must be an integer from zero to 100")
        else if (options.shading!=null and options.shading!="flat") error("chart: scientific SVG shading currently requires flat") else null;
    let rows=if (invalid is error) invalid
        else if (streamline) (let traced=stream.trace(prepared,options),
            if (traced is error) traced else if (traced.diagnostics.incomplete and options.allow_partial!=true) error("chart: incomplete streamline; increase work limits or set allow_partial")
            else [for (path in traced.paths where len(path.points)>1) {*:path,points:path.points |> [~.x,~.y],samples:path.points,
                magnitude:avg(path.points |> ~.magnitude),face_ids:util.unique_vals(path.points |> ~.face_id)}])
        else if (contour) contours.contours(prepared,field,if (options.levels!=null) options.levels else 8,{*:options,bands:options.kind=="contour_band"})
        else if (options.kind=="wireframe") [for (edge in prepared.edges,
            let uses=[for (use in edge.uses where mesh.face_active(prepared,prepared.faces[use.face],fields,association)) use],
            let diagonal=len(edge.uses)==2 and prepared.faces[edge.uses[0].face].cell_id!=null and
                prepared.faces[edge.uses[0].face].cell_id==prepared.faces[edge.uses[1].face].cell_id
            where len(uses)>0 and (options.diagonals==true or not diagonal))
            {id:edge.vertices,points:[for (i in edge.vertices) mesh.point(prepared.vertices[i])],
                face_ids:[for (u in uses) prepared.faces[u.face].id],
                *:if (field!=null) map([field,if (association=="vertex") avg([for (i in edge.vertices) float(prepared.vertices[i][field])])
                    else float(prepared.faces[uses[0].face][field])]) else {}}]
        else [for (f in (if (association=="cell" and prepared.cells!=null) prepared.cells else prepared.faces) where mesh.face_active(prepared,f,fields,association))
            {*:f,points:[for (i in f.vertices) mesh.point(prepared.vertices[i])],face_ids:[f.id],closed:true,
                *:if (field!=null) map([field,if (association=="vertex") avg([for (i in f.vertices) float(prepared.vertices[i][field])]) else float(f[field])]) else {}}];
    let data=if (rows is error) rows else [for (row in rows)
        {*:row,x:avg(row.points |> ~[0]),y:avg(row.points |> ~[1]),
            *:map([for (entry in [[aliases.x,avg(row.points |> ~[0])],[aliases.y,avg(row.points |> ~[1])],
                [if (contour) field else null,row.level]] where entry[0]!=null) for (part in entry) part])}];
    let domain=[for (row in data) for (p in row.points) {*:row,*:map([encoding.x.field,p[0],encoding.y.field,p[1]])},
        for (v in prepared.vertices) {*:v,*:map([encoding.x.field,v.x,encoding.y.field,v.y])}];
    {*:spec,data:data,encoding:encoding,_mesh:prepared,_domain_data:domain}
}

fn screen(p,ctx) => [scale.scale_apply(ctx.x_scale,p[0]),scale.scale_apply(ctx.y_scale,p[1])]
fn line_style(ctx,row,options) => mark.style(ctx,row,options,{fill:"none",stroke:"#4c78a8",'stroke-width':mark.appearance(ctx,"size",row,1.5),opacity:1.0},true)
fn stream_marks(data,ctx,options) => svg.group_class("marks streamline",[for (row in data,
    let arrows=if (options.arrows!=null) options.arrows else 1,let samples=row.samples,let total=samples[len(samples)-1].length)
    <g class:"streamline-path",'data-source-id':format(row.id,{type:"json",compact:true}),'data-incomplete':row.incomplete,
        for (i in 1 to (len(samples)-1),let appearance=line_style(ctx,{*:row,magnitude:(samples[i-1].magnitude+samples[i].magnitude)/2},options))
            <path d:svg.line_path([screen(row.points[i-1],ctx),screen(row.points[i],ctx)]),*:appearance,'stroke-linecap':"round">;
        for (index in 1 to arrows,let length=total*index/(arrows+1),
            let i=([for (i in 1 to (len(samples)-1) where samples[i].length>=length) i])[0],
            let t=(length-samples[i-1].length)/(samples[i].length-samples[i-1].length),
            let tip=screen(geometry.interpolate(row.points[i-1],row.points[i],t),ctx),let from=screen(row.points[i-1],ctx),
            let appearance=line_style(ctx,{*:row,magnitude:samples[i].magnitude},options))
            svg.arrow_head(from[0],from[1],tip[0],tip[1],appearance.stroke,if (options.arrow_size!=null) options.arrow_size else 7.0);
        mark.tooltip(ctx,row)
    >])

fn contour_labels(data,ctx,options) {
    let font=text.style({},"label",if (options.label_size!=null) options.label_size else 11);
    let labels=data |> util.format_value(~.level,options.label_format);
    let metrics=text.measure(labels,font);
    let candidates=[for (i,row in data,let points=row.points |> screen(~,ctx),let middle=int(floor(len(points)/2)),
        let length=sum([for (j in 1 to (len(points)-1)) math.hypot(points[j][0]-points[j-1][0],points[j][1]-points[j-1][1])])
        where len(points)>1 and length>metrics[i].width+16)
        {x:points[middle][0],y:points[middle][1]-3,label:labels[i],optional:true,gap:4,
            bounds:text.bounds(metrics[i],"middle",0,points[middle][0],points[middle][1]-3)}];
    [for (label in collision.select(candidates)) <text class:"contour-label",x:label.x,y:label.y,'text-anchor':"middle",*:text.attributes(font),label.label>]
}

pub fn render(data,ctx,options) {
    let line=options.kind=="wireframe" or options.kind=="contour" or options.kind=="streamline";
    if (options.kind=="streamline") stream_marks(data,ctx,options) else svg.group_class("marks "++options.kind,[for (row in data)
        <path class:options.kind++"-fragment",'data-source-id':format(row.id,{type:"json",compact:true}),
            d:svg.line_path([for (p in row.points) [scale.scale_apply(ctx.x_scale,p[0]),scale.scale_apply(ctx.y_scale,p[1])]])++(if (row.closed) " Z" else ""),
            *:mark.style(ctx,row,options,if (line) {fill:"none",stroke:"#4c78a8",'stroke-width':1.0,opacity:1.0}
                else {fill:"#4c78a8",stroke:"none",opacity:1.0},line),mark.tooltip(ctx,row)>,
        *if (options.kind=="contour" and options.labels==true) contour_labels(data,ctx,options) else []])
}
