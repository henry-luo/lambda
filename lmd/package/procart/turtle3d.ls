// spatial turtle emits architecture, leaving tessellation to plant.mesh (PG13.3).
import c: ._common
import lsys: .lsystem
import v: lambda.scene3d.camera
import t: lambda.scene3d.transform
import colors: lambda.color

pub fn frame(position=[0.0,0.0,0.0]) map => {position:position,h:[0.0,1.0,0.0],l:[-1.0,0.0,0.0],u:[0.0,0.0,1.0]}
pub fn matrix(f,length=1.0,width=1.0) array => [for (axis in 0 to 3)
    *(if (axis == 0) [*v.times(f.l,0.0-width),0.0] else if (axis == 1) [*v.times(f.h,length),0.0]
      else if (axis == 2) [*f.u,0.0] else [*f.position,1.0])]
pub fn turned(f,axis,degrees) map => {*:f,h:v.rotate(f.h,axis,degrees*math.pi/180.0),
    l:v.rotate(f.l,axis,degrees*math.pi/180.0),u:v.rotate(f.u,axis,degrees*math.pi/180.0)}
// reproject the transverse axis after bending to keep the frame orthonormal.
pub fn directed(f,heading) map {
    let h=v.unit(heading)
    let across=v.subtract(f.l,v.times(h,v.dot(f.l,h)))
    let l=if (v.dot(across,across)>1e-12) v.unit(across) else v.unit(v.cross(f.u,h));
    {*:f,h:h,l:l,u:v.cross(h,l)}
}
pub fn force(raw={}) map^ {
    let checked=c.require(raw is map and c.fields(raw,["direction","strength"]),"invalid plant force")^
    let direction=c.value(raw.direction,[0.0,1.0,0.0]),strength=c.value(raw.strength,0.0)
    let dimensions=c.require(t.vector(direction,3) and c.bounded(strength,0.0,2.0) and
        (strength == 0 or v.dot(direction,direction)>1e-12),"invalid force direction or strength")^;
    {direction:if (strength == 0) [0.0,1.0,0.0] else v.unit(direction),strength:strength}
}
pub fn bent(f,force,length) map => if (force.strength == 0) f else {
    let wanted=v.add(f.h,v.times(force.direction,force.strength*length));
    // exact cancellation has no preferred orientation; preserve the current frame.
    if (v.dot(wanted,wanted)<1e-12) f else directed(f,wanted)
}
fn parameters(m) array => if (m is array) slice(m,1,len(m)) else []
fn command(st,item,options) map^ {
    let sym=lsys.symbol_of(item.module),p=parameters(item.module),f=st.frame
    let arg=if (len(p)>0) p[0] else options.angle;
    if (sym == "[") {*:st,stack:[*st.stack,{frame:f,radius:st.radius,parent:st.parent}]}
    else if (sym == "]") {
        let saved=st.stack[len(st.stack)-1];
        {*:st,frame:saved.frame,radius:saved.radius,parent:saved.parent,stack:slice(st.stack,0,len(st.stack)-1)}
    }
    else if (sym == "F" or sym == "f") {
        let length=if (len(p)>0) p[0] else options.step
        let radius=if (len(p)>1) p[1] else st.radius
        let check=c.require(c.bounded(length,0.00001,10000.0) and c.bounded(radius,0.00001,1000.0) and len(p)<=2,"invalid 3D forward parameters")^
        let next=bent(f,options.force,length),end=v.add(f.position,v.times(next.h,length)),id="segment-"++string(item.index);
        {*:st,frame:{*:next,position:end},radius:radius,parent:if (sym == "F") id else st.parent,
         branches:if (sym == "f") st.branches else [*st.branches,{id:id,parent:st.parent,level:len(st.stack),points:[f.position,end],radii:[st.radius,radius]}]}
    }
    else if (contains(["+","-","&","^","/","\\","|"],sym)) {
        let checked=c.require(c.bounded(arg,-36000.0,36000.0) and len(p)<=1,"invalid 3D turn")^
        let axis=if (sym == "+" or sym == "-" or sym == "|") f.u else if (sym == "&" or sym == "^") f.l else f.h
        let angle=if (sym == "|") 180.0 else arg*(if (sym == "-" or sym == "^" or sym == "\\") -1.0 else 1.0);
        {*:st,frame:turned(f,axis,angle)}
    }
    else if (sym == "!") {
        let check=c.require(len(p)==1 and c.bounded(p[0],0.00001,1000.0),"radius command needs one positive radius")^;
        {*:st,radius:p[0]}
    }
    else if (sym == "L") {
        let length=if (len(p)>0) p[0] else options.leaf_length,width=if (len(p)>1) p[1] else options.leaf_width
        let check=c.require(len(p)<=2 and c.bounded(length,0.00001,1000.0) and c.bounded(width,0.00001,1000.0),"invalid turtle leaf dimensions")^;
        {*:st,leaves:[*st.leaves,{id:"leaf-"++string(item.index),parent:st.parent,frame:f,length:length,width:width,color:options.leaf_color}]}
    }
    else st
}
pub fn skeleton(sequence,options={}) map^ {
    let inputs=c.require((sequence is string or sequence is array) and options is map and
        c.fields(options,["step","angle","radius","start","force","bark","leaf_color","leaf_length","leaf_width"]),"invalid 3D turtle input")^
    let modules=lsys.modules_of(sequence)
    let valid=c.require(len(modules)<=20000 and all([for (m in modules) m is string or (m is array and len(m)>0 and m[0] is string)]),"invalid 3D turtle modules")^
    let syms=modules |> lsys.symbol_of(~),depth=math.cumsum([for (s in syms) if (s == "[") 1 else if (s == "]") -1 else 0])
    let brackets=c.require(len(depth)==0 or (min(depth)>=0 and max(depth)<=128 and depth[len(depth)-1]==0),"invalid 3D turtle brackets or depth")^
    let counts=c.require(len([for (s in syms where s == "F") s])<=512 and len([for (s in syms where s == "L") s])<=8192,"3D turtle skeleton budget exceeded")^
    let opt={step:1.0,angle:25.0,radius:0.06,start:[0.0,0.0,0.0],bark:"#806247",leaf_color:"#719258",leaf_length:0.45,leaf_width:0.22,*:options,force:force(c.value(options.force,{}))^}
    let dims=c.require(c.bounded(opt.step,0.00001,10000.0) and c.bounded(opt.angle,-36000,36000) and c.bounded(opt.radius,0.00001,1000) and
        c.bounded(opt.leaf_length,0.00001,1000) and c.bounded(opt.leaf_width,0.00001,1000) and t.vector(opt.start,3),"invalid 3D turtle settings")^
    let bark=colors.parse(opt.bark)^,leaf=colors.parse(opt.leaf_color)^
    let result=reduce([{frame:frame(opt.start),radius:opt.radius,parent:null,branches:[],leaves:[],stack:[]},
        *[for (i,m in modules) {index:i,module:m}]],(st,item) => command(st,item,opt)^)^;
    {branches:result.branches,leaves:result.leaves,bark:opt.bark,leaf_shape:"broad",end:result.frame}
}
