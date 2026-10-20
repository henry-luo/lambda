// Intersections carry vertex/edge keys, so joins never depend on rounded coordinates.
import mesh: .mesh
import geometry: .geometry
import records: .records
import util: .util

fn intersection(vertices,a,b,field,level) {
    let va=float(vertices[a][field]);
    let vb=float(vertices[b][field]);
    let t=(level-va)/(vb-va);
    {key:if (level==va) ["v",a] else if (level==vb) ["v",b] else ["e",*mesh.edge_key(a,b)],
        point:geometry.interpolate(mesh.point(vertices[a]),mesh.point(vertices[b]),t)}
}

fn segments(prepared,field,level) {
    let vertices=prepared.vertices;
    let active=prepared.faces |: mesh.face_active(prepared,~,[field]);
    let flat=[for (f in active where all([for (i in f.vertices) float(vertices[i][field])==level])) f];
    let inner=[for (e in mesh.edges(flat) where len(e.uses)>1) e.vertices];
    let raw=[for (face in active,
        let points=[for (i,a in face.vertices,let b=face.vertices[(i+1)%3],let va=float(vertices[a][field]),let vb=float(vertices[b][field])
            where (va<level and vb>=level) or (vb<level and va>=level)) intersection(vertices,a,b,field,level)],
        let unique=records.group_by(points,["key"]) |> ~.rows[0]) (
        for (i,a in face.vertices,let b=face.vertices[(i+1)%3] where float(vertices[a][field])==level and float(vertices[b][field])==level and
            not contains(inner,mesh.edge_key(a,b)))
            {a:{key:["v",a],point:mesh.point(vertices[a])},b:{key:["v",b],point:mesh.point(vertices[b])},faces:[face.id]},
        if (len(unique)==2 and unique[0].key!=unique[1].key) {a:unique[0],b:unique[1],faces:[face.id]})] |: ~!=null;
    [for (group in records.group_by([for (s in raw) {*:s,key:sort([s.a.key,s.b.key])}],["key"]))
        {*:group.rows[0],faces:util.unique_vals([for (s in group.rows) for (id in s.faces) id])}]
}

fn walk(remaining,points,keys,faces) {
    let end=keys[len(keys)-1];
    let next=([for (i,s in remaining where s.a.key==end or s.b.key==end) i])[0];
    if (next==null or (len(keys)>2 and keys[0]==end)) {remaining:remaining,points:points,keys:keys,faces:faces}
    else {
        let s=remaining[next];
        let p=if (s.a.key==end) s.b else s.a;
        walk([for (i,s in remaining where i!=next) s],[*points,p.point],[*keys,p.key],[*faces,*s.faces])
    }
}
fn stitch(segments,result=[]) {
    if (len(segments)==0) result else {
        let degrees=records.group_by([for (s in segments) for (p in [s.a,s.b]) p],["key"]);
        let endpoint=(degrees |: len(~.rows)==1)[0].rows[0];
        let start=if (endpoint!=null) endpoint else segments[0].a;
        let path=walk(segments,[start.point],[start.key],[]);
        stitch(path.remaining,[*result,{points:path.points,keys:path.keys,face_ids:util.unique_vals(path.faces),closed:path.keys[0]==path.keys[len(path.keys)-1]}])
    }
}

pub fn levels(prepared,field,requested) {
    let values=[for (v in prepared.vertices where mesh.finite(v[field])) float(v[field])];
    let result=if (requested is array) requested else if (mesh.positive_int(requested))
        if (len(values)==0) [] else util.nice_ticks(min(values),max(values),int(requested)) else null;
    if (result==null or not all([for (i,v in result) mesh.finite(v) and (i==0 or v>result[i-1])]))
        error("mesh.contours: levels must increase and be finite") else result
}

pub fn contours(prepared,field,requested,options={}) {
    let thresholds=levels(prepared,field,requested);
    let maximum=if (options.max_fragments!=null) options.max_fragments else 100000;
    if (thresholds is error) thresholds
    else if (not contains(prepared.vertex_fields,field)) error("mesh.contours: expected a vertex scalar field")
    else if (not mesh.positive_int(maximum) or len(thresholds)*len(prepared.faces)*3>maximum)
        error("mesh.contours: fragment limit exceeded")
    else if (options.bands==true) [for (li in 0 to (len(thresholds)-2),let lo=thresholds[li],let hi=thresholds[li+1])
        for (f in prepared.faces,
            let triangle=[for (i in f.vertices) [*mesh.point(prepared.vertices[i]),float(prepared.vertices[i][field])]],
            let polygon=geometry.clip(geometry.clip(triangle,(p)=>p[2]-lo),(p)=>hi-p[2])
            where mesh.face_active(prepared,f,[field]) and len(polygon)>=3 and abs(geometry.area(polygon))>0)
            {id:[li,f.id],level:(lo+hi)/2,band:[lo,hi],points:polygon,face_ids:[f.id],closed:true}]
    else [for (li,level in thresholds) for (pi,path in stitch(segments(prepared,field,level)))
        {*:path,id:[li,pi],level:level}]
}
