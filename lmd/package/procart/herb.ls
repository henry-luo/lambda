// PG15: compact flowering species share organ meshes and environment-driven poses.
import c: ._common
import rng: .rng
import env: .environment
import t: lambda.scene3d.transform
import s: lambda.scene3d
import color: lambda.color
import v: lambda.scene3d.camera

pub let SPECIES = ["sunflower","water-lily","daisy","poppy","cosmos","reed"]
let PROFILES = {sunflower:{height:1.7,size:0.42,petals:16,petal:"#efc64d",center:"#59442b"},
    'water-lily':{height:0.14,size:0.4,petals:18,petal:"#f0c8d4",center:"#e8bd58"},
    daisy:{height:0.65,size:0.2,petals:12,petal:"#f6efdb",center:"#e4b847"},
    poppy:{height:0.8,size:0.3,petals:6,petal:"#db6653",center:"#4e3940"},
    cosmos:{height:0.9,size:0.26,petals:8,petal:"#e7a1b4",center:"#e4ba64"},
    reed:{height:1.5,size:0.15,petals:0,petal:"#775238",center:"#775238"}}
pub fn normalize(raw={}) map^ {
    let checked=c.require(raw is map and c.fields(raw,["species","height","size","petals","petal","center","leaf","stem"]),"unknown herb parameter")^
    let species=c.value(raw.species,"daisy")
    let known=c.require(contains(SPECIES,species),"unknown herb species")^
    let p={leaf:"#678b50",stem:"#537346",*:PROFILES[species],*:raw,species:species}
    let dimensions=c.require(c.bounded(p.height,0.05,4.0) and c.bounded(p.size,0.03,1.0) and c.bounded(p.petals,0,32,true),"invalid herb dimensions or petals")^
    let colors=[for (value in [p.leaf,p.stem,p.petal,p.center]) color.parse(value)^];
    p
}
fn petal() element {
    let rows=6,columns=4
    let points=[for (j in 0 to rows,i in 0 to columns,let u=float(j)/rows,let w=2.0*float(i)/columns-1.0)
        {p:[u,0.16*u*u+0.06*(1.0-w*w)*sin(math.pi*u),w*sin(math.pi*u)],
         n:v.unit([-0.32*u-0.06*(1.0-w*w)*math.pi*cos(math.pi*u),1.0,0.12*w]),uv:[float(i)/columns,u]}];
    s.geometry([for (point in points) *point.p],{id:"herb-petal",normals:[for (point in points) *point.n],uvs:[for (point in points) *point.uv],
        // wind toward the supplied upper normals so double-sided shading keeps petal tops lit.
        indices:[for (j in 0 to (rows-1),i in 0 to (columns-1),let a=j*(columns+1)+i,let b=a+columns+1) *[a,a+1,b,a+1,b+1,b]]})
}
fn pad() element {
    let segments=36
    let points=[[0.0,0.0,0.0],*[for (i in 0 to segments,let a=0.18+(2.0*math.pi-0.36)*float(i)/segments) [cos(a),0.025,sin(a)]]];
    s.geometry([for (p in points) *p],{id:"herb-pad",normals:[for (p in points) *[0.0,1.0,0.0]],
        uvs:[for (p in points) *[0.5+0.5*p[0],0.5+0.5*p[2]]],indices:[for (i in 1 to segments) *[0,i+1,i]]})
}
pub fn resources() array => [s.sphere(8,12,{id:"herb-round"}),petal(),pad()]
fn piece(shape,position,scale,color,role,rotation=[0.0,0.0,0.0]) map =>
    {shape:shape,geometry:shape,matrix:t.matrix(position,rotation,scale)^,color:color,role:role}
fn oval(position,radii,color,role,rotation=[0.0,0.0,0.0]) map => piece("herb-round",position,radii*2.0,color,role,rotation)
fn translated(part,parent) map => {*:part,matrix:t.multiply(parent,part.matrix)^}

pub fn generate(k:u64,parameters={},environment=null,frame=c.identity()) map^ {
    let p=normalize(parameters)^,e=if (environment == null) env.sample()^ else environment
    let response=env.response(p.species,e)
    let height=p.height*rng.between(rng.derive(k,"height"),0.9,1.1),size=p.size*rng.between(rng.derive(k,"size"),0.9,1.1)
    let phase=rng.between(rng.derive(k,"phase"),0.0,2.0*math.pi)
    let lily=p.species=="water-lily",reed=p.species=="reed"
    let stalks=if (lily) [] else [oval([0.0,height/2.0,0.0],[0.018,height/2.0,0.018],p.stem,"stem")]
    let leaves=if (lily) [for (i in 0 to 1,let a=phase+float(i)*2.3)
        piece("herb-pad",[cos(a)*0.35,0.01+float(i)*0.012,sin(a)*0.35],[0.55,1.0,0.45],p.leaf,"leaf",[0.0,a,0.0])]
        else [for (i in 0 to (if (reed) 4 else 3),let a=phase+float(i)*2.4,let leaf_length=if (reed) 0.7 else 0.32)
            piece("herb-petal",[0.0,height*(0.18+float(i)*0.12),0.0],
                [leaf_length,if (reed) 0.16 else 0.45,if (reed) 0.03 else 0.12],p.leaf,"leaf",[0.0,-a,0.25-response.droop])]
    let head=t.multiply(t.matrix([0.0,height,0.0],[0.0,0.0,0.0],[1.0,1.0,1.0])^,env.head(response.direction,frame)^)^
    let blooms=if (reed) [oval([0.0,height*0.94,0.0],[0.065,0.24,0.065],p.center,"seedhead")]
        else [*[for (layer in 0 to (if (lily) 1 else 0),i in 0 to (p.petals-1),
            let a=phase+float(i)*2.0*math.pi/float(p.petals)+float(layer)*0.18,
            let tilt=(1.0-response.openness)*1.3+0.12+float(layer)*0.18,
            let length=size*(1.0-float(layer)*0.18)*rng.between(rng.at(rng.derive(k,"petal"),i),0.9,1.1),
            let radius=size*0.16*response.openness)
            translated(piece("herb-petal",[radius*cos(a),0.02*float(layer),radius*sin(a)],
                [length,length*0.65,length*(if (p.species=="sunflower") 0.22 else if (p.species=="poppy") 0.85 else 0.4)],
                p.petal,"petal",[0.0,-a,tilt]),head)],
            translated(oval([0.0,0.04,0.0],[size*0.3,size*0.12,size*0.3],p.center,"center"),head)];
    {parts:[*stalks,*leaves,*blooms],resources:resources(),species:p.species,response:response}
}
fn validate_request(request) bool^ {
    let p=normalize(request.parameters)^
    let checked=c.require(request.kind == (if (p.species=="reed") "grass" else "flower"),"herb species does not match semantic tag")^;
    true
}
fn generate_request(request) map^ => generate(request.key,request.parameters,request.environment,c.value(request.frame,c.identity()))^
pub fn registry() map => {'plant.herb@1':{validate:validate_request,generate:generate_request}}
