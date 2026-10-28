#include "physics_geometry.h"
#include "../mem.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MESH_LEAF_SIZE 4
#define MESH_VERTEX_FAN_LIMIT 128
#define MESH_PATCH_LIMIT (PHYSICS_MANIFOLD_MAX_POINTS / 4)
typedef struct MeshTriangle {uint32_t v[3],adjacent[3];PhysicsVec normal;} MeshTriangle;
typedef struct MeshNode {PhysicsVec low,high;uint32_t start,count,left,right;} MeshNode;
typedef struct MeshOrder {uint32_t triangle;double key;} MeshOrder;
struct PhysicsMesh {
    PhysicsVec* vertices;
    uint32_t* indices;
    double* heights;
    MeshTriangle* triangles;
    MeshNode* nodes;
    MeshOrder* order;
    uint32_t *fan_offsets,*fan;
    size_t vertex_count,triangle_count,node_count;
    double tolerance;
};
typedef struct MeshVertex {PhysicsVec point;uint32_t original;} MeshVertex;
typedef struct MeshEdge {uint32_t low,high,triangle,slot;bool reversed;} MeshEdge;
static bool mesh_fail(const char** error,const char* message) {if(error)*error=message;return false;}
static PhysicsVec mesh_min(PhysicsVec a,PhysicsVec b) {return pv(fmin(a.x,b.x),fmin(a.y,b.y),fmin(a.z,b.z));}
static PhysicsVec mesh_max(PhysicsVec a,PhysicsVec b) {return pv(fmax(a.x,b.x),fmax(a.y,b.y),fmax(a.z,b.z));}
static int mesh_vertex_compare(const void* left,const void* right) {
    const MeshVertex *a=left,*b=right;
    for(unsigned i=0;i<3;i++){double x=paxis(a->point,i),y=paxis(b->point,i);if(x!=y)return x<y?-1:1;}
    return a->original<b->original?-1:a->original>b->original;
}
static int mesh_edge_compare(const void* left,const void* right) {
    const MeshEdge *a=left,*b=right;
    if(a->low!=b->low)return a->low<b->low?-1:1;
    if(a->high!=b->high)return a->high<b->high?-1:1;
    return a->triangle<b->triangle?-1:a->triangle>b->triangle;
}
static int mesh_order_compare(const void* left,const void* right) {
    const MeshOrder *a=left,*b=right;
    return a->key<b->key?-1:a->key>b->key?1:a->triangle<b->triangle?-1:a->triangle>b->triangle;
}
static void mesh_triangle_points(const PhysicsMesh* m,uint32_t index,PhysicsVec points[3]) {
    for(unsigned i=0;i<3;i++)points[i]=m->vertices[m->triangles[index].v[i]];
}
static uint32_t mesh_node_build(PhysicsMesh* m,uint32_t start,uint32_t count) {
    uint32_t index=(uint32_t)m->node_count++;MeshNode* node=&m->nodes[index];node->start=start;node->count=count;
    PhysicsVec low=pv(DBL_MAX,DBL_MAX,DBL_MAX),high=pmul(low,-1),clow=low,chigh=high;
    for(uint32_t i=start;i<start+count;i++) {
        PhysicsVec p[3];mesh_triangle_points(m,m->order[i].triangle,p);
        PhysicsVec centre=padd(pmul(p[0],1.0/3),padd(pmul(p[1],1.0/3),pmul(p[2],1.0/3)));
        clow=mesh_min(clow,centre);chigh=mesh_max(chigh,centre);
        for(unsigned j=0;j<3;j++){low=mesh_min(low,p[j]);high=mesh_max(high,p[j]);}
    }
    node->low=low;node->high=high;if(count<=MESH_LEAF_SIZE)return index;
    PhysicsVec span=psub(chigh,clow);unsigned axis=0;for(unsigned i=1;i<3;i++)if(paxis(span,i)>paxis(span,axis))axis=i;
    for(uint32_t i=start;i<start+count;i++) {
        PhysicsVec p[3];mesh_triangle_points(m,m->order[i].triangle,p);
        m->order[i].key=paxis(p[0],axis)/3+paxis(p[1],axis)/3+paxis(p[2],axis)/3;
    }
    // stable triangle IDs break centroid ties; median splits bound traversal depth even on flat grids.
    qsort(m->order+start,count,sizeof(MeshOrder),mesh_order_compare);
    node->count=0;node->left=mesh_node_build(m,start,count/2);node->right=mesh_node_build(m,start+count/2,count-count/2);return index;
}
void physics_mesh_destroy(PhysicsMesh* m) {
    if(!m)return;mem_free(m->vertices);mem_free(m->indices);mem_free(m->heights);mem_free(m->triangles);
    mem_free(m->nodes);mem_free(m->order);mem_free(m->fan_offsets);mem_free(m->fan);mem_free(m);
}
bool physics_mesh_input_counts(const PhysicsColliderDesc* d,size_t* vertex_count,size_t* triangle_count,const char** error) {
    if(!d||(d->shape!=PHYSICS_MESH&&d->shape!=PHYSICS_HEIGHTFIELD))return mesh_fail(error,"mesh requires a surface descriptor");
    bool grid=d->shape==PHYSICS_HEIGHTFIELD;size_t vertices=d->vertex_count,triangles=d->index_count/3;
    if(grid) {
        if(!d->heights||d->rows<2||d->columns<2||d->rows>PHYSICS_WORLD_MAX_VERTICES/d->columns||
            !isfinite(d->cell_width)||!isfinite(d->cell_depth)||d->cell_width<=0||d->cell_depth<=0)
            return mesh_fail(error,"heightfield requires bounded rows, columns, heights and positive cell size");
        vertices=d->rows*d->columns;triangles=2*(d->rows-1)*(d->columns-1);
    } else if(!d->vertices||!d->indices||d->index_count%3)return mesh_fail(error,"mesh requires indexed vertex triples");
    if(vertices<3||vertices>PHYSICS_WORLD_MAX_VERTICES||!triangles||triangles>PHYSICS_MESH_MAX_TRIANGLES)
        return mesh_fail(error,"mesh vertex or triangle quota exceeded");
    *vertex_count=vertices;*triangle_count=triangles;return true;
}
bool physics_mesh_create(const PhysicsColliderDesc* d,PhysicsMesh** out,const char** error) {
    if(!out)return mesh_fail(error,"mesh requires an output");*out=NULL;
    size_t vertices,triangles;if(!physics_mesh_input_counts(d,&vertices,&triangles,error))return false;
    bool grid=d->shape==PHYSICS_HEIGHTFIELD;
    PhysicsMesh* m=mem_calloc(1,sizeof(*m),MEM_CAT_EVAL);
    MeshVertex* sorted=mem_alloc(vertices*sizeof(*sorted),MEM_CAT_EVAL);
    uint32_t* remap=mem_alloc(vertices*sizeof(*remap),MEM_CAT_EVAL);
    MeshEdge* edges=mem_alloc(3*triangles*sizeof(*edges),MEM_CAT_EVAL);const char* failure=NULL;
    if(!m||!sorted||!remap||!edges){failure="mesh allocation failed";goto done;}
    m->vertices=mem_alloc(vertices*sizeof(PhysicsVec),MEM_CAT_EVAL);
    m->indices=mem_alloc(3*triangles*sizeof(uint32_t),MEM_CAT_EVAL);
    m->triangles=mem_calloc(triangles,sizeof(MeshTriangle),MEM_CAT_EVAL);
    m->nodes=mem_calloc(2*triangles,sizeof(MeshNode),MEM_CAT_EVAL);
    m->order=mem_calloc(triangles,sizeof(MeshOrder),MEM_CAT_EVAL);
    m->fan_offsets=mem_calloc(vertices+1,sizeof(uint32_t),MEM_CAT_EVAL);
    m->fan=mem_alloc(3*triangles*sizeof(uint32_t),MEM_CAT_EVAL);
    if(grid)m->heights=mem_alloc(vertices*sizeof(double),MEM_CAT_EVAL);
    if(!m->vertices||!m->indices||!m->triangles||!m->nodes||!m->order||!m->fan_offsets||!m->fan||(grid&&!m->heights)) {
        failure="mesh allocation failed";goto done;
    }
    for(size_t i=0;i<vertices;i++) {
        PhysicsVec p=grid?pv((i%d->columns)*d->cell_width,d->heights[i],(i/d->columns)*d->cell_depth):d->vertices[i];
        if(!pfinite(p)){failure="mesh requires finite vertices and heights";goto done;}
        sorted[i]=(MeshVertex){p,(uint32_t)i};if(grid)m->heights[i]=d->heights[i];
    }
    qsort(sorted,vertices,sizeof(*sorted),mesh_vertex_compare);
    for(size_t i=0;i<vertices;i++) {
        if(!i||sorted[i].point.x!=sorted[i-1].point.x||sorted[i].point.y!=sorted[i-1].point.y||sorted[i].point.z!=sorted[i-1].point.z)
            m->vertices[m->vertex_count++]=sorted[i].point;
        remap[sorted[i].original]=(uint32_t)m->vertex_count-1;
    }
    for(size_t i=0;i<triangles;i++) {
        uint32_t source[3];
        if(grid) {
            size_t cell=i/2,row=cell/(d->columns-1),column=cell%(d->columns-1);uint32_t a=(uint32_t)(row*d->columns+column),n=(uint32_t)d->columns;
            source[0]=i%2?a+1:a;source[1]=a+n;source[2]=i%2?a+n+1:a+1;
        } else for(unsigned j=0;j<3;j++)source[j]=d->indices[3*i+j];
        MeshTriangle* t=&m->triangles[i];
        for(unsigned j=0;j<3;j++) {
            if(source[j]>=vertices){failure="mesh index outside vertex range";goto done;}
            t->v[j]=remap[source[j]];m->indices[3*i+j]=t->v[j];t->adjacent[j]=UINT32_MAX;
            if(++m->fan_offsets[t->v[j]+1]>MESH_VERTEX_FAN_LIMIT){failure="mesh vertex adjacency quota exceeded";goto done;}
        }
        PhysicsVec p[3];mesh_triangle_points(m,(uint32_t)i,p);
        PhysicsVec ab=psub(p[1],p[0]),ac=psub(p[2],p[0]);double scale=fmax(plen(ab),plen(ac));
        if(!isfinite(scale)||scale<=0){failure="mesh has a degenerate triangle";goto done;}
        PhysicsVec normal=pcross(pmul(ab,1/scale),pmul(ac,1/scale));double area=plen(normal);
        if(!isfinite(area)||area<=1e-12){failure="mesh has a degenerate triangle";goto done;}
        t->normal=pmul(normal,1/area);
        for(unsigned j=0;j<3;j++) {
            uint32_t a=t->v[j],b=t->v[(j+1)%3];edges[3*i+j]=(MeshEdge){a<b?a:b,a<b?b:a,(uint32_t)i,j,a>b};
        }
        m->order[i].triangle=(uint32_t)i;
    }
    qsort(edges,3*triangles,sizeof(*edges),mesh_edge_compare);
    for(size_t i=0;i<3*triangles;) {
        size_t end=i+1;while(end<3*triangles&&edges[end].low==edges[i].low&&edges[end].high==edges[i].high)end++;
        if(end-i>2){failure="mesh has a nonmanifold edge";goto done;}
        if(end-i==2) {
            MeshEdge a=edges[i],b=edges[i+1];
            if(a.reversed==b.reversed){failure="mesh adjacent triangles have inconsistent winding";goto done;}
            m->triangles[a.triangle].adjacent[a.slot]=b.triangle;m->triangles[b.triangle].adjacent[b.slot]=a.triangle;
        }i=end;
    }
    // opposite duplicate triangles are also invalid, even though their edge directions agree.
    for(size_t i=0;i<triangles;i++) {
        MeshTriangle* t=&m->triangles[i];uint32_t a=t->adjacent[0];
        if(a!=UINT32_MAX&&a==t->adjacent[1]&&a==t->adjacent[2]){failure="mesh has duplicate triangles";goto done;}
    }
    for(size_t i=1;i<=m->vertex_count;i++)m->fan_offsets[i]+=m->fan_offsets[i-1];
    memcpy(remap,m->fan_offsets,m->vertex_count*sizeof(uint32_t));
    for(size_t i=0;i<triangles;i++)for(unsigned j=0;j<3;j++)m->fan[remap[m->triangles[i].v[j]]++]=(uint32_t)i;
    m->triangle_count=triangles;mesh_node_build(m,0,(uint32_t)triangles);
    // query tolerance follows local coordinates; a distant unused vertex cannot inflate it.
    PhysicsVec span=psub(m->nodes[0].high,m->nodes[0].low);
    m->tolerance=128*DBL_EPSILON*fmax(plen(m->nodes[0].low),plen(m->nodes[0].high))+1e-9*fmax(span.x,fmax(span.y,span.z));
    if(!isfinite(m->tolerance)){failure="mesh bounds outside numerical range";goto done;}
    *out=m;
done:
    mem_free(sorted);mem_free(remap);mem_free(edges);
    if(failure){physics_mesh_destroy(m);return mesh_fail(error,failure);}return true;
}
void physics_mesh_bind(const PhysicsMesh* m,PhysicsColliderDesc* d) {
    d->vertices=m->vertices;d->vertex_count=m->vertex_count;d->indices=m->indices;d->index_count=3*m->triangle_count;d->heights=m->heights;
}
void physics_mesh_bounds(const PhysicsMesh* m,PhysicsVec* low,PhysicsVec* high) {*low=m->nodes[0].low;*high=m->nodes[0].high;}
static bool mesh_budget(size_t* work,const char** error) {
    if(++*work>PHYSICS_MESH_QUERY_BUDGET)return mesh_fail(error,"mesh query triangle work quota exceeded");return true;
}
static bool mesh_box_overlap(PhysicsVec al,PhysicsVec ah,PhysicsVec bl,PhysicsVec bh,double tolerance) {
    for(unsigned i=0;i<3;i++)if(paxis(al,i)>paxis(bh,i)+tolerance||paxis(bl,i)>paxis(ah,i)+tolerance)return false;return true;
}
static double mesh_box_distance(PhysicsVec al,PhysicsVec ah,PhysicsVec bl,PhysicsVec bh) {
    PhysicsVec gap=pv(0,0,0);
    gap.x=fmax(0,fmax(al.x-bh.x,bl.x-ah.x));gap.y=fmax(0,fmax(al.y-bh.y,bl.y-ah.y));gap.z=fmax(0,fmax(al.z-bh.z,bl.z-ah.z));return plen(gap);
}
static bool mesh_ray_box(const MeshNode* node,PhysicsVec o,PhysicsVec d,double maximum,double* near) {
    *near=0;double far=maximum;
    for(unsigned i=0;i<3;i++) {
        double p=paxis(o,i),v=paxis(d,i),low=paxis(node->low,i),high=paxis(node->high,i);
        if(fabs(v)<=DBL_MIN){if(p<low||p>high)return false;continue;}
        double a=(low-p)/v,b=(high-p)/v;if(a>b){double swap=a;a=b;b=swap;}
        *near=fmax(*near,a);far=fmin(far,b);if(*near>far)return false;
    }return true;
}
bool physics_mesh_ray(const PhysicsMesh* m,bool double_sided,PhysicsVec origin,PhysicsVec direction,double maximum,PhysicsHit* hit,const char** error) {
    if(!pfinite(origin)||!pfinite(direction))return mesh_fail(error,"mesh ray outside numerical range");
    uint32_t stack[64]={0};unsigned pending=1;double best=maximum;size_t work=0;uint32_t selected=UINT32_MAX;
    while(pending) {
        const MeshNode* node=&m->nodes[stack[--pending]];double near;if(!mesh_ray_box(node,origin,direction,best,&near))continue;
        if(!node->count) {
            double a,b;bool left=mesh_ray_box(&m->nodes[node->left],origin,direction,best,&a),right=mesh_ray_box(&m->nodes[node->right],origin,direction,best,&b);
            if(left&&right){stack[pending++]=a<=b?node->right:node->left;stack[pending++]=a<=b?node->left:node->right;}
            else if(left||right)stack[pending++]=left?node->left:node->right;continue;
        }
        for(uint32_t i=node->start;i<node->start+node->count;i++) {
            if(!mesh_budget(&work,error))return false;uint32_t index=m->order[i].triangle;PhysicsVec p[3];mesh_triangle_points(m,index,p);
            PhysicsVec n=m->triangles[index].normal;double denominator=pdot(n,direction);
            if(double_sided?fabs(denominator)<=64*DBL_EPSILON:denominator>=-64*DBL_EPSILON)continue;
            double distance=pdot(psub(p[0],origin),n)/denominator;if(distance<0||distance>best)continue;
            PhysicsVec point=padd(origin,pmul(direction,distance)),closest=physics_triangle_closest(p[0],p[1],p[2],point);
            if(!pfinite(point)||!pfinite(closest))return mesh_fail(error,"mesh ray outside numerical range");
            if(plen(psub(closest,point))>m->tolerance)continue;
            if(selected!=UINT32_MAX&&distance==best&&index>selected)continue;
            selected=index;best=distance;hit->point=point;hit->normal=denominator<0?n:pmul(n,-1);
        }
    }
    hit->triangle_tests=work;hit->found=selected!=UINT32_MAX;hit->distance=hit->found?best:0;return true;
}
bool physics_mesh_point(const PhysicsMesh* m,PhysicsVec point,PhysicsHit* hit,const char** error) {
    if(!pfinite(point))return mesh_fail(error,"mesh point outside numerical range");
    uint32_t stack[64]={0};unsigned pending=1;size_t work=0;
    while(pending) {
        const MeshNode* node=&m->nodes[stack[--pending]];
        if(!mesh_box_overlap(node->low,node->high,point,point,m->tolerance))continue;
        if(!node->count){stack[pending++]=node->right;stack[pending++]=node->left;continue;}
        for(uint32_t i=node->start;i<node->start+node->count;i++) {
            if(!mesh_budget(&work,error))return false;uint32_t index=m->order[i].triangle;PhysicsVec p[3];mesh_triangle_points(m,index,p);
            PhysicsVec closest=physics_triangle_closest(p[0],p[1],p[2],point);
            if(!pfinite(closest))return mesh_fail(error,"mesh point outside numerical range");
            if(plen(psub(point,closest))<=m->tolerance) {
                hit->found=true;hit->normal=m->triangles[index].normal;hit->triangle_tests=work;return true;
            }
        }
    }hit->triangle_tests=work;return true;
}
static PhysicsGeometry mesh_local_geometry(const PhysicsGeometry* shape,PhysicsPose frame) {
    PhysicsGeometry local=*shape;PhysicsQuat inverse=pqconj(frame.rotation);
    local.pose.position=prot(inverse,psub(shape->pose.position,frame.position));local.pose.rotation=pqmul(inverse,shape->pose.rotation);return local;
}
static void mesh_convex_bounds(const PhysicsGeometry* g,PhysicsVec* low,PhysicsVec* high) {
    PhysicsVec p[3],q[3];for(unsigned i=0;i<3;i++){p[i]=physics_geometry_support(g,pbase(i));q[i]=physics_geometry_support(g,pmul(pbase(i),-1));}
    *low=pv(q[0].x,q[1].y,q[2].z);*high=pv(p[0].x,p[1].y,p[2].z);
}
static bool mesh_internal_feature(const PhysicsMesh* m,uint32_t index,PhysicsVec point,PhysicsVec normal) {
    const MeshTriangle* t=&m->triangles[index];if(fabs(pdot(normal,t->normal))>=1-1e-8)return false;
    double tolerance=m->tolerance*4;
    for(unsigned j=0;j<3;j++)if(plen(psub(point,m->vertices[t->v[j]]))<=tolerance) {
        // a vertex is internal only when its entire fan is closed and coplanar; boundaries keep their features.
        for(uint32_t k=m->fan_offsets[t->v[j]];k<m->fan_offsets[t->v[j]+1];k++) {
            const MeshTriangle* f=&m->triangles[m->fan[k]];if(pdot(f->normal,t->normal)<1-1e-8)return false;
            for(unsigned e=0;e<3;e++)if((f->v[e]==t->v[j]||f->v[(e+1)%3]==t->v[j])&&f->adjacent[e]==UINT32_MAX)return false;
        }return true;
    }
    for(unsigned e=0;e<3;e++)if(t->adjacent[e]!=UINT32_MAX&&pdot(t->normal,m->triangles[t->adjacent[e]].normal)>=1-1e-8) {
        PhysicsVec p=psegment_closest(m->vertices[t->v[e]],m->vertices[t->v[(e+1)%3]],point);
        if(plen(psub(point,p))<=tolerance)return true;
    }return false;
}
typedef struct MeshPatch {PhysicsVec normal;double plane;PhysicsGeometryContact contacts[4];unsigned count;} MeshPatch;
static double mesh_patch_score(const PhysicsGeometryContact* c,unsigned skip,PhysicsVec normal) {
    PhysicsVec p[4];unsigned n=0;for(unsigned i=0;i<5;i++)if(i!=skip)p[n++]=c[i].point;
    // projected triangle areas choose spatial coverage rather than retaining a diagonal's clustered samples.
    double area=0;for(unsigned i=0;i<4;i++)for(unsigned j=i+1;j<4;j++)for(unsigned k=j+1;k<4;k++)
        area+=fabs(pdot(pcross(psub(p[j],p[i]),psub(p[k],p[i])),normal));return area;
}
static bool mesh_patch_add(MeshPatch* patches,unsigned* count,PhysicsGeometryContact c,double plane,double tolerance,const char** error) {
    unsigned i=0;for(;i<*count;i++)if(pdot(patches[i].normal,c.normal)>1-1e-8&&fabs(patches[i].plane-plane)<=tolerance)break;
    if(i==*count) {
        if(i==MESH_PATCH_LIMIT)return mesh_fail(error,"mesh contact patch quota exceeded");
        patches[i]=(MeshPatch){.normal=c.normal,.plane=plane};(*count)++;
    }
    MeshPatch* p=&patches[i];
    for(unsigned j=0;j<p->count;j++)if(plen(psub(c.point,p->contacts[j].point))<=tolerance) {
        if(c.depth>p->contacts[j].depth)p->contacts[j]=c;return true;
    }
    if(p->count<4){p->contacts[p->count++]=c;return true;}
    PhysicsGeometryContact candidates[5];memcpy(candidates,p->contacts,sizeof(p->contacts));candidates[4]=c;
    unsigned deepest=0;for(unsigned j=1;j<5;j++)if(candidates[j].depth>candidates[deepest].depth)deepest=j;
    unsigned skip=4;double score=-1;
    for(unsigned j=0;j<5;j++)if(j!=deepest){double next=mesh_patch_score(candidates,j,p->normal);if(next>score){score=next;skip=j;}}
    unsigned n=0;for(unsigned j=0;j<5;j++)if(j!=skip)p->contacts[n++]=candidates[j];return true;
}
bool physics_mesh_contacts(const PhysicsGeometry* surface,const PhysicsGeometry* convex,bool flip,
    PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error) {
    const PhysicsMesh* m=surface->mesh;PhysicsGeometry local=mesh_local_geometry(convex,surface->pose);PhysicsVec low,high;mesh_convex_bounds(&local,&low,&high);
    PhysicsVec centre=local.pose.position;if(local.hull)centre=padd(centre,prot(local.pose.rotation,local.hull->centre));
    if(!pfinite(low)||!pfinite(high)||!pfinite(centre))return mesh_fail(error,"mesh contact outside numerical range");
    uint32_t stack[64]={0};unsigned pending=1,patch_count=0;size_t work=0;MeshPatch patches[MESH_PATCH_LIMIT];*count=0;
    while(pending) {
        const MeshNode* node=&m->nodes[stack[--pending]];if(!mesh_box_overlap(node->low,node->high,low,high,m->tolerance))continue;
        if(!node->count){stack[pending++]=node->right;stack[pending++]=node->left;continue;}
        for(uint32_t i=node->start;i<node->start+node->count;i++) {
            if(!mesh_budget(&work,error))return false;uint32_t index=m->order[i].triangle;PhysicsVec p[3];mesh_triangle_points(m,index,p);
            PhysicsVec normal=m->triangles[index].normal;double side=pdot(normal,psub(centre,p[0]));
            if(!surface->desc->double_sided&&side< -m->tolerance)continue;
            if(side<0){PhysicsVec swap=p[1];p[1]=p[2];p[2]=swap;normal=pmul(normal,-1);}
            PhysicsGeometry triangle={.desc=surface->desc,.pose={{0,0,0},{0,0,0,1}},.triangle=p};
            PhysicsGeometryContact found[PHYSICS_MANIFOLD_MAX_POINTS];size_t n=0;
            if(!physics_convex_contacts(&triangle,&local,found,PHYSICS_MANIFOLD_MAX_POINTS,&n,error))return false;
            for(size_t j=0;j<n;j++) {
                PhysicsGeometryContact c=found[j];PhysicsVec feature=physics_triangle_closest(p[0],p[1],p[2],c.point);
                if(!pfinite(feature))return mesh_fail(error,"mesh contact outside numerical range");
                if(mesh_internal_feature(m,index,feature,c.normal))continue;
                // project impulse anchors to the surface before merging triangulated plane patches.
                c.point=feature;double plane=pdot(c.point,c.normal);
                if(!mesh_patch_add(patches,&patch_count,c,plane,m->tolerance*8,error))return false;
            }
        }
    }
    for(unsigned i=0;i<patch_count;i++)for(unsigned j=0;j<patches[i].count;j++) {
        if(*count==capacity)return mesh_fail(error,"mesh contact manifold quota exceeded");PhysicsGeometryContact c=patches[i].contacts[j];
        c.point=padd(surface->pose.position,prot(surface->pose.rotation,c.point));c.normal=pmul(prot(surface->pose.rotation,c.normal),flip?-1:1);contacts[(*count)++]=c;
    }return true;
}
bool physics_mesh_distance(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsHit* hit,const char** error) {
    if(a->mesh&&b->mesh)return mesh_fail(error,"mesh/mesh distance queries are unsupported");
    bool flip=!a->mesh;const PhysicsGeometry* surface=flip?b:a;const PhysicsMesh* m=surface->mesh;
    PhysicsGeometry local=mesh_local_geometry(flip?a:b,surface->pose);PhysicsVec low,high;mesh_convex_bounds(&local,&low,&high);
    if(!pfinite(low)||!pfinite(high))return mesh_fail(error,"mesh distance outside numerical range");
    uint32_t stack[64]={0};unsigned pending=1;size_t work=0;double best=DBL_MAX;PhysicsHit selected={0};
    while(pending) {
        const MeshNode* node=&m->nodes[stack[--pending]];if(mesh_box_distance(node->low,node->high,low,high)>best+m->tolerance)continue;
        if(!node->count) {
            const MeshNode *l=&m->nodes[node->left],*r=&m->nodes[node->right];double dl=mesh_box_distance(l->low,l->high,low,high),dr=mesh_box_distance(r->low,r->high,low,high);
            stack[pending++]=dl<=dr?node->right:node->left;stack[pending++]=dl<=dr?node->left:node->right;continue;
        }
        for(uint32_t i=node->start;i<node->start+node->count;i++) {
            if(!mesh_budget(&work,error))return false;PhysicsVec p[3];mesh_triangle_points(m,m->order[i].triangle,p);
            PhysicsGeometry triangle={.desc=surface->desc,.pose={{0,0,0},{0,0,0,1}},.triangle=p};PhysicsHit next={0};
            if(!physics_geometry_distance(&triangle,&local,&next,error))return false;
            if(next.distance<best){best=next.distance;selected=next;}
            if(best==0){pending=0;break;}
        }
    }
    *hit=selected;hit->triangle_tests=work;
    PhysicsVec pa=padd(surface->pose.position,prot(surface->pose.rotation,selected.point)),pb=padd(surface->pose.position,prot(surface->pose.rotation,selected.point_b));
    hit->point=flip?pb:pa;hit->point_b=flip?pa:pb;hit->normal=pmul(prot(surface->pose.rotation,selected.normal),flip?-1:1);
    return (pfinite(hit->point)&&pfinite(hit->point_b)&&pfinite(hit->normal))||mesh_fail(error,"mesh distance outside numerical range");
}
