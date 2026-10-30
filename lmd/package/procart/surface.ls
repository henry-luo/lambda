// stored patterns bake to ordinary Scene3D textures; generation has no renderer effects (PG14 / S12.1.1v3).
import c: ._common
import rng: .rng
import noise: .noise
import rules: .rules
import colors: lambda.color
import image: lambda.image
import media: lambda.doc.media
import native: lambda.scene3d.model
import t: lambda.scene3d.transform
pub let KINDS = ["noise","stripes","marble","bark","leaf-veins","spots","feathers","scales"]
pub let FIELDS = ["surface","surfaces"]
pub fn normalize(definition) map^ {
    let envelope=c.require(definition is element and name(definition)=='pattern' and
        c.fields(definition,["id","generator","version","seed","parameters"]) and c.identifier(definition.id) and
        contains(KINDS,definition.generator) and definition.version is int and definition.version==1 and
        (definition.seed == null or definition.seed is int or definition.seed is string) and len(content(definition))==0,"invalid surface pattern")^
    let raw=c.value(definition.parameters,{})
    let fields=c.require(raw is map and c.fields(raw,["colors","frequency","warp","width"]),"unknown pattern parameter")^
    let p={colors:["#808080","#ffffff"],frequency:6.0,warp:0.35,width:0.06,*:raw}
    let dimensions=c.require(c.bounded(p.frequency,0.1,64.0) and c.bounded(p.warp,0.0,4.0) and
        c.bounded(p.width,0.005,0.5) and p.colors is array and len(p.colors)==2,"invalid pattern parameters")^
    let palette=[for (color in p.colors) colors.parse(color,"pattern palette")^]
    let opaque=c.require(all([for (rgba in palette) rgba[3]==1.0]),"surface pattern colors must be opaque")^;
    {id:definition.id,generator:definition.generator,seed:definition.seed,parameters:p,palette:palette}
}
pub fn binding(raw,definitions) map?^ {
    if (raw == null) null else {
        let input=if (raw is string) {pattern:raw} else raw
        let fields=c.require(input is map and c.fields(input,["pattern","mapping","scale","offset","resolution","roughness","metalness"]),"invalid surface binding")^
        let pattern=rules.lookup(definitions,input.pattern,"pattern")^
        let b={mapping:"uv",scale:1.0,offset:null,resolution:64,roughness:0.85,metalness:0.0,*:input}
        let dimensions=if (b.mapping == "object") 3 else 2
        let scale=if (b.scale is number) [for (i in 1 to dimensions) b.scale] else b.scale
        let offset=c.value(b.offset,[for (i in 1 to dimensions) 0.0])
        let checked=c.require(contains(["uv","leaf","branch","object"],b.mapping) and t.vector(scale,dimensions) and
            min(scale)>0 and max(scale)<=10000 and t.vector(offset,dimensions) and
            c.bounded(b.resolution,8,256,true) and c.bounded(b.roughness,0.0,1.0) and c.bounded(b.metalness,0.0,1.0),"invalid surface mapping or material")^;
        {*:b,scale:scale,offset:offset}
    }
}
pub fn validate(node,definitions) bool^ {
    let whole=binding(node.surface,definitions)^
    let roles=c.value(node.surfaces,{})
    let checked=c.require(roles is map and len(roles)<=32,"surfaces must be a role map with at most 32 entries")^
    let bindings=[for (role,raw in roles) {
        let valid=c.require(c.identifier(string(role)),"invalid surface role")^;
        binding(raw,definitions)^
    }]; true
}
pub fn inherit(parent,node) map => {surface:if ("surface" at node) node.surface else parent.surface,
    surfaces:{*:c.value(parent.surfaces,{}),*:c.value(node.surfaces,{})}}
pub fn select(context,role) => if (role at context.surfaces) context.surfaces[role] else context.surface
pub fn prepare(definition,seed) map^ {
    let p=normalize(definition)^
    let key=rng.derive(rng.key(c.value(p.seed,seed)),["surface",p.id]);
    {*:p,table:noise.table(key)}
}
fn clamp(x) float => max(0.0,min(1.0,x))
fn fraction(x) float => x-floor(x)
// periodic UV noise embeds the circumference on a circle; quintic blending closes the other seam.
fn field(p,q,periodic) float {
    let f=p.parameters.frequency;
    if (not periodic) noise.value3(p.table,float(q[0]*f),float(q[1]*f),float(q[2]*f))
    else {
        let u=q[0],v=fraction(q[1]),a=2.0*math.pi*u
        let x=cos(a)*f,y=sin(a)*f,z=v*f,w=v*v*v*(v*(v*6.0-15.0)+10.0);
        (1.0-w)*noise.value3(p.table,float(x),float(y),float(z))+w*noise.value3(p.table,float(x),float(y),float(z-f))
    }
}
pub fn sample(pattern,coordinate,periodic=false) array {
    let p=pattern.parameters,q=coordinate
    let n=field(pattern,q,periodic)
    let phase=q[0]*p.frequency+p.warp*n
    let signal=if (pattern.generator == "noise") 0.5+0.5*n
        else if (pattern.generator == "stripes") 0.5+0.5*sin(2.0*math.pi*phase)
        else if (pattern.generator == "marble") min(1.0,abs(sin(math.pi*phase))/p.width)
        else if (pattern.generator == "bark") (abs(sin(math.pi*phase))**0.35)*(0.8+0.2*n)
        else if (pattern.generator == "spots") clamp((abs(n)-p.width)*8.0+0.5)
        else if (pattern.generator == "feathers") {
            let across=abs(fraction(q[0]*p.frequency)-0.5)
            let bar=abs(sin(math.pi*(q[1]*p.frequency-across*1.7+p.warp*n)));
            clamp(bar/(p.width*3.0))*clamp(across/(p.width*0.7))
        }
        else if (pattern.generator == "scales") {
            let row=floor(q[1]*p.frequency),u=fraction(q[0]*p.frequency+0.5*(row%2.0))-0.5
            let z=fraction(q[1]*p.frequency),rim=abs(sqrt(u*u+(z-0.15)*(z-0.15))-0.55);
            clamp(rim/(p.width*1.5))
        }
        else {
            let u=fraction(q[0]),v=fraction(q[1]),distance=abs(u-0.5)
            let ribs=abs(sin(math.pi*(v*p.frequency-distance*2.0)));
            min(clamp(distance/p.width),clamp(ribs/(p.width*3.0)))
        }
    let amount=clamp(signal);
    [for (i in 0 to 2) pattern.palette[0][i]+amount*(pattern.palette[1][i]-pattern.palette[0][i])]
}
fn point(buffer,index) array => slice(buffer.positions,index*3,index*3+3)
fn indices(buffer) array => c.value(buffer.indices,[for (i in 0 to (len(buffer.positions) div 3-1)) i])
fn plan(part,b,definition,geometry,seed) map^ {
    let atlas=b.mapping == "object"
    let mapped=not atlas and (b.scale!=[1,1] or b.offset!=[0,0])
    let coords=c.require((atlas and geometry.type=='buffer') or (not atlas and
        (geometry.uvs != null or contains(['box','plane'],geometry.type))),"surface mapping requires buffer positions or UV coordinates")^
    let role=c.require((b.mapping != "leaf" or part.role == "leaf") and (b.mapping != "branch" or part.role == "bark"),"surface mapping does not match organ role")^
    let transform=c.require(not mapped or (geometry.type=='buffer' and geometry.uvs != null),"UV scale/offset requires explicit buffer UVs")^
    let faces=if (atlas) indices(geometry) else []
    let tris=len(faces) div 3
    let count=c.require(not atlas or (tris>0 and tris<=4096),"object surface atlas needs 1..4096 triangles")^
    let columns=if (atlas) int(ceil(sqrt(float(tris)))) else 1
    let tile=if (atlas) max(4,int(floor(float(b.resolution)/columns))) else b.resolution
    let rows=if (atlas) int(ceil(float(tris)/columns)) else 1
    let id="surface-"++c.fingerprint([definition,c.value(definition.seed,seed),b,*(if (atlas or mapped) [geometry] else [])]);
    {id:id,binding:b,definition:definition,geometry:geometry,indices:faces,atlas:atlas,mapped:mapped,columns:columns,tile:tile,
        width:columns*tile,height:rows*tile,vertices:if (atlas) tris*3 else if (mapped) len(geometry.positions) div 3 else 0}
}
// one padded triangle per atlas tile; reflected barycentrics keep both sides of the diagonal filled.
fn atlas_point(job,x,y) array {
    let tile=job.tile,index=(y div tile)*job.columns+(x div tile)
    let ids=job.indices
    let triangle=min(index,len(ids) div 3-1)
    let u=clamp((float(x%tile)-1.0)/float(tile-3)),v=clamp((float(y%tile)-1.0)/float(tile-3))
    let a=if (u+v>1.0) 1.0-v else u,b=if (u+v>1.0) 1.0-u else v
    let points=[for (i in 0 to 2) point(job.geometry,ids[triangle*3+i])];
    [for (i in 0 to 2) points[0][i]*(1.0-a-b)+points[1][i]*a+points[2][i]*b]
}
fn texture(job,seed) element^ {
    let p=prepare(job.definition,seed)^
    let b=job.binding
    // image rows start at the top; native texture UVs start at the bottom.
    let pixels=reshape([for (row in 0 to (job.height-1),x in 0 to (job.width-1),let y=job.height-1-row,
        let point=if (job.atlas) atlas_point(job,x,y) else [(float(x)+0.5)/job.width,(float(y)+0.5)/job.height],
        let q=if (job.atlas) [for (i,value in point) value/b.scale[i]+b.offset[i]] else point,
        let rgb=sample(p,q,not job.atlas)) *[for (channel in [*rgb,1.0]) u8(round(clamp(channel)*255.0))]], [job.height,job.width,4])
    let bytes=image.encode(image.from_array(pixels)^,{format:'png'})^;
    <texture id:job.id,src:media.data_uri(bytes,"image/png")^,'wrap-s':(if (job.atlas) 33071 else 10497),
        'wrap-t':(if (job.atlas) 33071 else 10497),'min-filter':9729,'mag-filter':9729,'color-space':'srgb'>
}
fn atlas_geometry(job) element {
    let source=job.geometry,ids=job.indices
    let uvs=[for (i in 0 to (len(ids)-1),let triangle=i div 3,let corner=i%3,
        let x=(triangle%job.columns)*job.tile+1.5+(if (corner==1) job.tile-3 else 0),
        let y=(triangle div job.columns)*job.tile+1.5+(if (corner==2) job.tile-3 else 0)) *[x/job.width,y/job.height]];
    <geometry id:job.id++"-geometry",type:'buffer',positions:[for (i in ids) *point(source,i)],uvs:uvs,
        *:map([for (key,width in {normals:3,colors:3,tangents:4} where source[key] != null)
            (string(key),[for (i in ids) *slice(source[key],i*width,i*width+width)])])>
}
fn mapped_geometry(job) element => <geometry *:{*:map(job.geometry),id:job.id++"-geometry",
    uvs:[for (i,value in job.geometry.uvs) value/job.binding.scale[i%2]+job.binding.offset[i%2]]}>
// deduplicate bake jobs before admitting their total pixel/vertex cost; unbound models stay unchanged.
fn bake_plan(model,definitions,seed,limits) map^ {
    let geometry=map([for (g in model.resources where name(g)=='geometry') (g.id,g)])
    let requests=[for (i,part in model.parts where part.surface != null) {
        let b=binding(part.surface,definitions)^
        let definition=rules.lookup(definitions,b.pattern,"pattern")^;
        {index:i,job:plan(part,b,definition,geometry[part.geometry],seed)^}
    }]
    let jobs=[for (request in requests group by request.job.id as id into batch) batch[0].job]
    let pixels=sum([for (job in jobs) job.width*job.height]),vertices=sum(jobs |> ~.vertices)
    let budget=c.require(pixels<=limits.max_surface_pixels and model.stats.vertices+vertices<=limits.max_vertices,"surface pixel/vertex budget exceeded")^;
    {model:model,seed:seed,jobs:jobs,requests:requests,pixels:pixels,vertices:vertices}
}
fn finish_bake(plan,baked) map^ {
    let model=plan.model,jobs=plan.jobs,requests=plan.requests,pixels=plan.pixels,vertices=plan.vertices
    let replacements=map([for (r in requests) (string(r.index),r.job)])
    let parts=[for (i,part in model.parts,let job=replacements[string(i)]) if (job == null) part else
        {*:part,geometry:if (job.atlas or job.mapped) job.id++"-geometry" else part.geometry,
            surface:{texture:job.id,roughness:job.binding.roughness,metalness:job.binding.metalness}}]
    let resources=[*model.resources,*baked,*[for (job in jobs where job.atlas or job.mapped)
        if (job.atlas) atlas_geometry(job) else mapped_geometry(job)]]
    let checked=[for (g in resources where name(g)=='geometry') native.check_geometry(g)^];
    if (len(jobs)==0) model else {*:model,resources:resources,parts:parts,
        stats:{*:model.stats,vertices:model.stats.vertices+vertices,surface_pixels:pixels,surface_textures:len(jobs)}}
}

// identical UV textures recur across species; admit every model, then bake each job once.
pub fn bake_many(inputs) array^ {
    let plans=[for (item in inputs) bake_plan(item.model,item.definitions,item.seed,item.limits)^]
    let baked=map([for (item in [for (plan in plans) for (job in plan.jobs) {job:job,seed:plan.seed}]
        group by item.job.id as id into batch) (id,texture(batch[0].job,batch[0].seed)^)]);
    [for (plan in plans) finish_bake(plan,[for (job in plan.jobs) baked[job.id]])^]
}
pub fn bake(model,definitions,seed,limits) map^ =>
    bake_many([{model:model,definitions:definitions,seed:seed,limits:limits}])^[0]
