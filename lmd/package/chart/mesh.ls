// Scientific topology and interpolation are immutable values (S12.1.1v2).
import util: .util
import geometry: .geometry
import records: .records
import collision: .collision

pub fn supports(data) any | error => contains(["grid", "curvilinear", "mesh", "prepared_mesh"], data.kind)
pub fn finite(value) any | error => not (value is complex) and util.finite_number(value) and util.finite_number(float(value))
pub fn positive_int(value) => finite(value) and value >= 1 and floor(value) == value
fn field_names(fields) => [for (key, value in fields) string(key)]
fn matrix(value, rows, columns) => value is array and len(value) == rows and
    all([for (row in value) row is array and len(row) == columns])
fn increasing(values) => values is array and all([for (i, v in values)
    finite(v) and (i == 0 or float(v) > float(values[i - 1]))])
fn mask_valid(value, rows, columns) => value == null or (matrix(value, rows, columns) and
    all([for (row in value) all([for (v in row) v is bool])]))
fn fields_valid(fields, rows, columns) => (fields == null or fields is map) and
    all([for (key, value in fields) not contains(["id", "x", "y", "z", "masked", "vertices"], string(key)) and matrix(value, rows, columns)])
fn at_fields(fields, j, i) => map([for (key, value in fields) for (part in [string(key), value[j][i]]) part])
pub fn point(vertex) => [float(vertex.x), float(vertex.y)]
pub fn edge_key(a, b) => [min(a, b), max(a, b)]
fn face_points(vertices, face) => [for (i in face.vertices) point(vertices[i])]
fn orientation(vertices, face) any | error => (let p = face_points(vertices, face), math.orient2d(p[0], p[1], p[2]))
fn default_id(row, index) => if (row.id != null) row.id else index
fn duplicate_ids(rows) => len(util.unique_vals([for (row in rows) row.id])) != len(rows)

fn grid_data(data) {
    let rect = data.kind == "grid";
    let ny = len(data.y);
    let nx = if (rect) len(data.x) else len(data.x[0]);
    let empty = ny == 0 and len(data.x) == 0;
    let shape = if (rect) increasing(data.x) and increasing(data.y)
        else matrix(data.x, ny, nx) and matrix(data.y, ny, nx) and
            all([for (row in [*data.x, *data.y]) all([for (v in row) finite(v)])]);
    let names = [*field_names(data.vertex_fields), *field_names(data.cell_fields)];
    let aliases = [data.coordinate_fields.x, data.coordinate_fields.y] |: ~ != null;
    if (not shape or (not empty and (nx < 2 or ny < 2))) error("mesh.prepare: invalid grid coordinates or shape")
    else if (not fields_valid(data.vertex_fields, ny, nx) or not fields_valid(data.cell_fields, max(0, ny-1), max(0, nx-1)))
        error("mesh.prepare: invalid field matrix or reserved field name")
    else if (not mask_valid(data.vertex_mask, ny, nx) or not mask_valid(data.cell_mask, max(0, ny-1), max(0, nx-1)))
        error("mesh.prepare: invalid mask shape or value")
    else if (len(util.unique_vals(aliases)) != len(aliases) or any([for (a in aliases)
        not (a is string) or contains(["id", "x", "y", "z", "masked", "vertices", *names], a)]))
        error("mesh.prepare: invalid coordinate alias")
    else {
        let vertices = [for (j in 0 to (ny-1)) for (i in 0 to (nx-1))
            {id: [j,i], x: float(if (rect) data.x[i] else data.x[j][i]),
                y: float(if (rect) data.y[j] else data.y[j][i]), masked: data.vertex_mask[j][i] == true,
                *:at_fields(data.vertex_fields,j,i)}];
        let cells = [for (j in 0 to (ny-2)) for (i in 0 to (nx-2))
            {id: [j,i], vertices: [j*nx+i,j*nx+i+1,(j+1)*nx+i+1,(j+1)*nx+i],
                masked: data.cell_mask[j][i] == true, *:at_fields(data.cell_fields,j,i)}];
        let invalid = [for (cell in cells, let p = face_points(vertices,cell)
            where not all([for (i in 0 to 3) math.orient2d(p[i],p[(i+1)%4],p[(i+2)%4]) > 0])) cell.id];
        if (len(invalid)>0) error("mesh.prepare: grid cell must be convex with positive orientation at "++string(invalid[0]))
        else {kind:"mesh", source_kind:data.kind, vertices:vertices, cells:cells,
            vertex_fields:field_names(data.vertex_fields), face_fields:field_names(data.cell_fields),
            coordinate_fields:data.coordinate_fields,
            faces:[for (cell in cells) for (k,t in [[0,1,2],[0,2,3]])
                {*:cell,id:[cell.id,k],cell_id:cell.id,vertices:[for (i in t) cell.vertices[i]]}]}
    }
}

fn mesh_data(data) any | error {
    if (not (data.vertices is array) or not (data.faces is array)) error("mesh.prepare: vertices and faces must be arrays")
    else {kind:"mesh",source_kind:"mesh",cells:null,
        vertices:[for (i,v in data.vertices) {*:v,id:default_id(v,i)}],
        faces:[for (i,f in data.faces) {*:f,id:default_id(f,i)}],
        vertex_fields:util.unique_vals([for (v in data.vertices) for (key,value in v
            where not contains(["id","x","y","z","masked"],string(key))) string(key)]),
        face_fields:util.unique_vals([for (f in data.faces) for (key,value in f
            where not contains(["id","vertices","masked"],string(key))) string(key)])}
}

fn proper_cross(a,b,c,d) => math.orient2d(a,b,c)*math.orient2d(a,b,d)<0 and
    math.orient2d(c,d,a)*math.orient2d(c,d,b)<0
fn strictly_inside(p,t) any | error => (let signs=[for (i in 0 to 2) math.orient2d(t[i],t[(i+1)%3],p)],
    all(signs |> ~ > 0) or all(signs |> ~ < 0))
fn overlaps(a,b) any | error => any([for (i in 0 to 2) for (j in 0 to 2)
    proper_cross(a[i],a[(i+1)%3],b[j],b[(j+1)%3])]) or
    any([for (p in a) strictly_inside(p,b)]) or any([for (p in b) strictly_inside(p,a)]) or
    strictly_inside([avg(a |> ~[0]),avg(a |> ~[1])],b)

pub fn edges(faces) {
    let directed = [for (fi,face in faces) for (i,a in face.vertices,
        let b=face.vertices[(i+1)%3]) {key:edge_key(a,b),a:a,b:b,face:fi}];
    [for (group in records.group_by(directed,["key"])) {vertices:group.key[0], uses:group.rows}]
}

fn bounds(points) => [[min(points |> ~[0]),max(points |> ~[0])],[min(points |> ~[1]),max(points |> ~[1])]]
fn bin(value,axis,count) => if (axis[0]==axis[1]) 0 else int(max(0,min(count-1,floor((value-axis[0])/(axis[1]-axis[0])*count))))
fn spatial(vertices,faces) {
    let extent=if (len(vertices)==0) [[0.0,0.0],[0.0,0.0]] else bounds(vertices |> point(~));
    let count=max(1,min(32,int(ceil(sqrt(len(faces))))));
    let boxes=[for (f in faces) bounds(face_points(vertices,f))];
    {extent:extent,count:count,bins:[for (j in 0 to (count-1)) for (i in 0 to (count-1))
        [for (fi,b in boxes where i>=bin(b[0][0],extent[0],count) and i<=bin(b[0][1],extent[0],count) and
            j>=bin(b[1][0],extent[1],count) and j<=bin(b[1][1],extent[1],count)) fi]]}
}

fn overlap_candidate(faces,boxes,a,b) => not faces[a].masked and not faces[b].masked and collision.overlaps(boxes[a],boxes[b])
fn overlapping_faces(vertices,faces,index) {
    let triangles=[for (face in faces) face_points(vertices,face)];
    let boxes=[for (points in triangles,let extent=bounds(points))
        {left:extent[0][0],right:extent[0][1],top:extent[1][0],bottom:extent[1][1]}];
    // wide faces can fill many bins; cap broad-phase work at a single all-pairs box scan.
    let candidates=if (sum(index.bins |> len(~)**2)>len(faces)**2)
        [for (a,face in faces) for (b,other in faces where a<b and overlap_candidate(faces,boxes,a,b)) [a,b]]
        // positive-area intersections share a bin; test each pair only in its first common bin.
        else [for (bucket,entries in index.bins) for (i,a in entries)
            for (j,b in entries where i<j and overlap_candidate(faces,boxes,a,b) and
                bucket==bin(max(boxes[a].top,boxes[b].top),index.extent[1],index.count)*index.count+
                    bin(max(boxes[a].left,boxes[b].left),index.extent[0],index.count)) [a,b]];
    // source order keeps overlap diagnostics deterministic, regardless of bin traversal.
    [for (pair in sort(candidates) where overlaps(triangles[pair[0]],triangles[pair[1]]))
        [faces[pair[0]].id,faces[pair[1]].id]]
}

pub fn prepare(data, options = {}) {
    let maximum=if (options.max_vertices!=null) options.max_vertices else 4096;
    let face_max=if (options.max_faces!=null) options.max_faces else 8192;
    let count=if (data.kind=="grid") len(data.x)*len(data.y) else if (data.kind=="curvilinear") len(data.x)*len(data.x[0]) else len(data.vertices);
    if (not positive_int(maximum) or not positive_int(face_max)) error("mesh.prepare: limits must be positive integers")
    else if (count>maximum) error("mesh.prepare: vertex limit exceeded")
    else if (data.kind=="prepared_mesh") if (not (data.vertices is array) or not (data.faces is array) or not (data.edges is array) or
        not positive_int(data.index.count) or len(data.index.bins)!=data.index.count**2 or len(data.adjacency)!=len(data.faces) or
        not contains([2,3],data.dimension)) error("mesh.prepare: malformed prepared topology")
        else if (len(data.faces)>face_max) error("mesh.prepare: face limit exceeded") else data
    else if (not supports(data)) error("mesh.prepare: expected grid, curvilinear, or mesh descriptor")
    else if ((data.kind=="grid" or data.kind=="curvilinear") and 2*max(0,len(data.y)-1)*max(0,(if (data.kind=="grid") len(data.x) else len(data.x[0]))-1)>face_max)
        error("mesh.prepare: face limit exceeded")
    else {
        let raw=if (data.kind=="mesh") mesh_data(data) else grid_data(data);
        let vertices=raw.vertices;
        let faces=raw.faces;
        let invalid_vertex=[for (i,v in vertices where not finite(v.x) or not finite(v.y) or
            (v.z!=null and not finite(v.z)) or (v.masked!=null and not (v.masked is bool))) i];
        let invalid_face=[for (i,f in faces where not (f.vertices is array) or len(f.vertices)!=3 or
            (f.masked!=null and not (f.masked is bool)) or len(util.unique_vals(f.vertices))!=3 or
            any([for (index in f.vertices) not finite(index) or floor(index)!=index or index<0 or index>=len(vertices)])) i];
        if (raw is error) raw else if (len(faces)>face_max) error("mesh.prepare: face limit exceeded")
        else if (len(invalid_vertex)>0) error("mesh.prepare: invalid vertex at "++string(invalid_vertex[0]))
        else if (len(invalid_face)>0) error("mesh.prepare: invalid face at "++string(invalid_face[0]))
        else if (duplicate_ids(vertices) or duplicate_ids(faces)) error("mesh.prepare: duplicate source ID")
        else {
            let converted=[for (v in vertices) {*:v,x:float(v.x),y:float(v.y),*:if (v.z!=null) {z:float(v.z)} else if (options.dimension==3) {z:0.0} else {}}];
            let duplicate_positions=len(util.unique_vals([for (v in converted) if (options.dimension==3) [v.x,v.y,v.z] else [v.x,v.y]]))!=len(converted);
            let normalized=[for (f in faces) {*:f,masked:f.masked==true or any([for (i in f.vertices) vertices[i].masked==true])}];
            let active=normalized |: not ~.masked;
            let degenerate=[for (f in active where if (options.dimension==3) (
                let p=[for (i in f.vertices) converted[i]],
                math.orient2d([p[0].x,p[0].y],[p[1].x,p[1].y],[p[2].x,p[2].y])==0 and
                math.orient2d([p[0].x,p[0].z],[p[1].x,p[1].z],[p[2].x,p[2].z])==0 and
                math.orient2d([p[0].y,p[0].z],[p[1].y,p[1].z],[p[2].y,p[2].z])==0)
                else orientation(converted,f)==0) f.id];
            let duplicate_faces=len(util.unique_vals([for (f in faces) sort(f.vertices)]))!=len(faces);
            let links=edges(normalized);
            let index=spatial(converted,normalized);
            let crossing=if (options.dimension==3) [] else overlapping_faces(converted,normalized,index);
            if (duplicate_positions) error("mesh.prepare: duplicate binary64 vertex position")
            else if (len(degenerate)>0) error("mesh.prepare: degenerate face "++string(degenerate[0]))
            else if (duplicate_faces) error("mesh.prepare: duplicate face connectivity")
            else if (options.dimension!=3 and any(links |> len(~.uses)>2)) error("mesh.prepare: non-manifold edge")
            else if (len(crossing)>0) error("mesh.prepare: overlapping planar faces "++string(crossing[0]))
            else {*:raw,kind:"prepared_mesh",vertices:converted,faces:normalized,edges:links,
                cells:if (raw.cells!=null) [for (cell in raw.cells) {*:cell,masked:cell.masked==true or any([for (i in cell.vertices) vertices[i].masked==true])}] else null,
                adjacency:[for (fi,f in normalized) [for (edge in links where contains(edge.uses |> ~.face,fi))
                    for (use in edge.uses where use.face!=fi) use.face]], index:index,dimension:if (options.dimension==3) 3 else 2}
        }
    }
}

pub fn face_active(prepared, face, fields = [], association = "vertex") => not face.masked and
    all([for (field in fields) if (association=="vertex") all([for (i in face.vertices) finite(prepared.vertices[i][field])])
        else finite(face[field])])

// Scale edge differences before division; exact predicates decide inclusion independently.
pub fn barycentric(p, a, b, c) {
    let magnitude=max([abs(b[0]-a[0]),abs(b[1]-a[1]),abs(c[0]-a[0]),abs(c[1]-a[1])]);
    let u=[(b[0]-a[0])/magnitude,(b[1]-a[1])/magnitude];
    let v=[(c[0]-a[0])/magnitude,(c[1]-a[1])/magnitude];
    let q=[(p[0]-a[0])/magnitude,(p[1]-a[1])/magnitude];
    let det=u[0]*v[1]-u[1]*v[0];
    let y=(q[0]*v[1]-q[1]*v[0])/det;
    let z=(u[0]*q[1]-u[1]*q[0])/det;
    [1-y-z,y,z]
}

pub fn locate(prepared, position, fields = [], association = "vertex", include_masked = false) {
    let p=if (position is array) position else point(position);
    let index=prepared.index;
    let candidates=index.bins[bin(p[1],index.extent[1],index.count)*index.count+bin(p[0],index.extent[0],index.count)];
    let hits=[for (fi in candidates,let f=prepared.faces[fi],let t=face_points(prepared.vertices,f),
        let signs=[for (i in 0 to 2) math.orient2d(t[i],t[(i+1)%3],p)]
        where (include_masked or face_active(prepared,f,fields,association)) and (all(signs |> ~>=0) or all(signs |> ~<=0)))
        {face:fi,id:f.id,weights:barycentric(p,t[0],t[1],t[2])}];
    let hit=sort(hits,(v)=>v.id)[0];
    if (hit==null) null else {
        let f=prepared.faces[hit.face];
        {x:p[0],y:p[1],face:hit.face,face_id:f.id,cell_id:f.cell_id,weights:hit.weights,
            values:map([for (field in fields) for (part in [field,if (association=="vertex")
                sum([for (i,vi in f.vertices) hit.weights[i]*float(prepared.vertices[vi][field])]) else float(f[field])]) part])}
    }
}

// Split a candidate segment at topological edges before accepting it; small holes cannot be jumped.
pub fn segment_boundary(prepared,a,b,fields=[]) {
    let delta=[b[0]-a[0],b[1]-a[1]];
    let cuts=sort(util.unique_vals([0.0,1.0,for (edge in prepared.edges,
        let c=point(prepared.vertices[edge.vertices[0]]),let d=point(prepared.vertices[edge.vertices[1]]),
        let denominator=delta[0]*(d[1]-c[1])-delta[1]*(d[0]-c[0]),
        let t=if (denominator!=0) ((c[0]-a[0])*(d[1]-c[1])-(c[1]-a[1])*(d[0]-c[0]))/denominator else null,
        let u=if (denominator!=0) ((c[0]-a[0])*delta[1]-(c[1]-a[1])*delta[0])/denominator else null
        where t!=null and t>=0 and t<=1 and u>=0 and u<=1) t]));
    let gap=([for (i in 0 to (len(cuts)-2),let p=geometry.interpolate(a,b,(cuts[i]+cuts[i+1])/2)
        where cuts[i]<cuts[i+1] and locate(prepared,p,fields)==null)
        {t:cuts[i],point:geometry.interpolate(a,b,cuts[i]),reason:if (locate(prepared,p,[],"vertex",true)!=null) "masked" else "boundary"}])[0];
    gap
}

pub fn sample(data, positions, options = {}) {
    let prepared=prepare(data,options);
    let association=if (options.association!=null) options.association else "vertex";
    let fields=if (options.fields!=null) options.fields else if (association=="vertex") prepared.vertex_fields else prepared.face_fields;
    if (prepared is error) prepared
    else if (prepared.dimension!=2) error("mesh.sample: requires a planar field")
    else if (not contains(["vertex","face","cell"],association) or not (fields is array) or
        any([for (f in fields) not contains(if (association=="vertex") prepared.vertex_fields else prepared.face_fields,f)]))
        error("mesh.sample: invalid association or field")
    else if (not (positions is array) or any([for (p in positions,let xy=if (p is array) p else [p.x,p.y])
        len(xy)!=2 or not all(xy |> finite(~))])) error("mesh.sample: positions must be finite pairs")
    else (let samples=[for (p in positions) locate(prepared,if (p is array) p else point(p),fields,association)],
        let failure=util.first_error(samples),if (failure is error) failure else samples)
}

fn insert_point(vertices, faces, index) {
    let p=point(vertices[index]);
    let bad=[for (i,f in faces,let t=face_points(vertices,f)
        where math.incircle(t[0],t[1],t[2],p)>0) i];
    let cavity=edges([for (i,f in faces where contains(bad,i)) f]);
    let hull=edges(faces) |: len(~.uses)==1;
    let boundary=[for (e in cavity where len(e.uses)==1) e.uses[0],
        for (e in hull,let use=e.uses[0] where not contains(bad,use.face) and
            math.orient2d(point(vertices[use.a]),point(vertices[use.b]),p)<0) {a:use.b,b:use.a}];
    [for (i,f in faces where not contains(bad,i)) f,
        for (e in boundary where math.orient2d(point(vertices[e.a]),point(vertices[e.b]),p)>0)
            {vertices:[e.a,e.b,index]}]
}
fn insert_all(vertices, order, faces, index=0) => if (index>=len(order)) faces else
    insert_all(vertices,order,insert_point(vertices,faces,order[index]),index+1)

pub fn triangulate(points, options = {}) {
    let limit=if (options.max_vertices!=null) options.max_vertices else 4096;
    if (not positive_int(limit) or not (points is array) or len(points)>limit) error("mesh.triangulate: invalid points or vertex limit")
    else if (len(points)<3 or any([for (p in points) not finite(p.x) or not finite(p.y)])) error("mesh.triangulate: requires at least three finite points")
    else {
        let vertices=[for (i,p in points) {*:p,id:default_id(p,i),x:float(p.x),y:float(p.y)}];
        let ordered=sort([for (i,v in vertices) {index:i,xy:point(v)}],(v)=>v.xy);
        let seed=([for (v in ordered where math.orient2d(ordered[0].xy,ordered[1].xy,v.xy)!=0) v.index])[0];
        if (duplicate_ids(vertices) or len(util.unique_vals(ordered |> ~.xy))!=len(points)) error("mesh.triangulate: duplicate ID or binary64 position")
        else if (seed==null) error("mesh.triangulate: collinear points")
        else {
            let initial=[ordered[0].index,ordered[1].index,seed];
            let face={vertices:if (orientation(vertices,{vertices:initial})>0) initial else reverse(initial)};
            let triangles=insert_all(vertices,[for (v in ordered where not contains(initial,v.index)) v.index],[face]);
            prepare({kind:"mesh",vertices:vertices,faces:[for (f in triangles)
                {*:f,id:sort([for (i in f.vertices) vertices[i].id])}]},options)
        }
    }
}
