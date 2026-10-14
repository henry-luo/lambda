// Shared pure projection, clipping and ray queries; native scene3d uses the same RH/Y-up convention.
import t: .transform

pub fn dot(a,b) => sum([for (i in 0 to 2) a[i]*b[i]])
pub fn cross(a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
pub fn add(a,b) => [for (i in 0 to 2) a[i]+b[i]]
pub fn subtract(a,b) => [for (i in 0 to 2) a[i]-b[i]]
pub fn times(a,s) => [for (i in 0 to 2) a[i]*s]
pub fn unit(a) any | error => times(a,1.0/sqrt(dot(a,a)))
fn option(options,key,fallback) => if (options[key]!=null) options[key] else fallback

pub fn prepare(options={},width=640,height=440) {
    let camera={type:option(options,"type","perspective"),position:option(options,"position",[3.0,2.0,4.0]),
        target:option(options,"target",[0.0,0.0,0.0]),up:option(options,"up",[0.0,1.0,0.0]),
        fov:option(options,"fov",45.0),near:option(options,"near",0.01),far:option(options,"far",100.0),
        view_height:option(options,"view_height",3.5),width:width,height:height,aspect:option(options,"aspect",width/height)};
    let z=subtract(camera.position,camera.target);
    let x=cross(camera.up,z);
    if (not contains(["perspective","orthographic"],camera.type) or not t.vector(camera.position,3) or not t.vector(camera.target,3) or not t.vector(camera.up,3) or
        dot(z,z)<=1e-20 or dot(x,x)<=1e-20) error("scene3d.camera: invalid camera vectors or projection")
    else if (not all([for (v in [width,height,camera.aspect,camera.near,camera.far,camera.fov,camera.view_height]) t.numeric(v) and v>0]) or
        camera.far<=camera.near or camera.fov>=180) error("scene3d.camera: invalid camera dimensions or frustum")
    else {*:camera,right:unit(x),vertical:cross(unit(z),unit(x)),back:unit(z),tangent:math.tan(camera.fov*math.pi/360.0)}
}
pub fn camera_space(camera,point) => (let p=subtract(point,camera.position),[dot(p,camera.right),dot(p,camera.vertical),0-dot(p,camera.back)])
pub fn world(camera,point) any | error => add(camera.position,add(times(camera.right,point[0]),add(times(camera.vertical,point[1]),times(camera.back,0-point[2]))))
fn half_height(camera,depth) => if (camera.type=="orthographic") camera.view_height/2 else depth*camera.tangent
pub fn project(camera,point) {
    let p=camera_space(camera,point);
    let span=half_height(camera,p[2]);
    let visible=p[2]>=camera.near and p[2]<=camera.far and abs(p[0])<=span*camera.aspect and abs(p[1])<=span;
    {x:if (p[2]<=0) null else (p[0]/(span*camera.aspect)+1)*camera.width/2,
        y:if (p[2]<=0) null else (1-p[1]/span)*camera.height/2,depth:p[2],visible:visible}
}
pub fn unproject(camera,x,y,depth) any | error => world(camera,[(2*x/camera.width-1)*half_height(camera,depth)*camera.aspect,
    (1-2*y/camera.height)*half_height(camera,depth),depth])
pub fn ray(camera,x,y) {
    let a=unproject(camera,x,y,camera.near);
    let b=unproject(camera,x,y,camera.far);
    {origin:a,direction:unit(subtract(b,a))}
}
fn plane_distance(camera,p,index) => if (index==0) p[2]-camera.near else if (index==1) camera.far-p[2]
    else if (index==2) p[0]+half_height(camera,p[2])*camera.aspect else if (index==3) half_height(camera,p[2])*camera.aspect-p[0]
    else if (index==4) p[1]+half_height(camera,p[2]) else half_height(camera,p[2])-p[1]
fn clip_planes(camera,a,b,index=0) {
    if (index>=6) [world(camera,a),world(camera,b)] else {
        let da=plane_distance(camera,a,index);
        let db=plane_distance(camera,b,index);
        let intersection=add(a,times(subtract(b,a),da/(da-db)));
        if (da<0 and db<0) [] else clip_planes(camera,if (da<0) intersection else a,if (db<0) intersection else b,index+1)
    }
}
pub fn clip_segment(camera,a,b) => clip_planes(camera,camera_space(camera,a),camera_space(camera,b))

// Triangles used by chart picking are the same clipped/billboard geometry sent to scene3d.
pub fn triangle_hit(ray,a,b,c) {
    let e1=subtract(b,a);
    let e2=subtract(c,a);
    let h=cross(ray.direction,e2);
    let det=dot(e1,h);
    if (det==0) null else {
        let s=subtract(ray.origin,a);
        let u=dot(s,h)/det;
        let q=cross(s,e1);
        let v=dot(ray.direction,q)/det;
        let distance=dot(e2,q)/det;
        if (u<0 or v<0 or u+v>1 or distance<0) null else {distance:distance,weights:[1-u-v,u,v],position:add(ray.origin,times(ray.direction,distance))}
    }
}
pub fn billboard(camera,point,size) {
    let p=project(camera,point);
    let radius=sqrt(size/math.pi);
    if (p.depth<camera.near or p.depth>camera.far) [] else [for (i in 0 to 15)
        [point,unproject(camera,p.x+radius*math.cos(i*math.pi/8),p.y+radius*math.sin(i*math.pi/8),p.depth),
            unproject(camera,p.x+radius*math.cos((i+1)*math.pi/8),p.y+radius*math.sin((i+1)*math.pi/8),p.depth)]]
}
pub fn ribbon(camera,a,b,width) {
    let clipped=clip_segment(camera,a,b);
    if (len(clipped)==0) [] else {
        let p=project(camera,clipped[0]);
        let q=project(camera,clipped[1]);
        let length=math.hypot(q.x-p.x,q.y-p.y);
        if (length==0) [] else {
            let dx=(q.y-p.y)*width/(2*length);
            let dy=(p.x-q.x)*width/(2*length);
            let corners=[unproject(camera,p.x+dx,p.y+dy,p.depth),unproject(camera,p.x-dx,p.y-dy,p.depth),
                unproject(camera,q.x-dx,q.y-dy,q.depth),unproject(camera,q.x+dx,q.y+dy,q.depth)];
            [for (indices in [[0,1,2],[0,2,3]]) [for (i in indices) corners[i]]]
        }
    }
}

fn rotate(v,axis,angle) any | error => add(add(times(v,math.cos(angle)),times(cross(axis,v),math.sin(angle))),times(axis,dot(axis,v)*(1-math.cos(angle))))
pub fn control(camera,action) {
    let delta=subtract(camera.position,camera.target);
    let distance=sqrt(dot(delta,delta));
    let zoom=option(action,"factor",1.0);
    let moved=if (action.type=="orbit") (
        let yaw=rotate(delta,unit(camera.up),option(action,"yaw",0.0)),
        let right=unit(cross(camera.up,yaw)),let pitch=rotate(yaw,right,option(action,"pitch",0.0)),
        // refuse a pole crossing; keeping a nonparallel up vector avoids a discontinuous camera roll.
        let direction=if (abs(dot(unit(pitch),unit(camera.up)))>0.9999) yaw else pitch,
        {*:camera,position:add(camera.target,direction)})
        else if (action.type=="pan") (let factor=2*half_height(camera,distance)/camera.height,
            let shift=add(times(camera.right,0-option(action,"dx",0.0)*factor),times(camera.vertical,option(action,"dy",0.0)*factor)),
            {*:camera,position:add(camera.position,shift),target:add(camera.target,shift)})
        else if (action.type=="zoom") if (camera.type=="orthographic") {*:camera,view_height:camera.view_height*zoom}
            else {*:camera,position:add(camera.target,times(delta,zoom))}
        else if (action.type=="view") (let direction=if (action.name=="front") [0.0,0.0,1.0] else if (action.name=="side") [1.0,0.0,0.0]
            else if (action.name=="top") [0.0,1.0,0.0] else unit([3.0,2.0,4.0]),
            {*:camera,position:add(camera.target,times(direction,distance)),up:if (action.name=="top") [0.0,0.0,-1.0] else [0.0,1.0,0.0]})
        else null;
    if (moved==null or not all([for (v in [option(action,"yaw",0.0),option(action,"pitch",0.0),option(action,"dx",0.0),option(action,"dy",0.0),zoom]) t.numeric(v)]) or zoom<=0)
        error("scene3d.camera: invalid control action") else prepare(moved,camera.width,camera.height)
}
