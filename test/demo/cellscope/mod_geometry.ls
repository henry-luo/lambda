// Curve sampling and capsule parameters follow Three.js r181; see LICENSE.three.
import s: lambda.scene3d
import v: lambda.scene3d.camera
import math

let PI = 3.141592653589793

// shared indexed grid, with analytic normals at smooth surfaces and split cap rims.
fn surface(id, points, normals, rows, columns, reverse=false) => s.geometry(points, {
    id:id, normals:normals, indices:[for (j in 0 to (rows-1), i in 0 to (columns-1),
        let a = j*(columns+1)+i, let b = a+columns+1)
        *(if (reverse) [a,b,a+1,a+1,b,b+1] else [a,a+1,b,a+1,b+1,b])]})

pub fn sphere(id,segments=48) {
    let points = [for (j in 0 to segments,i in 0 to segments,
        let latitude = PI*j/segments,let longitude = 2*PI*i/segments)
        *[sin(latitude)*cos(longitude),cos(latitude),sin(latitude)*sin(longitude)]];
    surface(id,points,points,segments,segments)
}

fn revolve(id,profile,segments) {
    let points = [for (p in profile,i in 0 to segments,let phi = 2*PI*i/segments)
        *[p.r*cos(phi),p.y,p.r*sin(phi)]]
    let normals = [for (p in profile,i in 0 to segments,let phi = 2*PI*i/segments)
        *[p.nr*cos(phi),p.ny,p.nr*sin(phi)]];
    surface(id,points,normals,len(profile)-1,segments)
}

pub fn cylinder(id,segments=18) => revolve(id,[
    {r:0.0,y:1.0,nr:0.0,ny:1.0},{r:1.0,y:1.0,nr:0.0,ny:1.0},
    {r:1.0,y:1.0,nr:1.0,ny:0.0},{r:1.0,y:-1.0,nr:1.0,ny:0.0},
    {r:1.0,y:-1.0,nr:0.0,ny:-1.0},{r:0.0,y:-1.0,nr:0.0,ny:-1.0}],segments)

// radius and straight length are independent, as in Three.js CapsuleGeometry.
pub fn capsule(id,radius,length,caps,segments) => revolve(id,[for (half in [0,1],j in 0 to caps,
    let theta = PI*(half+j/caps)/2)
    {r:radius*sin(theta),y:(if (half == 0) length/2 else -length/2)+radius*cos(theta),
        nr:sin(theta),ny:cos(theta)}],segments)

fn cubic(a,b,c,d,t) {
    let dt0 = sqrt(math.norm(b-a)), dt1 = sqrt(math.norm(c-b)), dt2 = sqrt(math.norm(d-c))
    let u = ((b-a)/dt0-(c-a)/(dt0+dt1)+(c-b)/dt1)*dt1
    let w = ((c-b)/dt1-(d-b)/(dt1+dt2)+(d-c)/dt2)*dt1;
    b+u*t+(-3*b+3*c-2*u-w)*t*t+(2*b-2*c+u+w)*t*t*t
}
pub fn curve(points,t) {
    let segment = min(len(points)-2,int(floor((len(points)-1)*t)))
    let a = if (segment == 0) 2*points[0]-points[1] else points[segment-1]
    let b = points[segment], c = points[segment+1]
    let d = if (segment+2 == len(points)) 2*c-b else points[segment+2];
    cubic(a,b,c,d,(len(points)-1)*t-segment)
}
fn tangent(points,t) array^ => v.unit(curve(points,min(1.0,t+0.0001))-curve(points,max(0.0,t-0.0001)))^
fn arc_parameter(lengths,u) {
    let distance = u*lengths[len(lengths)-1]
    let i = min(len(lengths)-2,len([for (length in lengths where length <= distance) length])-1);
    (i+(distance-lengths[i])/(lengths[i+1]-lengths[i]))/(len(lengths)-1)
}

// the upstream three-point tubes are planar; a fixed binormal gives exact parallel transport.
pub fn tube(id,points,radius=0.03,segments=80,sides=12) element^ {
    let samples = [for (i in 0 to 200) curve(points,i/200)]
    let lengths = math.cumsum([0.0,*[for (i in 1 to 200) math.norm(samples[i]-samples[i-1])]])
    let parameters = [for (i in 0 to segments) arc_parameter(lengths,i/segments)]
    let tangents = [for (t in parameters) tangent(points,t)^]
    let first = tangents[0]
    let smallest = min([for (x in first) abs(x)])
    let axis = if (abs(first[2]) == smallest) [0.0,0.0,1.0]
        else if (abs(first[1]) == smallest) [0.0,1.0,0.0] else [1.0,0.0,0.0]
    let initial = v.cross(first,v.unit(v.cross(first,axis))^)
    let plane = v.unit(v.cross(points[1]-points[0],points[2]-points[1]))^
    let along = v.dot(initial,plane), across = v.dot(initial,v.cross(plane,first))
    let frames = [for (t in tangents,let normal = plane*along+v.cross(plane,t)*across)
        {n:normal,b:v.cross(t,normal)}]
    let normals = [for (frame in frames,i in 0 to sides,let phi = 2*PI*i/sides)
        *(frame.n*(-cos(phi))+frame.b*sin(phi))]
    let vertices = [for (j in 0 to segments,i in 0 to sides,let k = 3*(j*(sides+1)+i))
        *(curve(points,parameters[j])+[normals[k],normals[k+1],normals[k+2]]*radius)];
    surface(id,vertices,normals,segments,sides,true)
}
