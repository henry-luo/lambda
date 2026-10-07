final class RayTrace {
    record V(double x,double y,double z){double at(int i){return i==0?x:i==1?y:z;}}
    record Triangle(int axis,V normal,double nu,double nv,double nd,double eu,double ev,double nu1,double nv1,double nu2,double nv2,boolean floor){}
    record Light(V position,V colour){}
    static V add(V a,V b){return new V(a.x+b.x,a.y+b.y,a.z+b.z);}
    static V sub(V a,V b){return new V(a.x-b.x,a.y-b.y,a.z-b.z);}
    static V scale(V v,double s){return new V(v.x*s,v.y*s,v.z*s);}
    static double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static V cross(V a,V b){return new V(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
    static double length(V v){return Math.sqrt(dot(v,v));}
    static V normalize(V v){double l=length(v);return new V(v.x/l,v.y/l,v.z/l);}
    static Triangle triangle(V p1,V p2,V p3,boolean floor){V e1=sub(p3,p1),e2=sub(p2,p1),n=cross(e1,e2);double ax=Math.abs(n.x),ay=Math.abs(n.y),az=Math.abs(n.z);
        int axis=ax>ay?(ax>az?0:2):(ay>az?1:2),u=(axis+1)%3,v=(axis+2)%3;double u1=e1.at(u),v1=e1.at(v),u2=e2.at(u),v2=e2.at(v),det=u1*v2-v1*u2,na=n.at(axis);
        return new Triangle(axis,normalize(n),n.at(u)/na,n.at(v)/na,dot(n,p1)/na,p1.at(u),p1.at(v),u1/det,-v1/det,v2/det,-u2/det,floor);}
    static double intersect(Triangle tri,V origin,V direction,double near,double far){int a=tri.axis,u=(a+1)%3,v=(a+2)%3;
        double d=direction.at(a)+tri.nu*direction.at(u)+tri.nv*direction.at(v),t=(tri.nd-origin.at(a)-tri.nu*origin.at(u)-tri.nv*origin.at(v))/d;
        if(t<near||t>far)return Double.NaN;
        double pu=origin.at(u)+t*direction.at(u)-tri.eu,pv=origin.at(v)+t*direction.at(v)-tri.ev;
        double a2=pv*tri.nu1+pu*tri.nv1,a3=pu*tri.nu2+pv*tri.nv2;if(a2<0||a3<0||a2+a3>1)return Double.NaN;return t;}
    static boolean blocked(Triangle[] triangles,V origin,V direction,double far){for(Triangle t:triangles){double d=intersect(t,origin,direction,.0001,far);if(!Double.isNaN(d)&&!(d>far||d<.0001))return true;}return false;}
    static V trace(Triangle[] triangles,Light[] lights,V origin,V direction,double near,double far){Triangle closest=null;
        for(Triangle tri:triangles){double d=intersect(tri,origin,direction,near,far);if(Double.isNaN(d)||d>far||d<near)continue;far=d;closest=tri;}
        if(closest==null)return new V(.8,.8,1);V normal=closest.normal,hit=add(origin,scale(direction,far));if(dot(direction,normal)>0)normal=scale(normal,-1);V colour=new V(.7,.7,.7);
        if(closest.floor){double x=((hit.x/32%2)+2)%2,z=((hit.z/32+.3)%2+2)%2;
            if((x<1)!=(z<1)){V reflection=add(scale(normal,-2*dot(direction,normal)),direction);return trace(triangles,lights,hit,reflection,.0001,1000000);}colour=new V(0,.4,0);}
        V sum=new V(.1,.1,.1);for(Light light:lights){V to=sub(light.position,hit);double distance=length(to);to=scale(to,1/distance);
            if(blocked(triangles,hit,to,distance-.0001))continue;double nl=dot(normal,to);if(nl>0)sum=add(sum,scale(light.colour,nl));}
        return new V(sum.x*colour.x,sum.y*colour.y,sum.z*colour.z);}
    static V transform(V x,V y,V z,V v){return new V(x.x*v.x+y.x*v.y+z.x*v.z,x.y*v.x+y.y*v.y+z.y*v.z,x.z*v.x+y.z*v.y+z.z*v.z);}
    static V[][] render(){V tfl=new V(-10,10,-10),tfr=new V(10,10,-10),tbl=new V(-10,10,10),tbr=new V(10,10,10),bfl=new V(-10,-10,-10),bfr=new V(10,-10,-10),bbl=new V(-10,-10,10),bbr=new V(10,-10,10);
        V ffl=new V(-1000,-30,-1000),ffr=new V(1000,-30,-1000),fbl=new V(-1000,-30,1000),fbr=new V(1000,-30,1000);
        Triangle[] ts={triangle(tfl,tfr,bfr,false),triangle(tfl,bfr,bfl,false),triangle(tbl,tbr,bbr,false),triangle(tbl,bbr,bbl,false),triangle(tbl,tfl,bbl,false),triangle(tfl,bfl,bbl,false),triangle(tbr,tfr,bbr,false),triangle(tfr,bfr,bbr,false),triangle(tbl,tbr,tfr,false),triangle(tbl,tfr,tfl,false),triangle(bbl,bbr,bfr,false),triangle(bbl,bfr,bfl,false),triangle(fbl,fbr,ffr,true),triangle(fbl,ffr,ffl,true)};
        Light[] lights={new Light(new V(20,38,-22),new V(.7,.3,.3)),new Light(new V(-23,40,17),new V(.7,.3,.3)),new Light(new V(23,20,17),new V(.7,.7,.7))};
        V origin=new V(-40,40,40),z=normalize(sub(new V(0,0,0),origin)),x=normalize(cross(new V(0,1,0),z)),y=normalize(cross(x,scale(z,-1)));
        V d0=transform(x,y,z,normalize(new V(-.7,.7,1))),d1=transform(x,y,z,normalize(new V(.7,.7,1))),d2=transform(x,y,z,normalize(new V(.7,-.7,1))),d3=transform(x,y,z,normalize(new V(-.7,-.7,1)));
        V[][] pixels=new V[30][30];for(int row=0;row<30;row++){double yf=row/30.0;V r0=add(scale(d0,yf),scale(d3,1-yf)),r1=add(scale(d1,yf),scale(d2,1-yf));
            for(int col=0;col<30;col++){double xf=col/30.0;pixels[row][col]=trace(ts,lights,add(scale(origin,xf),scale(origin,1-xf)),normalize(add(scale(r0,xf),scale(r1,1-xf))),Double.NaN,Double.NaN);}}return pixels;}
    static String number(double v){return v==(long)v?Long.toString((long)v):Double.toString(v);}
    static String canvas(V[][] pixels){StringBuilder out=new StringBuilder(PREFIX);for(V[] row:pixels){out.append('[');for(V p:row)out.append('[').append(number(p.x)).append(',').append(number(p.y)).append(',').append(number(p.z)).append("],");out.append("],");}return out.append(SUFFIX).toString();}
    static final String PREFIX="<canvas id=\"renderCanvas\" width=\"30px\" height=\"30px\"></canvas><script>\nvar pixels = [";
    static final String SUFFIX="];\n    var canvas = document.getElementById(\"renderCanvas\").getContext(\"2d\");\n\n\n    var size = 30;\n    canvas.fillStyle = \"red\";\n    canvas.fillRect(0, 0, size, size);\n    canvas.scale(1, -1);\n    canvas.translate(0, -size);\n\n    if (!canvas.setFillColor)\n        canvas.setFillColor = function(r, g, b, a) {\n            this.fillStyle = \"rgb(\"+[Math.floor(r * 255), Math.floor(g * 255), Math.floor(b * 255)]+\")\";\n    }\n\nfor (var y = 0; y < size; y++) {\n  for (var x = 0; x < size; x++) {\n    var l = pixels[y][x];\n    canvas.setFillColor(l[0], l[1], l[2], 1);\n    canvas.fillRect(x, y, 1, 1);\n  }\n}</script>";
    static boolean run(){String commands=canvas(render());NativeBench.observed=commands;return commands.length()==20970;}
}
