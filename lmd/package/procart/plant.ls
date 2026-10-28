// skeleton-first plant architecture and meshing (PG13); generation stays pure (S12.1.1v3).
import c: ._common
import rng: .rng
import turtle: .turtle3d
import primitive: .primitive
import v: lambda.scene3d.camera
import s: lambda.scene3d
import colors: lambda.color
import apex: .apex
import adjust: .adjust

let LEVEL = {length:1.0,radius:0.04,children:0,start:0.35,angle:45.0,taper:0.85,sections:6,gnarliness:4.0,twist:137.507764,length_falloff:0.0}
let LEVELS = [{length:3.8,radius:0.19,children:7,start:0.38,angle:0.0,taper:0.9,sections:10,gnarliness:2.0},
    {length:1.55,radius:0.065,children:4,angle:52.0,taper:0.86},
    {length:0.65,radius:0.022,children:0,angle:48.0,taper:0.9}]
let LEAVES = {count:14,start:0.15,length:0.42,width:0.23,angle:58.0,size_variance:0.2,shape:"broad",colors:["#769153","#8d9f5b","#607f4b"]}

pub fn normalize(parameters={}) map^ {
    let outer=c.require(parameters is map and c.fields(parameters,["levels","leaves","bark","force","variation"]),"unknown tree parameter")^
    let levels=c.value(parameters.levels,LEVELS)
    let count=c.require(levels is array and len(levels)>=1 and len(levels)<=5,"tree needs 1..5 branch levels")^
    let normalized=[for (raw in levels) {
        let check=c.require(raw is map and c.fields(raw,[for (key,val in LEVEL) string(key)]),"unknown branch level field")^
        let p={*:LEVEL,*:raw}
        let fields=c.require(c.bounded(p.length,0.01,100.0) and c.bounded(p.radius,0.0001,5.0) and c.bounded(p.children,0,16,true) and
            c.bounded(p.start,0.0,0.95) and c.bounded(p.angle,0.0,175.0) and c.bounded(p.taper,0.0,0.98) and
            c.bounded(p.sections,2,24,true) and c.bounded(p.gnarliness,0.0,30.0) and c.bounded(p.twist,-360.0,360.0) and
            c.bounded(p.length_falloff,0.0,0.95),"invalid branch level")^;
        p
    }]
    let topology=c.require(normalized[len(normalized)-1].children == 0 and all([for (i,p in normalized where i<len(normalized)-1) p.children>0]),"only the final branch level may be terminal")^
    let leaf_raw=c.value(parameters.leaves,{})
    let leaf_fields=c.require(leaf_raw is map and c.fields(leaf_raw,[for (key,val in LEAVES) string(key)]),"unknown leaf field")^
    let leaves={*:LEAVES,*:leaf_raw}
    let leaf_dims=c.require(c.bounded(leaves.count,0,128,true) and c.bounded(leaves.start,0.0,1.0) and
        c.bounded(leaves.length,0.001,10.0) and c.bounded(leaves.width,0.001,10.0) and c.bounded(leaves.angle,0,175) and
        c.bounded(leaves.size_variance,0.0,0.9) and contains(["broad","needle"],leaves.shape) and
        leaves.colors is array and len(leaves.colors)>0 and len(leaves.colors)<=8,"invalid tree leaves")^
    let leaf_colors=[for (color in leaves.colors) colors.parse(color)^]
    let bark=c.value(parameters.bark,"#806247"),bark_color=colors.parse(bark)^
    let variation=c.value(parameters.variation,0.18)
    let vary=c.require(c.bounded(variation,0.0,0.5),"invalid tree variation")^
    let counts=math.cumprod([1,*[for (p in slice(normalized,0,len(normalized)-1)) p.children]])
    let budget=c.require(sum(counts)<=512 and counts[len(counts)-1]*leaves.count<=8192,"tree skeleton budget exceeded")^;
    {levels:normalized,leaves:leaves,bark:bark,variation:variation,force:turtle.force(c.value(parameters.force,{}))^}
}

// sample the original centerline, so attachment positions do not depend on tessellation.
fn sample(branch,fraction) map {
    let x=fraction*float(len(branch.points)-1),index=min(len(branch.points)-2,int(floor(x))),u=x-float(index)
    let a=branch.points[index],b=branch.points[index+1]
    let f=primitive.frames(branch.points)[index]
    let h=v.unit(v.subtract(b,a)),l=v.unit(v.cross(f.n,h));
    {frame:{position:v.add(a,v.times(v.subtract(b,a),u)),h:h,l:l,u:v.cross(h,l)},
     radius:branch.radii[index]+(branch.radii[index+1]-branch.radii[index])*u}
}
fn branch(k:u64,parameters,level:int,id,parent,initial,radius,length_factor=1.0) array {
    let p=parameters.levels[level],key=rng.derive(k,id)
    let length=p.length*length_factor*rng.between(rng.derive(key,"length"),1.0-parameters.variation,1.0+parameters.variation)
    let step=length/float(p.sections)
    let grown=reduce([[initial],*[for (i in 1 to p.sections) i]],(acc,i) => {
        let previous=acc[len(acc)-1]
        let wobble=rng.at(rng.derive(key,"curve"),i)
        let yaw=rng.between(wobble,0.0-p.gnarliness,p.gnarliness)
        let pitch=rng.between(rng.at(wobble,1),0.0-p.gnarliness,p.gnarliness)
        let tilted=turtle.turned(turtle.turned(previous,previous.u,yaw),previous.l,pitch)
        let next=turtle.bent(tilted,parameters.force,step);
        [*acc,{*:next,position:v.add(previous.position,v.times(next.h,step))}]
    })
    let result={id:id,parent:parent,level:level,points:grown |> ~.position,
        radii:[for (i in 0 to p.sections) radius*(1.0-p.taper*float(i)/float(p.sections))]};
    [result,*[for (j in 0 to (p.children-1),let fraction=p.start+(0.95-p.start)*(float(j)+0.5)/float(p.children),let attachment=sample(result,fraction),
        let spin=rng.between(rng.derive(key,"azimuth"),0.0,360.0)+float(j)*p.twist,
        let rolled=turtle.turned(attachment.frame,attachment.frame.h,spin),
        let child=turtle.turned(rolled,rolled.l,parameters.levels[level+1].angle))
        *branch(k,parameters,level+1,id++"-"++string(j),id,child,min(parameters.levels[level+1].radius,attachment.radius*0.85),1.0-p.length_falloff*fraction)]]
}
pub fn skeleton(k:u64,parameters={}) map^ {
    let p=normalize(parameters)^
    let branches=branch(k,p,0,"root",null,turtle.frame(),p.levels[0].radius)
    let leaves=[for (b in branches where b.level == len(p.levels)-1)
        for (i in 0 to (p.leaves.count-1),let key=rng.at(rng.derive(k,[b.id,"leaves"]),i),
            let at=sample(b,p.leaves.start+(1.0-p.leaves.start)*(float(i)+0.5)/float(p.leaves.count)),
            let rolled=turtle.turned(at.frame,at.frame.h,float(i)*137.507764+rng.between(rng.derive(key,"turn"),-16.0,16.0)),
            let f=turtle.turned(rolled,rolled.l,p.leaves.angle),let size=rng.between(key,1.0-p.leaves.size_variance,1.0+p.leaves.size_variance))
            {id:b.id++"-leaf-"++string(i),parent:b.id,frame:f,length:p.leaves.length*size,width:p.leaves.width*size,
             color:rng.pick(rng.derive(key,"color"),p.leaves.colors)}];
    {branches:branches,leaves:leaves,bark:p.bark,leaf_shape:p.leaves.shape,parameters:p}
}

// a folded diamond supplies volume and stable normals without texture assets.
fn leaf_buffer(shape) map {
    let width=if (shape == "needle") 0.18 else 0.5
    let points=[[0.0,0.0,0.0],[width,0.45,0.0],[0.0,1.0,0.0],[0.0-width,0.45,0.0],[0.0,0.45,0.08]]
    let triangles=[[0,1,4],[1,2,4],[2,3,4],[3,0,4]];
    primitive.merge([for (tri in triangles) {
        let a=points[tri[0]],b=points[tri[1]],d=points[tri[2]],normal=v.unit(v.cross(v.subtract(b,a),v.subtract(d,a)));
        {positions:[*a,*b,*d],normals:[*normal,*normal,*normal],indices:[0,1,2]}
    }])
}
pub fn mesh(skeleton,detail={}) map^ {
    let checked=c.require(detail is map and c.fields(detail,["segments","section_stride","leaf_stride","leaf_scale"]),"unknown plant detail")^
    let d={segments:8,section_stride:1,leaf_stride:1,leaf_scale:1.0,*:detail}
    let dimensions=c.require(c.bounded(d.segments,3,32,true) and c.bounded(d.section_stride,1,24,true) and
        c.bounded(d.leaf_stride,1,64,true) and c.bounded(d.leaf_scale,0.1,4.0),"invalid plant detail")^
    let sampled=[for (b in skeleton.branches) {
        let keep=[for (i in 0 to (len(b.points)-1) where i%d.section_stride == 0 or i == len(b.points)-1) i];
        {points:[for (i in keep) b.points[i]],radii:[for (i in keep) b.radii[i]]}
    }]
    let leaves=[for (i,l in skeleton.leaves where i%d.leaf_stride == 0) l]
    let vertices=sum([for (b in sampled) len(b.points)*(d.segments+1)+2*(d.segments+2)])+(if (len(leaves)>0) 12 else 0)
    let budget=c.require(vertices<=250000,"plant mesh vertex budget exceeded")^
    let bark=primitive.merge([for (b in sampled) primitive.sweep(b.points,b.radii,d.segments,true)])
    let leaf=leaf_buffer(skeleton.leaf_shape)
    let id="plant-"++string(hash(format([bark,skeleton.leaf_shape],'json')^,'sha256'))
    let resources=[*(if (len(sampled)>0) [s.geometry(bark.positions,{id:id++"-bark",normals:bark.normals,indices:bark.indices})] else []),
        *(if (len(leaves)>0) [s.geometry(leaf.positions,{id:id++"-leaf",normals:leaf.normals,indices:leaf.indices})] else [])]
    let bark_paint=adjust.paint([],skeleton.bark)^;
    {resources:resources,parts:[*(if (len(sampled)>0) [{id:"bark",geometry:id++"-bark",shape:id++"-bark",matrix:c.identity(),*:bark_paint,role:"bark",kind:"tree"}] else []),
        *[for (l in leaves) {id:l.id,geometry:id++"-leaf",shape:id++"-leaf",matrix:turtle.matrix(l.frame,l.length*d.leaf_scale,l.width*d.leaf_scale),
            *:adjust.paint([],l.color)^,role:"leaf",kind:"tree"}]],
     skeleton:skeleton,detail:d,stats:{branches:len(skeleton.branches),leaves:len(leaves),vertices:vertices,objects:(if (len(sampled)>0) 1 else 0)+len(leaves)}}
}
pub fn lods(skeleton,details) array^ {
    let checked=c.require(details is array and len(details)>0 and len(details)<=8,"plant LODs need 1..8 detail maps")^;
    [for (detail in details) mesh(skeleton,detail)^]
}
pub fn generate(k:u64,parameters={}) map^ => mesh(skeleton(k,parameters)^)^
// a standalone specimen stores generation only; cameras and detail stay caller-owned.
pub fn architecture(document) map^ {
    let envelope=c.require(document is element and string(name(document)) == "tree" and
        c.fields(document,["version","generator","generator_version","seed","parameters"]) and len(content(document)) == 0 and
        document.version is int and document.version == 1 and document.generator_version is int and document.generator_version == 1 and
        document.parameters is map and (document.seed is int or document.seed is string) and
        contains(["plant.tree","plant.apex"],document.generator),"invalid stored tree specimen")^;
    if (document.generator == "plant.tree") skeleton(rng.key(document.seed),document.parameters)^
    else apex.skeleton(rng.key(document.seed),document.parameters)^
}
pub fn from_document(document,detail={}) map^ => mesh(architecture(document)^,detail)^
fn validate_request(request) bool^ {
    let kind=c.require(request.kind == "tree","plant.tree needs a tree tag")^
    let checked=normalize(request.parameters)^;
    true
}
fn generate_request(request) map^ => generate(request.key,request.parameters)^
fn validate_apex(request) bool^ {
    let kind=c.require(request.kind == "tree","plant.apex needs a tree tag")^
    let checked=apex.normalize(request.parameters)^;
    true
}
fn generate_apex(request) map^ => mesh(apex.skeleton(request.key,request.parameters)^)^
pub fn registry() map => {'plant.tree@1':{validate:validate_request,generate:generate_request},
    'plant.apex@1':{validate:validate_apex,generate:generate_apex}}
