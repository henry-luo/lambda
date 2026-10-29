// PG17 / S12.1.1v3: shared smooth surfaces keep character proportions in the recipe.
import s: lambda.scene3d
import v: lambda.scene3d.camera
import shapes: lambda.scene3d.shape
import primitive: .primitive

let PROFILES={
 human_head:[[-0.5,0.18,0.24,0.02],[-0.44,0.30,0.34,0.02],[-0.3,0.40,0.42,0.02],[-0.12,0.48,0.47,0.0],[0.1,0.50,0.47,0.0],[0.30,0.46,0.44,-0.02],[0.44,0.31,0.30,-0.02],[0.5,0.01,0.01,-0.02]],
 plush_head:[[-0.5,0.04,0.04,0.03],[-0.42,0.30,0.30,0.03],[-0.24,0.46,0.43,0.04],[0.0,0.50,0.48,0.02],[0.24,0.47,0.44,0.0],[0.42,0.32,0.30,0.0],[0.5,0.01,0.01,0.0]],
 torso:[[-0.5,0.42,0.39,0.0],[-0.34,0.39,0.37,0.0],[-0.08,0.42,0.43,0.01],[0.18,0.50,0.48,0.0],[0.35,0.56,0.45,0.0],[0.44,0.38,0.34,0.0],[0.5,0.19,0.21,0.0]],
 plush_body:[[-0.5,0.18,0.27,0.0],[-0.42,0.41,0.43,0.02],[-0.16,0.52,0.52,0.03],[0.14,0.48,0.47,0.0],[0.34,0.39,0.38,0.0],[0.5,0.20,0.24,0.0]],
 spirit_body:[[-0.5,0.22,0.28,0.0],[-0.3,0.33,0.35,0.0],[0.0,0.48,0.47,0.0],[0.26,0.59,0.47,0.0],[0.4,0.48,0.36,0.0],[0.5,0.20,0.22,0.0]],
 pelvis:[[-0.5,0.32,0.36,0.0],[-0.25,0.46,0.47,0.0],[0.15,0.50,0.50,0.0],[0.5,0.43,0.44,0.0]],
 limb:[[-1.0,0.34,0.34,0.0],[-0.9,0.38,0.38,0.0],[-0.65,0.43,0.44,0.0],[-0.34,0.50,0.50,0.0],[-0.08,0.46,0.46,0.0],[0.0,0.39,0.39,0.0]],
 forearm:[[-1.0,0.29,0.29,0.0],[-0.87,0.32,0.33,0.0],[-0.5,0.45,0.46,0.0],[-0.15,0.48,0.48,0.0],[0.0,0.37,0.37,0.0]],
 hand:[[-0.5,0.32,0.32,0.0],[-0.42,0.43,0.39,0.0],[-0.12,0.50,0.48,0.0],[0.19,0.46,0.43,0.0],[0.5,0.25,0.27,0.0]],
 boot:[[-0.5,0.39,0.44,0.0],[-0.35,0.50,0.50,0.0],[0.0,0.49,0.47,0.03],[0.25,0.40,0.36,-0.04],[0.5,0.31,0.28,-0.10]],
 dress:[[-0.5,0.50,0.50,0.0],[-0.42,0.49,0.49,0.0],[-0.05,0.37,0.38,0.0],[0.32,0.25,0.27,0.0],[0.5,0.23,0.25,0.0]],
 coat:[[-0.5,0.47,0.47,0.0],[-0.2,0.42,0.43,0.0],[0.2,0.40,0.41,0.0],[0.5,0.42,0.43,0.0]],
 hair_back:[[-0.5,0.30,0.32,0.0],[-0.4,0.46,0.42,0.0],[-0.15,0.50,0.50,0.0],[0.15,0.48,0.48,0.0],[0.38,0.38,0.40,0.0],[0.5,0.08,0.12,0.0]],
 turban:[[-0.5,0.39,0.38,0.0],[-0.36,0.50,0.49,0.0],[-0.05,0.55,0.52,0.0],[0.2,0.48,0.45,0.0],[0.43,0.30,0.29,0.0],[0.5,0.01,0.01,0.0]],
 rounded_box:[[-0.5,0.37,0.37,0.0],[-0.40,0.49,0.49,0.0],[0.40,0.49,0.49,0.0],[0.5,0.37,0.37,0.0]]}

fn signed_power(x,p) float => (if (x<0.0) -1.0 else 1.0)*abs(x)**p
fn secant(profile,i,axis) float => (profile[i+1][axis]-profile[i][axis])/(profile[i+1][0]-profile[i][0])
// monotone Hermite radii remove ring facets without overshooting the authored silhouette.
fn tangent(profile,i,axis) float {
 if (i==0) secant(profile,0,axis) else if (i==len(profile)-1) secant(profile,i-1,axis)
 else {
  let before=secant(profile,i-1,axis),after=secant(profile,i,axis),
   a=profile[i][0]-profile[i-1][0],b=profile[i+1][0]-profile[i][0];
  if (before*after<=0.0) 0.0 else 3.0*(a+b)/((2.0*b+a)/before+(b+2.0*a)/after)
 }
}
fn sample_profile(profile,i,q) array {
 let a=profile[i],b=profile[i+1];
 [a[0]+(b[0]-a[0])*q,*[for (axis in 1 to 3)
   a[axis]*(2.0*q**3.0-3.0*q*q+1.0)+b[axis]*(-2.0*q**3.0+3.0*q*q)+
   (b[0]-a[0])*(tangent(profile,i,axis)*(q**3.0-2.0*q*q+q)+tangent(profile,i+1,axis)*(q**3.0-q*q))]]
}
fn smooth_profile(profile) array => [
 *[for (i in 0 to (len(profile)-2),j in 0 to 5) sample_profile(profile,i,float(j)/6.0)],
 profile[len(profile)-1]]
fn ring_point(row,angle,power,ripple) array =>
 [row[1]*signed_power(cos(angle),power),row[0]+ripple*sin(angle*8.0)*(0.5-row[0]),row[3]+row[2]*signed_power(sin(angle),power)]
fn loft(profile,power=1.0,ripple=0.0,start=0.0,end=2.0*math.pi,capped=true) map {
 let segments=40,rows=len(profile),step=(end-start)/float(segments)
 let vertices=[for (i,row in profile,j in 0 to segments,let angle=start+float(j)*step,
  let across=ring_point(row,angle+0.01,power,ripple)-ring_point(row,angle-0.01,power,ripple),
  let along=ring_point(profile[min(rows-1,i+1)],angle,power,ripple)-ring_point(profile[max(0,i-1)],angle,power,ripple))
   {p:ring_point(row,angle,power,ripple),n:v.unit(v.cross(along,across)),uv:[float(j)/segments,float(i)/float(rows-1)]}]
 let caps=if (not capped) [] else [{p:[0.0,profile[0][0],profile[0][3]],n:[0.0,-1.0,0.0],uv:[0.5,0.5]},
  {p:[0.0,profile[rows-1][0],profile[rows-1][3]],n:[0.0,1.0,0.0],uv:[0.5,0.5]}]
 let all=[*vertices,*caps],width=segments+1;
 {positions:[for (p in all) *p.p],normals:[for (p in all) *p.n],uvs:[for (p in all) *p.uv],indices:[
  *[for (i in 0 to (rows-2),j in 0 to (segments-1),let a=i*width+j,let b=a+width) *[a,b,a+1,b,b+1,a+1]],
  *(if (not capped) [] else [for (j in 0 to (segments-1)) *[len(vertices),j,j+1,len(vertices)+1,(rows-1)*width+j+1,(rows-1)*width+j]])]}
}
fn bezier(points,t) array => points[0]*(1.0-t)**3.0+points[1]*(3.0*t*(1.0-t)**2.0)+points[2]*(3.0*t*t*(1.0-t))+points[3]*t**3.0
fn curve(points,radius=0.025) map => primitive.sweep([for (i in 0 to 20) bezier(points,float(i)/20.0)],radius,8,true)
fn scalp_point(a,phi) array {
 let profile=PROFILES.human_head,y=cos(phi)*0.5,
  i=min(len(profile)-2,len([for (row in profile where row[0]<=y) row])-1),
  row=sample_profile(profile,i,(y-profile[i][0])/(profile[i+1][0]-profile[i][0]));
 [row[1]*cos(a),y,row[3]+row[2]*sin(a)]
}
fn cap(hood=false) map {
 let rows=12,columns=40
 let points=[for (i in 0 to rows,j in 0 to columns,let a=float(j)/columns*2.0*math.pi,
  let end=(if (hood) 2.65 else 2.1)-(if (hood) 1.3 else 1.05)*max(0.0,sin(a)),
  let phi=0.005+(end-0.005)*float(i)/rows,
  let n=if (hood) [sin(phi)*cos(a),cos(phi),sin(phi)*sin(a)] else
   v.unit(v.cross(scalp_point(a+0.001,phi)-scalp_point(a-0.001,phi),scalp_point(a,phi+0.001)-scalp_point(a,max(0.0001,phi-0.001)))))
   {p:if (hood) n*0.5 else scalp_point(a,phi),n:n,uv:[float(j)/columns,float(i)/rows]}];
 {positions:[for (p in points) *p.p],normals:[for (p in points) *p.n],uvs:[for (p in points) *p.uv],
  indices:[for (i in 0 to (rows-1),j in 0 to (columns-1),let a=i*(columns+1)+j,let b=a+columns+1) *[a,a+1,b,b,a+1,b+1]]}
}
fn membrane(lower=false) map {
 let controls=if (lower) [[0.0,0.0,0.0],[0.7,-0.85,0.0],[1.15,-0.20,0.0],[0.0,0.0,0.0]]
  else [[0.0,0.0,0.0],[0.55,1.25,0.0],[1.55,0.65,0.0],[0.0,0.0,0.0]]
 let edge=[for (i in 0 to 32) bezier(controls,float(i)/32.0)]
 let center=if (lower) [0.42,-0.23,0.025] else [0.50,0.43,0.025]
 let points=[center,*edge];
 {positions:[for (p in points) *p],normals:[for (p in points) *[0.0,0.0,1.0]],uvs:[for (p in points) *[p[0],p[1]+0.5]],
  indices:[for (i in 1 to 32) *(if (lower) [0,i,i+1] else [0,i+1,i])]}
}
fn ring() map => primitive.sweep([for (i in 0 to 40,let a=float(i)*2.0*math.pi/40.0) [cos(a)*0.5,0.0,sin(a)*0.5]],0.035,8,false)
fn spirit() map => primitive.sweep([for (i in 0 to 32,let q=float(i)/32.0)
 [sin(q*2.0*math.pi)*0.17*q,-q*0.62,0.05*sin(q*math.pi)]],
 [for (i in 0 to 32) 0.16*(1.0-float(i)/32.0)**0.85+0.001],20,true)
fn ear() map {
 let front=[[-0.5,-0.3,0.0],[-0.4,0.25,0.0],[0.5,0.5,0.0],[0.1,-0.35,0.0],[0.0,0.0,0.12]];
 {positions:[for (p in front) *p],normals:[for (p in front) *[0.0,0.0,1.0]],uvs:[for (p in front) *[p[0]+0.5,p[1]+0.5]],indices:[0,4,1,1,4,2,2,4,3,3,4,0]}
}
fn buffer(kind) map^ => if (PROFILES[kind]!=null) loft(if (kind=="rounded_box") PROFILES[kind] else smooth_profile(PROFILES[kind]),
 if (kind=="rounded_box") 0.28 else 1.0,if (kind=="dress") 0.018 else 0.0)
 else if (kind=="round") shapes.sphere(20,32)
 else if (kind=="hair" or kind=="hood") cap(kind=="hood")
 else if (kind=="vest") loft(smooth_profile(PROFILES.spirit_body),1.0,0.0,-math.pi*0.35,math.pi*0.42,false)
 else if (kind=="vest-edge") primitive.sweep([for (row in smooth_profile(PROFILES.spirit_body)) ring_point(row,math.pi*0.42,1.0,0.0)],0.012,8,true)
 else if (kind=="ring") ring()
 else if (kind=="spirit") spirit()
 else if (kind=="ear") ear()
 else if (kind=="wing-upper" or kind=="wing-lower") membrane(kind=="wing-lower")
 else if (kind=="smile") curve([[-0.5,0.18,0.0],[-0.2,-0.25,0.04],[0.2,-0.25,0.04],[0.5,0.18,0.0]],0.027)
 else if (kind=="arch") curve([[-0.5,-0.08,0.0],[-0.2,0.2,0.04],[0.2,0.2,0.04],[0.5,-0.08,0.0]],0.035)
 else if (kind=="lock") curve([[0.0,0.5,0.0],[-0.25,0.1,0.10],[0.25,-0.2,0.15],[0.0,-0.5,0.0]],0.14)
 else if (kind=="vein") curve([[0.0,0.0,0.0],[0.1,0.32,0.0],[0.4,0.55,0.0],[0.6,0.54,0.0]],0.008)
 else if (kind=="feather") primitive.blade(10,6,0.24,0.04)
 else raise error("avatar: unknown surface prototype")

pub fn build(kinds) array^ => [for (kind in kinds,let b=buffer(kind)^)
 s.geometry(b.positions,{id:"avatar-v2-"++kind,normals:b.normals,indices:b.indices,uvs:b.uvs})]
