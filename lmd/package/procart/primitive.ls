// canonical geometry shares the existing Scene3D buffers and vector algebra (PG12.2, PG9).
import c: ._common
import s: lambda.scene3d
import shapes: lambda.scene3d.shape
import v: lambda.scene3d.camera
import t: lambda.scene3d.transform
pub let KINDS = ["box","grid","sphere","line","point","triangle","mesh","cylinder","tube"]
pub let DEFAULTS = {box:{},grid:{stroke:0.01},sphere:{segments:24,rings:12},line:{stroke:0.01},point:{diameter:0.02},triangle:{},mesh:{},cylinder:{segments:24},tube:{radius:0.5,segments:12,capped:false}}
pub let FIELDS = {box:[],grid:["stroke"],sphere:["segments","rings"],line:["stroke"],point:["diameter"],triangle:["points"],mesh:["geometry"],cylinder:["segments"],tube:["points","radius","segments","capped"]}
let PATH = [[-0.5,0.0,0.0],[0.5,0.0,0.0]]
pub fn parameters(kind,node,defaults={}) map^ {
    let authored = map([for (k,val in map(node) where contains(FIELDS[kind],string(k))) (string(k),val)])
    let p = {*:DEFAULTS[kind],*:c.value(defaults[kind],{}),*:authored}
    let counts = c.require((p.segments == null or c.bounded(p.segments,3,64,true)) and (p.rings == null or c.bounded(p.rings,2,48,true)),"invalid primitive tessellation")^
    let dims = c.require(all([for (k in ["stroke","diameter","radius"]) p[k] == null or c.bounded(p[k],0.0001,1000.0)]),"invalid primitive dimension")^
    let points = if (kind == "tube") c.value(p.points,PATH) else p.points
    let checked_points = if (kind == "tube" or kind == "triangle") c.require(points is array and len(points) >= (if (kind == "triangle") 3 else 2) and len(points) <= (if (kind == "triangle") 3 else 128) and all([for (x in points) t.vector(x,3)]),"invalid primitive points")^
    let checked_triangle = if (kind == "triangle") (let n=v.cross(v.subtract(points[1],points[0]),v.subtract(points[2],points[0])),c.require(v.dot(n,n)>1e-16,"triangle points are collinear")^)
    let checked_tube = if (kind == "tube") {
        let checked_caps = c.require(p.capped is bool,"tube capped must be boolean")^
        let edges = [for (i in 0 to (len(points)-2)) v.subtract(points[i+1],points[i])]
        let lengths = c.require(all([for (e in edges) v.dot(e,e)>1e-16]),"tube has duplicate consecutive points")^
        let directions = edges |> v.unit(~);
        c.require(all([for (i in 0 to (len(directions)-2)) v.dot(directions[i],directions[i+1]) > -0.999999]),"tube reverses direction at a joint")^
    }
    let checked_mesh = if (kind == "mesh") c.require(p.geometry is string,"mesh needs a geometry reference")^;
    {*:p,*:(if (points != null) {points:points} else {})}
}
pub fn estimate(kind,p) int => if (kind == "box") 24 else if (kind == "mesh") 0 else if (kind == "sphere") (p.rings+1)*(p.segments+1)
    else if (kind == "triangle") 3 else if (kind == "point") 7*13 else if (kind == "grid") 12*38 else if (kind == "line") 38
    else if (kind == "cylinder") 2*(p.segments+1)+2*(p.segments+2)
    else len(p.points)*(p.segments+1)+(if (p.capped) 2*(p.segments+2) else 0)
// minimal rotation follows the path tangent without introducing an arbitrary ring twist.
fn transport(normal,a,b) array {
    let cross = v.cross(a,b)
    let rotated = v.add(normal,v.add(v.cross(cross,normal),v.times(v.cross(cross,v.cross(cross,normal)),1.0/(1.0+v.dot(a,b)))));
    v.unit(v.subtract(rotated,v.times(b,v.dot(rotated,b))))
}
pub fn frames(points) array {
    let directions = [for (i in 0 to (len(points)-2)) v.unit(v.subtract(points[i+1],points[i]))]
    let tangents = [directions[0],*[for (i in 1 to (len(points)-2)) v.unit(v.add(directions[i-1],directions[i]))],directions[len(directions)-1]]
    let first = v.unit(v.cross(tangents[0],if (abs(tangents[0][1]) < 0.9) [0.0,1.0,0.0] else [0.0,0.0,1.0]));
    reduce([[{t:tangents[0],n:first}],*[for (i in 1 to (len(points)-1)) i]],(acc,i) =>
        [*acc,{t:tangents[i],n:transport(acc[len(acc)-1].n,acc[len(acc)-1].t,tangents[i])}])
}
pub fn arc_lengths(points) array => math.cumsum([0.0,*[for (i in 1 to (len(points)-1),let d=v.subtract(points[i],points[i-1])) sqrt(v.dot(d,d))]])
// a radius array shares plant taper geometry with constant-radius P7 tubes.
pub fn sweep(points,radius,segments,capped,distances=null,path_frames=null) map {
    let frames = if (path_frames == null) frames(points) else path_frames
    let radii = if (radius is array) radius else [for (p in points) radius]
    let along = if (distances != null) distances else arc_lengths(points)
    let rings = [for (i,p in points,let frame=frames[i],let across=v.cross(frame.t,frame.n),
        let lo=max(0,i-1),let hi=min(len(points)-1,i+1),
        let slope=if (radius is array) (radii[hi]-radii[lo])/sqrt(v.dot(v.subtract(points[hi],points[lo]),v.subtract(points[hi],points[lo]))) else 0.0)
        for (j in 0 to segments,let a=2.0*math.pi*j/segments,let normal=v.add(v.times(frame.n,cos(a)),v.times(across,sin(a))))
            {p:v.add(p,v.times(normal,radii[i])),n:if (slope == 0.0) normal else v.unit(v.subtract(normal,v.times(frame.t,slope))),uv:[float(j)/segments,along[i]]}]
    let row = segments+1
    // separate cap vertices retain axial normals without flattening the side shading.
    let caps = if (capped) [for (end in [0,len(points)-1],let normal=v.times(frames[end].t,if (end == 0) -1.0 else 1.0))
        *[{p:points[end],n:normal,uv:[0.5,0.5]},*[for (j in 0 to segments,let a=2.0*math.pi*j/segments)
            {p:rings[end*row+j].p,n:normal,uv:[0.5+0.5*cos(a),0.5+0.5*sin(a)]}]]] else []
    let all_points = [*rings,*caps];
    {positions:[for (q in all_points) *q.p],normals:[for (q in all_points) *q.n],uvs:[for (q in all_points) *q.uv],
     indices:[*[for (i in 0 to (len(points)-2),j in 0 to (segments-1),let a=i*row+j,let b=a+row) *[a,a+1,b,b,a+1,b+1]],
        *(if (capped) [for (end in 0 to 1,let base=len(rings)+end*(segments+2)) for (j in 0 to (segments-1))
            *(if (end == 0) [base,base+j+2,base+j+1] else [base,base+j+1,base+j+2])] else [])]}
}
// concatenate indexed buffers once; offsets depend on vertices, not float entries.
pub fn merge(buffers) map {
    let ends=math.cumsum([for (b in buffers) len(b.positions) div 3]);
    {positions:[for (b in buffers) *b.positions],normals:[for (b in buffers) *b.normals],
     *:(if (len(buffers)>0 and all([for (b in buffers) b.uvs != null])) {uvs:[for (b in buffers) *b.uvs]} else {}),
     indices:[for (i,b in buffers) for (index in b.indices) index+(if (i == 0) 0 else ends[i-1])]}
}
// a curved blade shares the same sampled surface between petals, feathers and fins.
pub fn blade(rows=6,columns=4,bend=0.16,bulge=0.06) map {
    let points=[for (j in 0 to rows,i in 0 to columns,let u=float(j)/rows,let w=2.0*float(i)/columns-1.0)
        {p:[u,bend*u*u+bulge*(1.0-w*w)*sin(math.pi*u),w*sin(math.pi*u)],
         n:v.unit([-2.0*bend*u-bulge*(1.0-w*w)*math.pi*cos(math.pi*u),1.0,2.0*bulge*w]),uv:[float(i)/columns,u]}];
    {positions:[for (point in points) *point.p],normals:[for (point in points) *point.n],uvs:[for (point in points) *point.uv],
     indices:[for (j in 0 to (rows-1),i in 0 to (columns-1),let a=j*(columns+1)+i,let b=a+columns+1) *[a,a+1,b,a+1,b+1,b]]}
}
fn wireframe(stroke) map {
    let corners = [for (x in [-0.5,0.5],y in [-0.5,0.5],z in [-0.5,0.5]) [x,y,z]]
    let edges = [for (i,a in corners,j,b in corners where j>i and len([for (axis in 0 to 2 where a[axis]!=b[axis]) axis]) == 1) [a,b]]
    merge([for (e in edges) sweep(e,stroke/2.0,8,true)])
}
pub fn geometry(kind,p,id) element {
    if (kind == "box") s.box([1.0,1.0,1.0],{id:id})
    else {
        let buffer = if (kind == "sphere") shapes.sphere(p.rings,p.segments)
            else if (kind == "point") (let b=shapes.sphere(6,12),{*:b,positions:b.positions*p.diameter})
            else if (kind == "triangle") (let n=v.unit(v.cross(v.subtract(p.points[1],p.points[0]),v.subtract(p.points[2],p.points[0]))),
                {positions:[for (q in p.points) *q],normals:[for (i in 0 to 2) *n],indices:[0,1,2]})
            else if (kind == "grid") wireframe(p.stroke)
            else sweep(if (kind == "tube") p.points else PATH,if (kind == "tube") p.radius else if (kind == "line") p.stroke/2.0 else 0.5,
                if (kind == "line") 8 else p.segments,kind != "tube" or p.capped);
        s.geometry(buffer.positions,{id:id,normals:buffer.normals,indices:buffer.indices,*:(if (buffer.uvs != null) {uvs:buffer.uvs} else {})})
    }
}
