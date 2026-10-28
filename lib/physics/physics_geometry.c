#include "physics_geometry.h"
#include "../mem.h"
#include <float.h>
#include <limits.h>
#include <string.h>

static bool geometry_fail(const char** error,const char* message) {if(error)*error=message;return false;}
typedef struct HullTriangle {unsigned a,b,c;PhysicsVec normal;} HullTriangle;
typedef struct HullPoint2 {unsigned index;double x,y;} HullPoint2;

static bool hull_triangle(const PhysicsVec* p,unsigned a,unsigned b,unsigned c,PhysicsVec inside,HullTriangle* out) {
    PhysicsVec n=pcross(psub(p[b],p[a]),psub(p[c],p[a]));double length=plen(n);
    if(length<=1e-18)return false;n=pmul(n,1/length);
    if(pdot(n,psub(inside,p[a]))>0){unsigned swap=b;b=c;c=swap;n=pmul(n,-1);}
    *out=(HullTriangle){a,b,c,n};return true;
}
static double hull_turn(HullPoint2 a,HullPoint2 b,HullPoint2 c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
static bool hull_polygon(const PhysicsVec* points,unsigned count,HullTriangle triangle,PhysicsHullFace* face) {
    unsigned base=0;for(unsigned i=1;i<3;i++)if(fabs(paxis(triangle.normal,i))<fabs(paxis(triangle.normal,base)))base=i;
    PhysicsVec u=punit(pcross(pbase(base),triangle.normal)),v=pcross(triangle.normal,u);
    HullPoint2 projected[PHYSICS_CONVEX_MAX_VERTICES];unsigned size=0;
    for(unsigned i=0;i<count;i++)if(fabs(pdot(triangle.normal,psub(points[i],points[triangle.a])))<=1e-9)
        projected[size++]=(HullPoint2){i,pdot(points[i],u),pdot(points[i],v)};
    for(unsigned i=1;i<size;i++) {
        HullPoint2 point=projected[i];unsigned j=i;
        while(j&&(projected[j-1].x>point.x||(projected[j-1].x==point.x&&projected[j-1].y>point.y))){projected[j]=projected[j-1];j--;}
        projected[j]=point;
    }
    if(size<3)return false;
    // the 2D hull excludes coplanar interior points as well as triangulation diagonals.
    HullPoint2 boundary[2*PHYSICS_CONVEX_MAX_VERTICES];unsigned n=0;
    for(unsigned i=0;i<size;i++) {
        while(n>=2&&hull_turn(boundary[n-2],boundary[n-1],projected[i])<=1e-12)n--;
        boundary[n++]=projected[i];
    }
    unsigned lower=n;
    for(unsigned i=size-1;i>0;i--) {
        HullPoint2 point=projected[i-1];
        while(n>lower&&hull_turn(boundary[n-2],boundary[n-1],point)<=1e-12)n--;
        boundary[n++]=point;
    }
    if(n<4)return false;face->normal=triangle.normal;face->count=n-1;
    for(unsigned i=0;i<face->count;i++)face->vertices[i]=boundary[i].index;
    return true;
}
static bool hull_integrals(PhysicsHull* hull,const PhysicsVec* normalized,PhysicsVec origin) {
    PhysicsVec reference=pv(0,0,0),first=pv(0,0,0);double volume=0,second[9]={0};
    for(unsigned i=0;i<hull->vertex_count;i++)reference=padd(reference,pmul(normalized[i],1.0/hull->vertex_count));
    for(unsigned i=0;i<hull->face_count;i++) {
        const PhysicsHullFace* f=&hull->faces[i];PhysicsVec a=psub(normalized[f->vertices[0]],reference);
        for(unsigned j=1;j+1<f->count;j++) {
            PhysicsVec b=psub(normalized[f->vertices[j]],reference),c=psub(normalized[f->vertices[j+1]],reference);
            double tetra=pdot(a,pcross(b,c))/6;PhysicsVec sum=padd(a,padd(b,c));
            volume+=tetra;first=padd(first,pmul(sum,tetra/4));
            for(unsigned r=0;r<3;r++)for(unsigned col=0;col<3;col++)
                second[r*3+col]+=tetra/20*(paxis(sum,r)*paxis(sum,col)+paxis(a,r)*paxis(a,col)+paxis(b,r)*paxis(b,col)+paxis(c,r)*paxis(c,col));
        }
    }
    if(!isfinite(volume)||volume<=0)return false;
    PhysicsVec centre=pmul(first,1/volume);double trace=second[0]+second[4]+second[8],scale=hull->scale;
    double volume_scale=scale*scale*scale,inertia_scale=volume_scale*scale*scale;
    hull->volume=volume*volume_scale;hull->centre=padd(origin,pmul(padd(reference,centre),scale));
    if(!isfinite(hull->volume)||hull->volume<=0||!pfinite(hull->centre))return false;
    for(unsigned r=0;r<3;r++)for(unsigned col=0;col<3;col++) {
        hull->inertia[r*3+col]=((r==col?trace:0)-second[r*3+col]-
            volume*((r==col?pdot(centre,centre):0)-paxis(centre,r)*paxis(centre,col)))*inertia_scale;
        if(!isfinite(hull->inertia[r*3+col]))return false;
    }
    return hull->inertia[0]>0&&hull->inertia[4]>0&&hull->inertia[8]>0;
}
bool physics_hull_create(const PhysicsVec* input,size_t count,PhysicsHull** out,const char** error) {
    if(!out)return geometry_fail(error,"convex hull requires an output");
    *out=NULL;
    if(!input||count<4||count>PHYSICS_CONVEX_MAX_VERTICES)return geometry_fail(error,"convex hull requires 4 to 64 vertices");
    PhysicsVec low=input[0],high=input[0],sorted[PHYSICS_CONVEX_MAX_VERTICES];
    for(size_t i=0;i<count;i++) {
        if(!pfinite(input[i]))return geometry_fail(error,"convex hull requires finite vertices");
        sorted[i]=input[i];low=pv(fmin(low.x,input[i].x),fmin(low.y,input[i].y),fmin(low.z,input[i].z));
        high=pv(fmax(high.x,input[i].x),fmax(high.y,input[i].y),fmax(high.z,input[i].z));
    }
    PhysicsVec span=psub(high,low);double scale=fmax(span.x,fmax(span.y,span.z));
    if(!isfinite(scale)||scale<=0)return geometry_fail(error,"convex hull is degenerate or outside numerical range");
    for(size_t i=1;i<count;i++) {
        PhysicsVec p=sorted[i];size_t j=i;
        while(j&&(sorted[j-1].x>p.x||(sorted[j-1].x==p.x&&(sorted[j-1].y>p.y||(sorted[j-1].y==p.y&&sorted[j-1].z>p.z))))) {
            sorted[j]=sorted[j-1];j--;
        }sorted[j]=p;
    }
    PhysicsHull* hull=mem_calloc(1,sizeof(*hull),MEM_CAT_EVAL);
    if(!hull)return geometry_fail(error,"convex hull allocation failed");
    hull->scale=scale;PhysicsVec origin=padd(low,pmul(span,.5)),p[PHYSICS_CONVEX_MAX_VERTICES];
    for(size_t i=0;i<count;i++) {
        PhysicsVec point=pmul(psub(sorted[i],origin),1/scale);bool duplicate=false;
        for(unsigned j=0;j<hull->vertex_count;j++)if(plen(psub(point,p[j]))<=1e-9){duplicate=true;break;}
        if(!duplicate){unsigned j=hull->vertex_count++;p[j]=point;hull->vertices[j]=sorted[i];}
    }
    unsigned n=hull->vertex_count,a=0,b=0,c=0,d=0;double far=0;
    for(unsigned i=1;i<n;i++){double value=plen(psub(p[i],p[a]));if(value>far){far=value;b=i;}}
    PhysicsVec line=n?psub(p[b],p[a]):pv(0,0,0);double line_length=plen(line);far=0;
    if(line_length>0)for(unsigned i=0;i<n;i++){double value=plen(pcross(line,psub(p[i],p[a])))/line_length;if(value>far){far=value;c=i;}}
    PhysicsVec normal=punit(pcross(line,psub(p[c],p[a])));double area_distance=far;far=0;
    for(unsigned i=0;i<n;i++){double value=fabs(pdot(normal,psub(p[i],p[a])));if(value>far){far=value;d=i;}}
    if(n<4||area_distance<=1e-9||far<=1e-9){physics_hull_destroy(hull);return geometry_fail(error,"convex hull must have nondegenerate 3D volume");}
    PhysicsVec inside=pmul(padd(padd(p[a],p[b]),padd(p[c],p[d])),.25);
    HullTriangle triangles[PHYSICS_POLY_MAX_FACES],next;unsigned triangles_count=4;
    if(!hull_triangle(p,a,b,c,inside,&triangles[0])||!hull_triangle(p,a,d,b,inside,&triangles[1])||
       !hull_triangle(p,b,d,c,inside,&triangles[2])||!hull_triangle(p,c,d,a,inside,&triangles[3])) {
        physics_hull_destroy(hull);return geometry_fail(error,"convex hull seed exceeds numerical tolerance");
    }
    for(unsigned point=0;point<n;point++) {
        PhysicsHullEdge horizon[3*PHYSICS_POLY_MAX_FACES];unsigned horizon_count=0,kept=0;
        for(unsigned i=0;i<triangles_count;i++) {
            HullTriangle t=triangles[i];
            if(pdot(t.normal,psub(p[point],p[t.a]))<=1e-9){triangles[kept++]=t;continue;}
            unsigned corners[]={t.a,t.b,t.c};
            for(unsigned edge=0;edge<3;edge++) {
                PhysicsHullEdge candidate={corners[edge],corners[(edge+1)%3]};unsigned j=0;
                for(;j<horizon_count;j++)if(horizon[j].a==candidate.b&&horizon[j].b==candidate.a)break;
                if(j<horizon_count)horizon[j]=horizon[--horizon_count];else horizon[horizon_count++]=candidate;
            }
        }
        triangles_count=kept;
        for(unsigned i=0;i<horizon_count;i++) {
            if(triangles_count==PHYSICS_POLY_MAX_FACES||!hull_triangle(p,horizon[i].a,horizon[i].b,point,inside,&next)) {
                physics_hull_destroy(hull);return geometry_fail(error,"convex hull topology exceeded numerical tolerance or quota");
            }triangles[triangles_count++]=next;
        }
    }
    hull->faces=mem_calloc(triangles_count,sizeof(PhysicsHullFace),MEM_CAT_EVAL);
    if(!hull->faces){physics_hull_destroy(hull);return geometry_fail(error,"convex hull allocation failed");}
    for(unsigned i=0;i<triangles_count;i++) {
        bool merged=false;
        for(unsigned j=0;j<hull->face_count;j++)if(plen(psub(triangles[i].normal,hull->faces[j].normal))<=1e-9&&
            fabs(pdot(triangles[i].normal,psub(p[triangles[i].a],p[hull->faces[j].vertices[0]])))<=1e-9){merged=true;break;}
        if(!merged&&!hull_polygon(p,n,triangles[i],&hull->faces[hull->face_count++])) {
            physics_hull_destroy(hull);return geometry_fail(error,"convex hull face is degenerate");
        }
    }
    unsigned uses[PHYSICS_POLY_MAX_EDGES]={0};
    for(unsigned i=0;i<hull->face_count;i++) {
        PhysicsHullFace* f=&hull->faces[i];
        for(unsigned point=0;point<n;point++)if(pdot(f->normal,psub(p[point],p[f->vertices[0]]))>8e-9) {
            physics_hull_destroy(hull);return geometry_fail(error,"convex hull cooking failed containment validation");
        }
        for(unsigned j=0;j<f->count;j++) {
            unsigned x=f->vertices[j],y=f->vertices[(j+1)%f->count];if(x>y){unsigned swap=x;x=y;y=swap;}
            unsigned k=0;for(;k<hull->edge_count;k++)if(hull->edges[k].a==x&&hull->edges[k].b==y)break;
            if(k==hull->edge_count) {
                if(k==PHYSICS_POLY_MAX_EDGES){physics_hull_destroy(hull);return geometry_fail(error,"convex hull edge quota exceeded");}
                hull->edges[hull->edge_count++]=(PhysicsHullEdge){x,y};
                PhysicsVec axis=punit(psub(p[y],p[x]));bool parallel=false;
                for(unsigned q=0;q<hull->axis_count;q++)if(plen(pcross(axis,hull->axes[q]))<=64*DBL_EPSILON){parallel=true;break;}
                if(!parallel)hull->axes[hull->axis_count++]=axis;
            }uses[k]++;
        }
    }
    for(unsigned i=0;i<hull->edge_count;i++)if(uses[i]!=2){physics_hull_destroy(hull);return geometry_fail(error,"convex hull is not a closed manifold");}
    if(!hull_integrals(hull,p,origin)){physics_hull_destroy(hull);return geometry_fail(error,"convex hull mass properties outside numerical range");}
    *out=hull;return true;
}
void physics_hull_destroy(PhysicsHull* hull) {if(hull){mem_free(hull->faces);mem_free(hull);}}

static PhysicsVec geometry_support_local(const PhysicsGeometry* g,PhysicsVec d) {
    const PhysicsColliderDesc* c=g->desc;
    if(g->triangle) {
        unsigned best=0;for(unsigned i=1;i<3;i++)if(pdot(g->triangle[i],d)>pdot(g->triangle[best],d))best=i;
        return g->triangle[best];
    }
    if(g->hull) {
        unsigned best=0;for(unsigned i=1;i<g->hull->vertex_count;i++)if(pdot(g->hull->vertices[i],d)>pdot(g->hull->vertices[best],d))best=i;
        return g->hull->vertices[best];
    }
    if(c->shape==PHYSICS_BOX)return pv((d.x<0?-1:1)*c->size.x/2,(d.y<0?-1:1)*c->size.y/2,(d.z<0?-1:1)*c->size.z/2);
    PhysicsVec tip=c->shape==PHYSICS_CAPSULE?pv(0,(d.y<0?-1:1)*c->height/2,0):pv(0,0,0);
    return padd(tip,pmul(punit(d),c->radius));
}
PhysicsVec physics_geometry_support(const PhysicsGeometry* g,PhysicsVec d) {
    return padd(g->pose.position,prot(g->pose.rotation,geometry_support_local(g,prot(pqconj(g->pose.rotation),d))));
}
bool physics_hull_contains(const PhysicsHull* hull,PhysicsVec p) {
    for(unsigned i=0;i<hull->face_count;i++)if(pdot(hull->faces[i].normal,psub(p,hull->vertices[hull->faces[i].vertices[0]]))>1e-9*hull->scale)return false;
    return true;
}
bool physics_hull_ray(const PhysicsHull* hull,PhysicsVec o,PhysicsVec d,double maximum,PhysicsHit* hit) {
    double near=0,far=maximum;PhysicsVec normal=pv(0,0,0);
    for(unsigned i=0;i<hull->face_count;i++) {
        const PhysicsHullFace* f=&hull->faces[i];double distance=pdot(f->normal,psub(o,hull->vertices[f->vertices[0]])),speed=pdot(f->normal,d);
        if(fabs(speed)<=32*DBL_EPSILON){if(distance>1e-9*hull->scale)return false;continue;}
        double time=-distance/speed;
        if(speed<0){if(time>near){near=time;normal=f->normal;}}else far=fmin(far,time);
        if(near>far)return false;
    }
    hit->distance=near;hit->point=padd(o,pmul(d,near));hit->normal=normal;return true;
}

unsigned pclip(const PhysicsVec* input,unsigned count,PhysicsVec* output,unsigned capacity,PhysicsVec axis,double plane) {
    unsigned n=0;if(!count)return 0;PhysicsVec prev=input[count-1];double dp=pdot(prev,axis)-plane;
    for(unsigned i=0;i<count;i++){PhysicsVec next=input[i];double dn=pdot(next,axis)-plane;
        if((dp<=0)!=(dn<=0)){if(n==capacity)return UINT_MAX;output[n++]=padd(prev,pmul(psub(next,prev),dp/(dp-dn)));}
        if(dn<=0){if(n==capacity)return UINT_MAX;output[n++]=next;}prev=next;dp=dn;}
    return n;
}
void psegments_closest(PhysicsVec a,PhysicsVec ae,PhysicsVec b,PhysicsVec be,PhysicsVec* pa,PhysicsVec* pb) {
    PhysicsVec u=psub(ae,a),v=psub(be,b),r=psub(a,b);
    double aa=pdot(u,u),bb=pdot(v,v),ab=pdot(u,v),ar=pdot(u,r),br=pdot(v,r),s=0,t=0;
    if(aa>0&&bb>0) {
        double denominator=aa*bb-ab*ab;
        if(denominator>1e-12*aa*bb)s=pclamp((ab*br-ar*bb)/denominator,0,1);
        else {
            // choose the middle of a parallel overlap rather than an arbitrary end contact.
            double low=pdot(psub(b,a),u)/aa,high=pdot(psub(be,a),u)/aa;
            if(low>high){double swap=low;low=high;high=swap;}
            s=pclamp((fmax(0,low)+fmin(1,high))/2,0,1);
        }
        t=(ab*s+br)/bb;
        if(t<0){t=0;s=pclamp(-ar/aa,0,1);}
        else if(t>1){t=1;s=pclamp((ab-ar)/aa,0,1);}
    } else if(aa>0)s=pclamp(-ar/aa,0,1);
    else if(bb>0)t=pclamp(br/bb,0,1);
    *pa=padd(a,pmul(u,s));*pb=padd(b,pmul(v,t));
}

typedef struct PolyFace {PhysicsVec normal,points[PHYSICS_CONVEX_MAX_VERTICES];unsigned count;} PolyFace;
static double geometry_scale(const PhysicsGeometry* g) {
    if(g->triangle)return fmax(plen(psub(g->triangle[1],g->triangle[0])),plen(psub(g->triangle[2],g->triangle[0])));
    return g->hull?g->hull->scale:g->desc->shape==PHYSICS_BOX?plen(g->desc->size):g->desc->radius*2+(g->desc->shape==PHYSICS_CAPSULE?g->desc->height:0);
}
static unsigned poly_faces(const PhysicsGeometry* g) {return g->triangle?1:g->hull?g->hull->face_count:6;}
static PhysicsVec poly_normal(const PhysicsGeometry* g,unsigned face) {
    if(g->triangle)return prot(g->pose.rotation,punit(pcross(psub(g->triangle[1],g->triangle[0]),psub(g->triangle[2],g->triangle[0]))));
    return prot(g->pose.rotation,g->hull?g->hull->faces[face].normal:pmul(pbase(face/2),face%2?1:-1));
}
static PhysicsVec geometry_point(const PhysicsGeometry* g,PhysicsVec p,PhysicsVec origin) {
    return padd(psub(g->pose.position,origin),prot(g->pose.rotation,p));
}
static void poly_face(const PhysicsGeometry* g,unsigned index,PhysicsVec origin,PolyFace* out) {
    out->normal=poly_normal(g,index);
    if(g->triangle) {out->count=3;for(unsigned i=0;i<3;i++)out->points[i]=geometry_point(g,g->triangle[i],origin);}
    else if(g->hull) {
        const PhysicsHullFace* f=&g->hull->faces[index];out->count=f->count;
        for(unsigned i=0;i<f->count;i++)out->points[i]=geometry_point(g,g->hull->vertices[f->vertices[i]],origin);
    } else {
        unsigned axis=index/2;double sign=index%2?1:-1;PhysicsVec h=pmul(g->desc->size,.5);
        PhysicsVec centre=pmul(pbase(axis),sign*paxis(h,axis)),u=pmul(pbase((axis+1)%3),paxis(h,(axis+1)%3));
        PhysicsVec v=pmul(pbase((axis+2)%3),paxis(h,(axis+2)%3));
        PhysicsVec corners[]={padd(centre,padd(u,v)),padd(centre,psub(v,u)),psub(centre,padd(u,v)),padd(centre,psub(u,v))};
        out->count=4;for(unsigned i=0;i<4;i++)out->points[i]=geometry_point(g,corners[sign>0?i:(4-i)%4],origin);
    }
}
static unsigned poly_axes(const PhysicsGeometry* g) {return g->triangle?3:g->hull?g->hull->axis_count:3;}
static PhysicsVec poly_axis(const PhysicsGeometry* g,unsigned i) {return prot(g->pose.rotation,g->triangle?punit(psub(g->triangle[(i+1)%3],g->triangle[i])):g->hull?g->hull->axes[i]:pbase(i));}
static void geometry_interval(const PhysicsGeometry* g,PhysicsVec axis,PhysicsVec origin,double* low,double* high) {
    PhysicsVec local=prot(pqconj(g->pose.rotation),axis);double centre=pdot(psub(g->pose.position,origin),axis);
    *high=centre+pdot(geometry_support_local(g,local),local);
    *low=centre+pdot(geometry_support_local(g,pmul(local,-1)),local);
}
typedef struct PolySeparation {PhysicsVec normal,edge_a,edge_b;double depth;unsigned kind;} PolySeparation;
static bool separation_axis(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsVec axis,
    double tolerance,unsigned kind,PolySeparation* result) {
    double length=plen(axis);if(length<=64*DBL_EPSILON)return true;axis=pmul(axis,1/length);
    double alo,ahi,blo,bhi;geometry_interval(a,axis,a->pose.position,&alo,&ahi);geometry_interval(b,axis,a->pose.position,&blo,&bhi);
    if(ahi<blo-tolerance||bhi<alo-tolerance)return false;
    double plus=ahi-blo,minus=bhi-alo,depth=fmin(plus,minus);
    if(depth<result->depth){result->depth=fmax(0,depth);result->normal=pmul(axis,plus<=minus?1:-1);result->kind=kind;}
    return true;
}
static bool geometry_contact(PhysicsGeometryContact* contacts,size_t capacity,size_t* count,
    PhysicsVec point,PhysicsVec normal,double depth,const char** error) {
    if(!pfinite(point)||!pfinite(normal)||!isfinite(depth))return geometry_fail(error,"convex contact outside numerical range");
    if(*count==capacity)return geometry_fail(error,"convex manifold quota exceeded");
    contacts[(*count)++]=(PhysicsGeometryContact){point,normal,fmax(0,depth)};return true;
}
static void poly_edge(const PhysicsGeometry* g,unsigned i,PhysicsVec origin,PhysicsVec* start,PhysicsVec* end) {
    if(g->triangle) {*start=geometry_point(g,g->triangle[i],origin);*end=geometry_point(g,g->triangle[(i+1)%3],origin);}
    else if(g->hull) {
        PhysicsHullEdge e=g->hull->edges[i];*start=geometry_point(g,g->hull->vertices[e.a],origin);*end=geometry_point(g,g->hull->vertices[e.b],origin);
    } else {
        unsigned axis=i/4,mask=i%4;PhysicsVec half=pmul(g->desc->size,.5);
        PhysicsVec p=padd(pmul(pbase((axis+1)%3),(mask&1?1:-1)*paxis(half,(axis+1)%3)),
            pmul(pbase((axis+2)%3),(mask&2?1:-1)*paxis(half,(axis+2)%3)));
        *start=geometry_point(g,psub(p,pmul(pbase(axis),paxis(half,axis))),origin);
        *end=geometry_point(g,padd(p,pmul(pbase(axis),paxis(half,axis))),origin);
    }
}
static void poly_support_edge(const PhysicsGeometry* g,PhysicsVec direction,PhysicsVec normal,PhysicsVec origin,PhysicsVec* start,PhysicsVec* end) {
    double best=-DBL_MAX;unsigned count=g->triangle?3:g->hull?g->hull->edge_count:12;
    for(unsigned i=0;i<count;i++) {
        PhysicsVec a,b;poly_edge(g,i,origin,&a,&b);
        if(plen(pcross(punit(psub(b,a)),direction))>1e-8)continue;
        double projected=pdot(pmul(padd(a,b),.5),normal);
        if(projected>best){best=projected;*start=a;*end=b;}
    }
}
static bool poly_contacts(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error) {
    double tolerance=1e-9*fmax(geometry_scale(a),geometry_scale(b));PolySeparation separation={.depth=DBL_MAX};
    const PhysicsGeometry* shapes[]={a,b};
    for(unsigned owner=0;owner<2;owner++)for(unsigned i=0;i<poly_faces(shapes[owner]);i++)
        if(!separation_axis(a,b,poly_normal(shapes[owner],i),tolerance,owner,&separation))return true;
    for(unsigned i=0;i<poly_axes(a);i++)for(unsigned j=0;j<poly_axes(b);j++) {
        PhysicsVec ea=poly_axis(a,i),eb=poly_axis(b,j);double before=separation.depth;
        if(!separation_axis(a,b,pcross(ea,eb),tolerance,2,&separation))return true;
        if(separation.depth<before){separation.edge_a=ea;separation.edge_b=eb;}
    }
    PhysicsVec origin=a->pose.position;
    if(separation.kind==2) {
        PhysicsVec as,ae,bs,be,pa,pb;poly_support_edge(a,separation.edge_a,separation.normal,origin,&as,&ae);
        poly_support_edge(b,separation.edge_b,pmul(separation.normal,-1),origin,&bs,&be);
        psegments_closest(as,ae,bs,be,&pa,&pb);
        return geometry_contact(contacts,capacity,count,padd(origin,pmul(padd(pa,pb),.5)),separation.normal,separation.depth,error);
    }
    unsigned faces[2]={0,0};double align[2]={-DBL_MAX,-DBL_MAX};
    for(unsigned owner=0;owner<2;owner++)for(unsigned i=0;i<poly_faces(shapes[owner]);i++) {
        double dot=pdot(poly_normal(shapes[owner],i),pmul(separation.normal,owner?-1:1));
        if(dot>align[owner]){align[owner]=dot;faces[owner]=i;}
    }
    unsigned reference=align[0]>=align[1]?0:1;PolyFace ref,incident;
    poly_face(shapes[reference],faces[reference],origin,&ref);poly_face(shapes[1-reference],faces[1-reference],origin,&incident);
    if(align[reference]>=1-1e-8) {
        PhysicsVec polygon[PHYSICS_MANIFOLD_MAX_POINTS],scratch[PHYSICS_MANIFOLD_MAX_POINTS];unsigned n=incident.count;
        memcpy(polygon,incident.points,n*sizeof(PhysicsVec));
        for(unsigned i=0;i<ref.count&&n;i++) {
            PhysicsVec edge=psub(ref.points[(i+1)%ref.count],ref.points[i]),outward=punit(pcross(edge,ref.normal));
            n=pclip(polygon,n,scratch,PHYSICS_MANIFOLD_MAX_POINTS,outward,pdot(ref.points[i],outward)+tolerance);
            if(n==UINT_MAX)return geometry_fail(error,"convex manifold clipping quota exceeded");
            memcpy(polygon,scratch,n*sizeof(PhysicsVec));
        }
        for(unsigned i=0;i<n;i++) {
            double depth=pdot(psub(ref.points[0],polygon[i]),ref.normal);
            if(depth>=-tolerance&&!geometry_contact(contacts,capacity,count,padd(origin,padd(polygon[i],pmul(ref.normal,fmax(0,depth)/2))),
                separation.normal,depth,error))return false;
        }
        if(*count)return true;
    }
    // containment can select a separating plane opposite a polytope's actual face.
    PhysicsVec pa=physics_geometry_support(a,separation.normal),pb=physics_geometry_support(b,pmul(separation.normal,-1));
    return geometry_contact(contacts,capacity,count,pmul(padd(pa,pb),.5),separation.normal,separation.depth,error);
}
static PhysicsVec triangle_closest_normalized(PhysicsVec a,PhysicsVec b,PhysicsVec c,PhysicsVec p) {
    PhysicsVec ab=psub(b,a),ac=psub(c,a),ap=psub(p,a);double d1=pdot(ab,ap),d2=pdot(ac,ap);
    if(d1<=0&&d2<=0)return a;
    PhysicsVec bp=psub(p,b);double d3=pdot(ab,bp),d4=pdot(ac,bp);if(d3>=0&&d4<=d3)return b;
    double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return padd(a,pmul(ab,d1/(d1-d3)));
    PhysicsVec cp=psub(p,c);double d5=pdot(ab,cp),d6=pdot(ac,cp);if(d6>=0&&d5<=d6)return c;
    double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return padd(a,pmul(ac,d2/(d2-d6)));
    // both edge barycentric weights must be nonnegative; reversing the second admits exterior face points.
    double va=d3*d6-d5*d4;if(va<=0&&d4>=d3&&d5>=d6)return padd(b,pmul(psub(c,b),(d4-d3)/(d4-d3+d5-d6)));
    return padd(a,padd(pmul(ab,vb/(va+vb+vc)),pmul(ac,vc/(va+vb+vc))));
}
PhysicsVec physics_triangle_closest(PhysicsVec a,PhysicsVec b,PhysicsVec c,PhysicsVec p) {
    PhysicsVec ab=psub(b,a),ac=psub(c,a);double scale=fmax(plen(ab),plen(ac));
    // barycentric products are fourth order in edge length; normalize before forming them.
    if(!isfinite(scale)||scale<=0)return pv(NAN,NAN,NAN);
    PhysicsVec closest=triangle_closest_normalized(pv(0,0,0),pmul(ab,1/scale),pmul(ac,1/scale),pmul(psub(p,a),1/scale));
    return padd(a,pmul(closest,scale));
}
static void closest_candidate(PhysicsVec a,PhysicsVec b,double* distance,PhysicsVec* pa,PhysicsVec* pb) {
    double next=plen(psub(a,b));if(next<*distance){*distance=next;*pa=a;*pb=b;}
}
static void segment_triangle(PhysicsVec start,PhysicsVec end,PhysicsVec a,PhysicsVec b,PhysicsVec c,double* distance,PhysicsVec* pa,PhysicsVec* pb) {
    PhysicsVec normal=punit(pcross(psub(b,a),psub(c,a))),delta=psub(end,start);double denominator=pdot(delta,normal);
    if(denominator!=0) {
        double t=pdot(psub(a,start),normal)/denominator;
        if(t>=0&&t<=1) {
            PhysicsVec p=padd(start,pmul(delta,t));
            if(pdot(pcross(psub(b,a),psub(p,a)),normal)>=0&&pdot(pcross(psub(c,b),psub(p,b)),normal)>=0&&
                pdot(pcross(psub(a,c),psub(p,c)),normal)>=0)closest_candidate(p,p,distance,pa,pb);
        }
    }
    closest_candidate(start,physics_triangle_closest(a,b,c,start),distance,pa,pb);
    closest_candidate(end,physics_triangle_closest(a,b,c,end),distance,pa,pb);
    PhysicsVec points[]={a,b,c};
    for(unsigned i=0;i<3;i++){PhysicsVec p,q;psegments_closest(start,end,points[i],points[(i+1)%3],&p,&q);closest_candidate(p,q,distance,pa,pb);}
}
static double poly_segment_closest(const PhysicsGeometry* poly,PhysicsVec start,PhysicsVec end,PhysicsVec origin,PhysicsVec* pa,PhysicsVec* pb) {
    double distance=DBL_MAX;
    for(unsigned i=0;i<poly_faces(poly);i++) {
        PolyFace face;poly_face(poly,i,origin,&face);
        for(unsigned j=1;j+1<face.count;j++)segment_triangle(start,end,face.points[0],face.points[j],face.points[j+1],&distance,pa,pb);
    }return distance;
}
static bool poly_segment_intersects(const PhysicsGeometry* poly,PhysicsVec start,PhysicsVec end,PhysicsVec origin) {
    double near=0,far=1;
    for(unsigned i=0;i<poly_faces(poly);i++) {
        PolyFace f;poly_face(poly,i,origin,&f);double d=pdot(psub(start,f.points[0]),f.normal),v=pdot(psub(end,start),f.normal);
        if(v==0){if(d>0)return false;continue;}
        double t=-d/v;if(v<0)near=fmax(near,t);else far=fmin(far,t);if(near>far)return false;
    }return true;
}
static bool round_poly_contacts(const PhysicsGeometry* poly,const PhysicsGeometry* round,bool flip,
    PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error) {
    PhysicsVec origin=poly->pose.position,half=round->desc->shape==PHYSICS_CAPSULE?prot(round->pose.rotation,pv(0,round->desc->height/2,0)):pv(0,0,0);
    PhysicsVec centre=psub(round->pose.position,origin),start=psub(centre,half),end=padd(centre,half),pa,pb;
    double tolerance=1e-9*fmax(geometry_scale(poly),geometry_scale(round));
    double distance=poly_segment_closest(poly,start,end,origin,&pa,&pb),depth=round->desc->radius-distance;PhysicsVec normal;
    if(poly->triangle&&distance<=tolerance) {
        normal=poly_normal(poly,0);
        PhysicsVec base=geometry_point(poly,poly->triangle[0],origin);
        depth=round->desc->radius-fmin(pdot(psub(start,base),normal),pdot(psub(end,base),normal));
    } else if(!poly->triangle&&poly_segment_intersects(poly,start,end,origin)) {
        PolySeparation separation={.depth=DBL_MAX};
        for(unsigned i=0;i<poly_faces(poly);i++)if(!separation_axis(poly,round,poly_normal(poly,i),tolerance,0,&separation))return true;
        if(plen(half)>0)for(unsigned i=0;i<poly_axes(poly);i++)
            if(!separation_axis(poly,round,pcross(punit(half),poly_axis(poly,i)),tolerance,0,&separation))return true;
        normal=separation.normal;depth=separation.depth;
        pa=pdot(half,normal)>0?start:end;
        if(fabs(pdot(punit(half),normal))<=1e-12)pa=psegment_closest(start,end,geometry_point(poly,poly->hull->centre,origin));
        pb=padd(pa,pmul(normal,depth/2-round->desc->radius));
    } else {
        if(depth< -tolerance)return true;normal=punit(psub(pa,pb));
        if(plen(half)>0)for(unsigned i=0;i<2;i++) {
            PhysicsVec tip=i?end:start,x,y;double d=poly_segment_closest(poly,tip,tip,origin,&x,&y);
            if(d>0&&d<=round->desc->radius+tolerance&&pdot(punit(psub(x,y)),normal)>=1-1e-9) {
                if(!geometry_contact(contacts,capacity,count,padd(origin,y),pmul(normal,flip?-1:1),round->desc->radius-d,error))return false;
            }
        }
        if(*count)return true;
    }
    return geometry_contact(contacts,capacity,count,padd(origin,pb),pmul(normal,flip?-1:1),depth,error);
}
bool physics_convex_contacts(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error) {
    *count=0;
    if(a->mesh||b->mesh) {
        if(a->mesh&&b->mesh)return geometry_fail(error,"mesh/mesh contacts are unsupported");
        return physics_mesh_contacts(a->mesh?a:b,a->mesh?b:a,!a->mesh,contacts,capacity,count,error);
    }
    if((a->triangle||a->hull||a->desc->shape==PHYSICS_BOX)&&(b->triangle||b->hull||b->desc->shape==PHYSICS_BOX))return poly_contacts(a,b,contacts,capacity,count,error);
    if(a->triangle||a->hull)return round_poly_contacts(a,b,false,contacts,capacity,count,error);
    if(b->triangle||b->hull)return round_poly_contacts(b,a,true,contacts,capacity,count,error);
    return geometry_fail(error,"convex contact requires a cooked hull");
}

typedef struct GjkVertex {PhysicsVec point,a,b;} GjkVertex;
static GjkVertex gjk_support(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsVec direction) {
    PhysicsVec pa=prot(a->pose.rotation,geometry_support_local(a,prot(pqconj(a->pose.rotation),direction)));
    PhysicsVec pb=padd(psub(b->pose.position,a->pose.position),
        prot(b->pose.rotation,geometry_support_local(b,prot(pqconj(b->pose.rotation),pmul(direction,-1)))));
    return (GjkVertex){psub(pa,pb),pa,pb};
}
static bool gjk_subset(const GjkVertex* simplex,unsigned mask,double weights[4],PhysicsVec* nearest) {
    unsigned indices[4],count=0;for(unsigned i=0;i<4;i++)if(mask&(1u<<i))indices[count++]=i;
    memset(weights,0,4*sizeof(double));weights[indices[0]]=1;
    if(count>1) {
        PhysicsVec basis[3];double lengths[3],matrix[3][4]={{0}};
        for(unsigned i=0;i<count-1;i++) {
            basis[i]=psub(simplex[indices[i+1]].point,simplex[indices[0]].point);lengths[i]=plen(basis[i]);
            if(lengths[i]<=DBL_MIN)return false;basis[i]=pmul(basis[i],1/lengths[i]);
        }
        for(unsigned r=0;r<count-1;r++) {
            for(unsigned col=0;col<count-1;col++)matrix[r][col]=pdot(basis[r],basis[col]);
            matrix[r][count-1]=-pdot(basis[r],simplex[indices[0]].point);
        }
        // normalized columns keep a short edge from masquerading as angular degeneracy.
        for(unsigned col=0;col<count-1;col++) {
            unsigned pivot=col;for(unsigned r=col+1;r<count-1;r++)if(fabs(matrix[r][col])>fabs(matrix[pivot][col]))pivot=r;
            if(fabs(matrix[pivot][col])<=64*DBL_EPSILON)return false;
            for(unsigned j=col;j<count;j++){double swap=matrix[col][j];matrix[col][j]=matrix[pivot][j];matrix[pivot][j]=swap;}
            double diagonal=matrix[col][col];for(unsigned j=col;j<count;j++)matrix[col][j]/=diagonal;
            for(unsigned r=0;r<count-1;r++)if(r!=col){double factor=matrix[r][col];for(unsigned j=col;j<count;j++)matrix[r][j]-=factor*matrix[col][j];}
        }
        for(unsigned i=0;i<count-1;i++){double weight=matrix[i][count-1]/lengths[i];weights[indices[i+1]]=weight;weights[indices[0]]-=weight;}
    }
    double total=0;for(unsigned i=0;i<4;i++){if(weights[i]< -64*DBL_EPSILON)return false;weights[i]=fmax(0,weights[i]);total+=weights[i];}
    *nearest=pv(0,0,0);for(unsigned i=0;i<4;i++)if(weights[i]>0){weights[i]/=total;*nearest=padd(*nearest,pmul(simplex[i].point,weights[i]));}
    return pfinite(*nearest);
}
static bool gjk_reduce(GjkVertex simplex[4],unsigned* count,PhysicsVec* nearest,PhysicsVec* pa,PhysicsVec* pb) {
    double best=DBL_MAX,selected[4]={0};
    for(unsigned mask=1;mask<(1u<<*count);mask++) {
        double weights[4];PhysicsVec point;if(!gjk_subset(simplex,mask,weights,&point))continue;
        double distance=plen(point);if(distance<best){best=distance;memcpy(selected,weights,sizeof(selected));*nearest=point;}
    }
    if(best==DBL_MAX)return false;
    *pa=*pb=pv(0,0,0);unsigned kept=0;
    for(unsigned i=0;i<*count;i++)if(selected[i]>0) {
        *pa=padd(*pa,pmul(simplex[i].a,selected[i]));*pb=padd(*pb,pmul(simplex[i].b,selected[i]));simplex[kept++]=simplex[i];
    }
    *count=kept;return kept!=0;
}
bool physics_geometry_distance(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsHit* hit,const char** error) {
    if(a->mesh||b->mesh)return physics_mesh_distance(a,b,hit,error);
    PhysicsVec direction=psub(b->pose.position,a->pose.position);if(plen(direction)==0)direction=pv(1,0,0);
    double scale=fmax(geometry_scale(a),geometry_scale(b));
    double coordinate=plen(direction)+(a->hull?plen(a->hull->centre):0)+(b->hull?plen(b->hull->centre):0);
    double tolerance=1e-9*scale+128*DBL_EPSILON*coordinate;
    GjkVertex simplex[4]={gjk_support(a,b,direction)};unsigned count=1;
    for(unsigned iteration=0;iteration<128;iteration++) {
        PhysicsVec nearest,pa,pb;if(!gjk_reduce(simplex,&count,&nearest,&pa,&pb))return geometry_fail(error,"convex distance simplex is degenerate");
        double distance=plen(nearest);if(!isfinite(distance))return geometry_fail(error,"convex distance outside numerical range");
        GjkVertex next=gjk_support(a,b,pmul(nearest,-1));
        double gap=pdot(nearest,psub(nearest,next.point));
        if(distance<=tolerance||gap<=tolerance*distance) {
            hit->found=true;hit->distance=distance<=tolerance?0:distance;hit->overlapping=distance<=tolerance;
            hit->point=padd(a->pose.position,pa);hit->point_b=padd(a->pose.position,pb);
            hit->normal=hit->overlapping?pv(0,0,0):pmul(psub(pb,pa),1/distance);return true;
        }
        if(count==4)return geometry_fail(error,"convex distance simplex failed to reduce");
        bool duplicate=false;for(unsigned i=0;i<count;i++)if(plen(psub(next.point,simplex[i].point))<=64*DBL_EPSILON*scale){duplicate=true;break;}
        if(duplicate)return geometry_fail(error,"convex distance stalled before convergence");
        simplex[count++]=next;
    }
    return geometry_fail(error,"convex distance iteration budget exceeded");
}
