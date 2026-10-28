// a saved pond carves the terrain and owns a stable horizontal water level (PG15).
import c: lambda.procart._common
import field: lambda.procart.field
import s: lambda.scene3d
import color: lambda.color

pub fn from_document(doc) map?^ {
    let ponds=[for (n in content(doc) where n is element and name(n)=='pond') n]
    let count=c.require(len(ponds)<=1,"meadow supports one pond")^;
    if (len(ponds)==0) null else {
        let relief=c.require(doc.relief>0.0,"pond needs positive terrain relief")^
        let p=ponds[0]
        let fields=c.require(c.fields(p,["id","position","size","depth","color"]) and c.identifier(p.id) and len(content(p))==0,"invalid pond element")^
        let cfg={id:p.id,position:[6.0,-2.6],size:[6.6,4.6],depth:0.55,color:"#719e9a",*:map(p)}
        let dims=c.require(cfg.position is array and len(cfg.position)==2 and cfg.size is array and len(cfg.size)==2 and
            all([for (i in 0 to 1) c.bounded(cfg.size[i],1.0,10.0) and c.bounded(cfg.position[i],-32.0,32.0) and
                abs(cfg.position[i])+cfg.size[i]*0.65< (if (i==0) doc.width else doc.depth)/2.0]) and
            c.bounded(cfg.depth,0.1,1.5) and cfg.color is string,"pond dimensions exceed meadow")^;
        let paint=color.parse(cfg.color)^;
        cfg
    }
}
fn shore(angle) float => 1.0+0.07*sin(3.0*angle)+0.04*cos(5.0*angle)
pub fn distance(p,x,z) float {
    let a=(x-p.position[0])/(p.size[0]/2.0),b=(z-p.position[1])/(p.size[1]/2.0);
    sqrt(a*a+b*b)/shore(math.atan2(b,a))
}
pub fn height(p,x,z,original) float {
    let d=distance(p,x,z)
    let blend=max(0.0,min(1.0,(1.3-d)/0.3)),smooth=blend*blend*(3.0-2.0*blend)
    let bottom=p.level-0.1-p.depth*max(0.0,1.0-d*d);
    original*(1.0-smooth)+bottom*smooth
}
pub fn scene(p) array {
    let segments=64
    let points=[[p.position[0],p.level,p.position[1]],*[for (i in 0 to segments,let a=2.0*math.pi*float(i)/segments,let r=shore(a))
        [p.position[0]+p.size[0]/2.0*r*cos(a),p.level,p.position[1]+p.size[1]/2.0*r*sin(a)]]];
    [s.mesh(s.geometry([for (point in points) *point],{normals:[for (point in points) *[0.0,1.0,0.0]],
        indices:[for (i in 1 to segments) *[0,i+1,i]]}),s.material('standard',{color:p.color,roughness:0.2,metalness:0.15,
            transparent:true,opacity:0.9,side:'double'}),{id:"pond-water",'data-kind':"pond"}),
     *[for (ring in 0 to 2,let radius=0.2+float(ring)*0.15)
        s.mesh(s.geometry([for (i in 0 to 48,let a=2.0*math.pi*float(i)/48.0) for (r in [radius,radius+0.006])
            *[p.position[0]-0.9+r*cos(a),p.level+0.008,p.position[1]+0.7+r*sin(a)]],
            {indices:[for (i in 0 to 47,let a=i*2) *[a,a+3,a+1,a,a+2,a+3]]}),
            s.material('basic',{color:"#c0d5c3",transparent:true,opacity:0.35,side:'double'}),{'data-kind':"pond"})]]
}
