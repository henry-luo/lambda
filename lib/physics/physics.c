#include "physics.h"
#include "physics_math.h"
#include "physics_geometry.h"
#include "../mem.h"
#include <float.h>
#include <limits.h>

typedef struct PBody {
    PhysicsBodyDesc d;
    PhysicsVec force,torque,com;
    double mass,inverse_mass,inertia[9],inverse[9],idle;
    bool sleeping,target_set;
    PhysicsPose target;
    size_t island;
    bool island_awake;
    double island_idle;
} PBody;
typedef struct PBox { PhysicsVec lo,hi; } PBox;
typedef struct PCollider { PhysicsColliderDesc d;size_t body;PhysicsPose pose;PBox bounds;PhysicsHull* hull;PhysicsMesh* mesh; } PCollider;
typedef struct PContact {
    size_t a,b;
    PhysicsVec point,normal,friction_impulse;
    double depth,impulse,bounce;
} PContact;
typedef struct PConstraintRow {
    PhysicsVec linear,angular_a,angular_b;
    double impulse,bias,gamma,lower,upper,position_error,position_cap;
    bool position;
    unsigned key;
} PConstraintRow;
#define PJOINT_MAX_ROWS 8
typedef struct PJoint {
    PhysicsJointDesc d;
    size_t a,b;
    PConstraintRow rows[PJOINT_MAX_ROWS];
    unsigned row_count,base_count,base_indices[PJOINT_MAX_ROWS];
    double matrix[PJOINT_MAX_ROWS][PJOINT_MAX_ROWS],factor[PJOINT_MAX_ROWS][PJOINT_MAX_ROWS],base_scale[PJOINT_MAX_ROWS];
    PhysicsVec anchor_a,anchor_b,arm_b,force,torque;
    double coordinate,linear_error,angular_error;
    bool broken;
} PJoint;
typedef struct PPair { size_t a,b;bool sensor; } PPair;
typedef struct PProxy { size_t collider;PBox bounds; } PProxy;
struct PhysicsWorld {
    PhysicsDesc d;
    PBody* bodies;
    PCollider* colliders;
    PhysicsBodyState* states;
    PProxy* proxies;
    PContact *contacts,*previous;
    PPair *pairs,*old_pairs;
    PhysicsEvent* events;
    PJoint* joints;
    PhysicsJointState* joint_states;
    size_t contact_count,previous_count,pair_count,old_pair_count,event_count,candidates;
    uint64_t tick;
    bool faulted;
};
static bool pjoints_geometry_valid(PhysicsWorld* world,PhysicsError* error);

static bool pfail(PhysicsWorld* w,PhysicsError* e,const char* text,uint64_t id) {
    if(e){e->message=text;e->id=id;e->tick=w?w->tick:0;}return false;
}
static size_t pbody(PhysicsWorld* w,uint64_t id) {
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].d.id==id)return i;return SIZE_MAX;
}
static size_t pcollider(PhysicsWorld* w,uint64_t id) {
    for(size_t i=0;i<w->d.collider_count;i++)if(w->colliders[i].d.id==id)return i;return SIZE_MAX;
}
static PhysicsGeometry pgeometry(const PCollider* c) {return (PhysicsGeometry){&c->d,c->hull,c->pose,c->mesh,NULL};}
static bool ppose(PhysicsPose* p) { return pfinite(p->position)&&pqnorm(&p->rotation); }
static PhysicsVec pmat(const double* m,PhysicsVec v) {
    return pv(m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,m[6]*v.x+m[7]*v.y+m[8]*v.z);
}
static PhysicsVec pinverse(PBody* b,PhysicsVec v) {
    return prot(b->d.pose.rotation,pmat(b->inverse,prot(pqconj(b->d.pose.rotation),v)));
}
static PhysicsVec pcentre(PBody* b) {return padd(b->d.pose.position,prot(b->d.pose.rotation,b->com));}
static void ptranslate(PBody* b,PhysicsVec v) {b->d.pose.position=padd(b->d.pose.position,v);}
static void pwake(PBody* b) {b->sleeping=false;b->idle=0;}
static void pbody_impulse(PBody* b,PhysicsVec impulse,PhysicsVec angular) {
    if(!b->inverse_mass)return;
    b->d.velocity=padd(b->d.velocity,pmul(impulse,b->inverse_mass));
    b->d.angular_velocity=padd(b->d.angular_velocity,pinverse(b,angular));
}
static void pimpulse(PBody* b,PhysicsVec impulse,PhysicsVec r) {pbody_impulse(b,impulse,pcross(r,impulse));}
static bool poverlap(PBox a,PBox b) {
    return a.lo.x<=b.hi.x&&a.hi.x>=b.lo.x&&a.lo.y<=b.hi.y&&a.hi.y>=b.lo.y&&a.lo.z<=b.hi.z&&a.hi.z>=b.lo.z;
}
static PBox pbox_expand(PBox bounds,double amount) {
    PhysicsVec padding=pv(amount,amount,amount);return (PBox){psub(bounds.lo,padding),padd(bounds.hi,padding)};
}
static PBox pbox_sweep(PBox bounds,PhysicsVec offset,PhysicsVec delta) {
    return (PBox){padd(padd(bounds.lo,offset),pv(fmin(0,delta.x),fmin(0,delta.y),fmin(0,delta.z))),
        padd(padd(bounds.hi,offset),pv(fmax(0,delta.x),fmax(0,delta.y),fmax(0,delta.z)))};
}
static double pbox_query_padding(PBox a,PBox b) {
    return 1e-8*(plen(psub(a.hi,a.lo))+plen(psub(b.hi,b.lo)))+128*DBL_EPSILON*fmax(plen(a.lo),plen(b.lo));
}
static bool pupdate(PhysicsWorld* w,PhysicsError* e) {
    // commands can overflow before integration; never publish a non-finite velocity or force.
    for(size_t i=0;i<w->d.body_count;i++) {
        PBody* b=&w->bodies[i];
        if(!pfinite(b->d.pose.position)||!pfinite(b->d.velocity)||!pfinite(b->d.angular_velocity)||!pfinite(b->force)||!pfinite(b->torque)) {
            w->faulted=true;return pfail(w,e,"physics numerical overflow",b->d.id);
        }
    }
    for(size_t i=0;i<w->d.collider_count;i++) {
        PCollider* c=&w->colliders[i];c->pose=pcompose(w->bodies[c->body].d.pose,c->d.pose);
        PhysicsVec h=pmul(c->d.size,0.5),extent=pv(c->d.radius,c->d.radius,c->d.radius);
        if(c->d.shape==PHYSICS_BOX) {
            extent=pv(0,0,0);
            for(unsigned j=0;j<3;j++) {PhysicsVec v=prot(c->pose.rotation,pbase(j));double d=paxis(h,j);
                extent=padd(extent,pv(fabs(v.x)*d,fabs(v.y)*d,fabs(v.z)*d));}
        } else if(c->d.shape==PHYSICS_CAPSULE) {
            PhysicsVec axis=prot(c->pose.rotation,pv(0,c->d.height/2,0));
            extent=padd(extent,pv(fabs(axis.x),fabs(axis.y),fabs(axis.z)));
        }
        c->bounds.lo=psub(c->pose.position,extent);c->bounds.hi=padd(c->pose.position,extent);
        if(c->hull)for(unsigned j=0;j<c->hull->vertex_count;j++) {
            PhysicsVec p=padd(c->pose.position,prot(c->pose.rotation,c->hull->vertices[j]));
            if(!j)c->bounds.lo=c->bounds.hi=p;
            else {c->bounds.lo=pv(fmin(c->bounds.lo.x,p.x),fmin(c->bounds.lo.y,p.y),fmin(c->bounds.lo.z,p.z));
                c->bounds.hi=pv(fmax(c->bounds.hi.x,p.x),fmax(c->bounds.hi.y,p.y),fmax(c->bounds.hi.z,p.z));}
        }
        if(c->mesh) {
            PhysicsVec low,high;physics_mesh_bounds(c->mesh,&low,&high);
            for(unsigned j=0;j<8;j++) {
                PhysicsVec p=padd(c->pose.position,prot(c->pose.rotation,pv(j&1?high.x:low.x,j&2?high.y:low.y,j&4?high.z:low.z)));
                if(!j)c->bounds.lo=c->bounds.hi=p;
                else {c->bounds.lo=pv(fmin(c->bounds.lo.x,p.x),fmin(c->bounds.lo.y,p.y),fmin(c->bounds.lo.z,p.z));
                    c->bounds.hi=pv(fmax(c->bounds.hi.x,p.x),fmax(c->bounds.hi.y,p.y),fmax(c->bounds.hi.z,p.z));}
            }
        }
        if(!pfinite(c->pose.position)||!pfinite(c->bounds.lo)||!pfinite(c->bounds.hi)) {
            w->faulted=true;return pfail(w,e,"physics collider bounds overflow",c->d.id);
        }
        w->proxies[i].collider=i;w->proxies[i].bounds=c->bounds;
    }
    if(!pjoints_geometry_valid(w,e)){w->faulted=true;return false;}
    return true;
}
static int pproxy_compare(const void* l,const void* r) {
    const PProxy *a=l,*b=r;return a->bounds.lo.x<b->bounds.lo.x?-1:a->bounds.lo.x>b->bounds.lo.x?1:a->collider<b->collider?-1:a->collider>b->collider;
}
static int ppair_compare(const void* l,const void* r) {
    const PPair *a=l,*b=r;return a->a<b->a?-1:a->a>b->a?1:a->b<b->b?-1:a->b>b->b;
}
static int pcontact_compare(const void* l,const void* r) {
    const PContact *a=l,*b=r;
    if(a->a!=b->a)return a->a<b->a?-1:1;
    if(a->b!=b->b)return a->b<b->b?-1:1;
    for(unsigned i=0;i<3;i++){double x=paxis(a->point,i),y=paxis(b->point,i);if(x!=y)return x<y?-1:1;}
    return 0;
}
static void pshape_mass(const PhysicsColliderDesc* d,double* mass,PhysicsVec* diagonal) {
    if(d->shape==PHYSICS_BOX) {
        *mass=d->density*(d->size.x*d->size.y*d->size.z);
        *diagonal=pmul(pv(d->size.y*d->size.y+d->size.z*d->size.z,
            d->size.x*d->size.x+d->size.z*d->size.z,d->size.x*d->size.x+d->size.y*d->size.y),*mass/12);
        return;
    }
    double radius2=d->radius*d->radius,height=d->shape==PHYSICS_CAPSULE?d->height:0;
    double sphere=d->density*(4.0/3.0*acos(-1.0)*pow(d->radius,3));
    double cylinder=d->density*acos(-1.0)*radius2*height;
    *mass=sphere+cylinder;
    // retain the existing sphere arithmetic when the cylindrical segment vanishes.
    double ball_inertia=0.4*sphere*d->radius*d->radius;
    // hemisphere centres of mass are 3r/8 beyond the cylinder ends.
    double transverse=cylinder*(3*radius2+height*height)/12+ball_inertia+
        sphere*(3*height*d->radius/8+height*height/4);
    *diagonal=pv(transverse,ball_inertia+0.5*cylinder*radius2,transverse);
}
static void pcollider_properties(PCollider* c,double* mass,PhysicsVec* centre,double inertia[9]) {
    memset(inertia,0,9*sizeof(double));PhysicsVec local=pv(0,0,0);
    if(c->mesh)*mass=0;
    else if(c->hull) {
        *mass=c->hull->volume*c->d.density;local=c->hull->centre;
        for(unsigned i=0;i<9;i++)inertia[i]=c->hull->inertia[i]*c->d.density;
    } else {
        PhysicsVec diagonal;pshape_mass(&c->d,mass,&diagonal);
        inertia[0]=diagonal.x;inertia[4]=diagonal.y;inertia[8]=diagonal.z;
    }
    *centre=padd(c->d.pose.position,prot(c->d.pose.rotation,local));
}
static bool pmass(PhysicsWorld* w,size_t index,PhysicsError* e) {
    PBody* b=&w->bodies[index];PhysicsVec first=pv(0,0,0);
    for(size_t i=0;i<w->d.collider_count;i++) {
        PCollider* c=&w->colliders[i];if(c->body!=index)continue;
        double mass,inertia[9];PhysicsVec centre;pcollider_properties(c,&mass,&centre,inertia);
        b->mass+=mass;first=padd(first,pmul(centre,mass));
    }
    if(b->mass>0)b->com=pmul(first,1/b->mass);
    if(!isfinite(b->mass)||!pfinite(b->com))return pfail(w,e,"collider mass outside numerical range",b->d.id);
    if(b->d.motion!=PHYSICS_DYNAMIC)return true;
    if(!isfinite(b->mass)||b->mass<=0)return pfail(w,e,"dynamic body requires positive finite collider mass",b->d.id);
    b->inverse_mass=1/b->mass;
    if(!isfinite(b->inverse_mass))return pfail(w,e,"inverse mass outside numerical range",b->d.id);
    for(size_t i=0;i<w->d.collider_count;i++) {
        PCollider* c=&w->colliders[i];if(c->body!=index||c->d.density==0)continue;
        double mass,inertia[9];PhysicsVec centre;pcollider_properties(c,&mass,&centre,inertia);
        PhysicsVec offset=psub(centre,b->com);
        for(unsigned j=0;j<3;j++) {
            PhysicsVec axis=prot(c->d.pose.rotation,pbase(j));
            for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)
                b->inertia[row*3+col]+=inertia[j*3+j]*paxis(axis,row)*paxis(axis,col);
        }
        if(c->hull)for(unsigned j=0;j<3;j++)for(unsigned k=0;k<3;k++)if(j!=k) {
            PhysicsVec u=prot(c->d.pose.rotation,pbase(j)),v=prot(c->d.pose.rotation,pbase(k));
            for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)b->inertia[row*3+col]+=inertia[j*3+k]*paxis(u,row)*paxis(v,col);
        }
        for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)
            b->inertia[row*3+col]+=mass*((row==col?pdot(offset,offset):0)-paxis(offset,row)*paxis(offset,col));
    }
    const double* m=b->inertia;
    double det=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    if(!isfinite(det)||det<=0)return pfail(w,e,"invalid dynamic inertia",b->d.id);
    double inv[9]={m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
        m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
        m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]};
    for(unsigned i=0;i<9;i++){b->inverse[i]=inv[i]/det;if(!isfinite(b->inverse[i]))return pfail(w,e,"inertia outside numerical range",b->d.id);}
    return true;
}

// descriptor checks shared by creation and reconfiguration; they normalize rotations in place.
static bool pbody_valid(PhysicsBodyDesc* d) {
    return d->id&&(unsigned)d->motion<=PHYSICS_KINEMATIC_VELOCITY&&ppose(&d->pose)&&pfinite(d->velocity)&&pfinite(d->angular_velocity)&&
        !(d->motion==PHYSICS_FIXED&&(plen(d->velocity)>0||plen(d->angular_velocity)>0))&&
        isfinite(d->linear_damping)&&d->linear_damping>=0&&isfinite(d->angular_damping)&&d->angular_damping>=0&&isfinite(d->gravity_scale);
}
static bool pcollider_valid(PhysicsColliderDesc* d) {
    bool environment=d->shape==PHYSICS_MESH||d->shape==PHYSICS_HEIGHTFIELD;
    bool geometry=environment?d->density==0:d->shape==PHYSICS_BOX?
        (pfinite(d->size)&&d->size.x>0&&d->size.y>0&&d->size.z>0):d->shape==PHYSICS_CONVEX?
        (d->vertices&&d->vertex_count>=4&&d->vertex_count<=PHYSICS_CONVEX_MAX_VERTICES):(isfinite(d->radius)&&d->radius>0);
    return d->id&&(unsigned)d->shape<=PHYSICS_HEIGHTFIELD&&ppose(&d->pose)&&geometry&&
        (d->shape!=PHYSICS_CAPSULE||(isfinite(d->height)&&d->height>=0))&&
        isfinite(d->density)&&d->density>=0&&isfinite(d->friction)&&d->friction>=0&&isfinite(d->restitution)&&d->restitution>=0&&d->restitution<=1;
}
bool physics_collider_equal(const PhysicsColliderDesc* x,const PhysicsColliderDesc* y) {
    if(x->id!=y->id||x->body!=y->body||x->shape!=y->shape||memcmp(&x->pose,&y->pose,sizeof(PhysicsPose))||memcmp(&x->size,&y->size,sizeof(PhysicsVec))||
        x->radius!=y->radius||x->height!=y->height||x->density!=y->density||x->friction!=y->friction||x->restitution!=y->restitution||
        x->membership!=y->membership||x->filter!=y->filter||x->sensor!=y->sensor||x->vertex_count!=y->vertex_count||x->index_count!=y->index_count||
        x->rows!=y->rows||x->columns!=y->columns||x->cell_width!=y->cell_width||x->cell_depth!=y->cell_depth||x->double_sided!=y->double_sided)return false;
    if(x->vertex_count&&memcmp(x->vertices,y->vertices,x->vertex_count*sizeof(PhysicsVec)))return false;
    if(x->index_count&&memcmp(x->indices,y->indices,x->index_count*sizeof(uint32_t)))return false;
    return !x->rows||!memcmp(x->heights,y->heights,x->rows*x->columns*sizeof(double));
}
static void pcolliders_destroy(PCollider* colliders,size_t count) {
    if(!colliders)return;for(size_t i=0;i<count;i++){physics_hull_destroy(colliders[i].hull);physics_mesh_destroy(colliders[i].mesh);}mem_free(colliders);
}
static PCollider* pcolliders_build(PhysicsWorld* w,const PhysicsColliderDesc* descriptions,size_t count,const PhysicsBodyDesc* bodies,PhysicsError* e) {
    PCollider* result=mem_calloc(count+1,sizeof(PCollider),MEM_CAT_EVAL);size_t vertices=0,triangles=0;
    if(!result){pfail(w,e,"physics allocation failed",0);return NULL;}
    for(size_t i=0;i<count;i++) {
        PCollider* c=&result[i];c->d=descriptions[i];c->body=pbody(w,c->d.body);
        bool duplicate=false;for(size_t j=0;j<i;j++)duplicate|=result[j].d.id==c->d.id;
        const char* error=NULL;
        if(duplicate||c->body==SIZE_MAX||!pcollider_valid(&c->d))error="invalid or duplicate physics collider";
        else if(c->d.shape==PHYSICS_CONVEX) {
            vertices+=c->d.vertex_count;
            if(vertices>PHYSICS_WORLD_MAX_VERTICES)error="physics world convex vertex quota exceeded";
            else if(physics_hull_create(c->d.vertices,c->d.vertex_count,&c->hull,&error)) {
                c->d.vertices=c->hull->vertices;c->d.vertex_count=c->hull->vertex_count;
            }
        } else if(c->d.shape==PHYSICS_MESH||c->d.shape==PHYSICS_HEIGHTFIELD) {
            size_t points=0,faces=0;
            PhysicsMotion motion=bodies?bodies[c->body].motion:w->bodies[c->body].d.motion;
            if(motion==PHYSICS_DYNAMIC)error="mesh and heightfield colliders require a fixed or kinematic body";
            else if(physics_mesh_input_counts(&c->d,&points,&faces,&error)) {
                // charge source storage before welding: duplicated vertices still allocate owned buffers.
                vertices+=points;triangles+=faces;
                if(vertices>PHYSICS_WORLD_MAX_VERTICES||triangles>PHYSICS_WORLD_MAX_TRIANGLES)error="physics world geometry quota exceeded";
                else if(physics_mesh_create(&c->d,&c->mesh,&error))physics_mesh_bind(c->mesh,&c->d);
            }
        } else {c->d.vertices=NULL;c->d.vertex_count=0;}
        if(!c->mesh){c->d.indices=NULL;c->d.index_count=0;c->d.heights=NULL;}
        if(error){pfail(w,e,error,c->d.id);pcolliders_destroy(result,count);return NULL;}
    }
    return result;
}

// a single sphere remains geometrically invariant under spin, including an offset body origin.
static bool pbody_sphere(PhysicsWorld* w,size_t body) {
    size_t count=0;
    for(size_t i=0;i<w->d.collider_count;i++)if(w->colliders[i].body==body) {
        if(w->colliders[i].d.shape!=PHYSICS_SPHERE||w->colliders[i].d.sensor)return false;count++;
    }
    return count==1;
}
static bool pccd_valid(PhysicsWorld* w,PhysicsError* e) {
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].d.ccd&&
        (w->bodies[i].d.motion!=PHYSICS_DYNAMIC||!pbody_sphere(w,i)))
        return pfail(w,e,"CCD requires a dynamic body with one solid sphere collider",w->bodies[i].d.id);
    for(size_t i=0;i<w->d.joint_count&&w->joints;i++)if(!w->joints[i].d.disabled&&!w->joints[i].broken&&
        (w->bodies[w->joints[i].a].d.ccd||w->bodies[w->joints[i].b].d.ccd))return pfail(w,e,"joint CCD coupling is not supported yet",w->joints[i].d.id);
    return true;
}

static bool pjoint_active(const PJoint* j) {return !j->d.disabled&&!j->broken;}
bool physics_joint_equal(const PhysicsJointDesc* a,const PhysicsJointDesc* b) {
    return a->id==b->id&&a->body_a==b->body_a&&a->body_b==b->body_b&&a->kind==b->kind&&
        !memcmp(&a->frame_a,&b->frame_a,sizeof(PhysicsPose))&&!memcmp(&a->frame_b,&b->frame_b,sizeof(PhysicsPose))&&
        a->length==b->length&&a->stiffness==b->stiffness&&a->damping==b->damping&&a->lower_limit==b->lower_limit&&
        a->upper_limit==b->upper_limit&&a->cone_angle==b->cone_angle&&a->motor_speed==b->motor_speed&&
        a->motor_max_force==b->motor_max_force&&a->break_force==b->break_force&&a->break_torque==b->break_torque&&
        a->disabled==b->disabled&&a->collide_connected==b->collide_connected&&a->spring==b->spring&&
        a->limit_enabled==b->limit_enabled&&a->motor_enabled==b->motor_enabled;
}
static bool pjoint_attached(PhysicsWorld* w,size_t body) {
    for(size_t i=0;i<w->d.joint_count;i++)if(pjoint_active(&w->joints[i])&&(w->joints[i].a==body||w->joints[i].b==body))return true;
    return false;
}
static bool pjoint_collides(PhysicsWorld* w,size_t a,size_t b) {
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];if(pjoint_active(j)&&!j->d.collide_connected&&((j->a==a&&j->b==b)||(j->a==b&&j->b==a)))return false;
    }
    return true;
}
static void pjoint_invalidate_body(PhysicsWorld* w,size_t body) {
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];if(j->a!=body&&j->b!=body)continue;
        j->row_count=0;
        if(pjoint_active(j)){pwake(&w->bodies[j->a]);pwake(&w->bodies[j->b]);}
    }
}
static PJoint* pjoints_build(PhysicsWorld* w,const PhysicsJointDesc* descriptions,size_t count,PhysicsError* e) {
    if(count>PHYSICS_WORLD_MAX_JOINTS||(count&&!descriptions)){pfail(w,e,"invalid joint count or description",0);return NULL;}
    PJoint* out=mem_calloc(count+1,sizeof(PJoint),MEM_CAT_EVAL);if(!out){pfail(w,e,"physics joint allocation failed",0);return NULL;}
    for(size_t i=0;i<count;i++) {
        PJoint* j=&out[i];j->d=descriptions[i];PhysicsJointDesc* d=&j->d;
        j->a=pbody(w,d->body_a);j->b=pbody(w,d->body_b);bool duplicate=false;
        for(size_t k=0;k<i;k++)duplicate|=out[k].d.id==d->id;
        const char* error=NULL;
        if(!d->id||duplicate||(unsigned)d->kind>PHYSICS_JOINT_CONE_TWIST||j->a==SIZE_MAX||j->b==SIZE_MAX||j->a==j->b||
            !ppose(&d->frame_a)||!ppose(&d->frame_b))error="invalid joint identity, bodies or local frames";
        double values[]={d->length,d->stiffness,d->damping,d->lower_limit,d->upper_limit,d->cone_angle,d->motor_speed,
            d->motor_max_force,d->break_force,d->break_torque};
        for(unsigned k=0;k<sizeof(values)/sizeof(values[0]);k++)if(!isfinite(values[k]))error="joint parameter outside numerical range";
        if(d->length<0||d->stiffness<0||d->damping<0||d->motor_max_force<0||d->break_force<0||d->break_torque<0)
            error="joint parameters require nonnegative lengths, coefficients and thresholds";
        bool axial=d->kind==PHYSICS_JOINT_DISTANCE||d->kind==PHYSICS_JOINT_HINGE||d->kind==PHYSICS_JOINT_PRISMATIC||d->kind==PHYSICS_JOINT_CONE_TWIST;
        if((d->limit_enabled&&(!axial||d->lower_limit>d->upper_limit))||(d->motor_enabled&&!axial)||
            (d->spring&&(d->kind!=PHYSICS_JOINT_DISTANCE||d->stiffness<=0)))error="unsupported or inconsistent joint limits, motor or spring";
        if(d->kind==PHYSICS_JOINT_DISTANCE&&(d->length<=1e-9*w->d.length_scale||
            (d->limit_enabled&&d->lower_limit<=1e-9*w->d.length_scale)||
            (d->motor_enabled&&!d->spring&&!d->limit_enabled)))error="distance joint requires positive length and a compliant/range motor mode";
        if((d->kind==PHYSICS_JOINT_HINGE||d->kind==PHYSICS_JOINT_CONE_TWIST)&&d->limit_enabled&&
            (d->lower_limit< -acos(-1.0)||d->upper_limit>acos(-1.0)))error="joint twist limits must lie within [-pi,pi]";
        if(d->kind==PHYSICS_JOINT_CONE_TWIST&&(d->cone_angle<=0||d->cone_angle>=acos(-1.0)))error="cone joint requires an angle between zero and pi";
        if(!error)for(size_t k=0;k<w->d.joint_count&&w->joints;k++) {
            const PJoint* old=&w->joints[k];PhysicsJointDesc definition=*d;definition.disabled=old->d.disabled;
            if(!physics_joint_equal(&definition,&old->d))continue;
            // playback toggles do not repair a fracture; changed constraint definitions do.
            bool disabled=d->disabled;bool toggled=disabled!=old->d.disabled;*j=*old;j->d.disabled=disabled;
            if(toggled)j->row_count=0;
            break;
        }
        if(!error) {
            PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];
            if(pjoint_active(j)&&!a->inverse_mass&&!b->inverse_mass)error="active joint requires at least one dynamic body";
            if(pjoint_active(j)&&(a->d.ccd||b->d.ccd))error="joint CCD coupling is not supported yet";
        }
        if(error){pfail(w,e,error,d->id);mem_free(out);return NULL;}
    }
    return out;
}
static bool pjoints_prepare(PhysicsWorld* w,PhysicsError* e);
static bool pjoints_finish(PhysicsWorld* w,PhysicsError* e);

bool physics_create(const PhysicsDesc* d,PhysicsWorld** result,PhysicsError* e) {
    if(result)*result=NULL;
    if(!d||!result||!pfinite(d->gravity)||!isfinite(d->timestep)||d->timestep<=0||!isfinite(d->length_scale)||d->length_scale<=0||
        !d->iterations||d->iterations>256||!d->contact_limit||d->contact_limit>1048576||!d->event_limit||d->event_limit>1048576||
        d->body_count>65536||d->collider_count>65536||d->joint_count>PHYSICS_WORLD_MAX_JOINTS||(d->joint_count&&!d->joints)||(d->body_count&&!d->bodies)||(d->collider_count&&!d->colliders))
        return pfail(NULL,e,"invalid physics world configuration",0);
    PhysicsWorld* w=mem_calloc(1,sizeof(*w),MEM_CAT_EVAL);if(!w)return pfail(NULL,e,"physics allocation failed",0);
    w->d=*d;w->d.bodies=NULL;w->d.colliders=NULL;w->d.joints=NULL;
    w->bodies=mem_calloc(d->body_count+1,sizeof(PBody),MEM_CAT_EVAL);
    w->states=mem_calloc(d->body_count+1,sizeof(PhysicsBodyState),MEM_CAT_EVAL);
    w->proxies=mem_calloc(d->collider_count+1,sizeof(PProxy),MEM_CAT_EVAL);
    w->contacts=mem_calloc(d->contact_limit,sizeof(PContact),MEM_CAT_EVAL);
    w->previous=mem_calloc(d->contact_limit,sizeof(PContact),MEM_CAT_EVAL);
    w->pairs=mem_calloc(d->contact_limit,sizeof(PPair),MEM_CAT_EVAL);
    w->old_pairs=mem_calloc(d->contact_limit,sizeof(PPair),MEM_CAT_EVAL);
    w->events=mem_calloc(d->event_limit,sizeof(PhysicsEvent),MEM_CAT_EVAL);
    w->joint_states=mem_calloc(d->joint_count+1,sizeof(PhysicsJointState),MEM_CAT_EVAL);
    if(!w->bodies||!w->states||!w->proxies||!w->contacts||!w->previous||!w->pairs||!w->old_pairs||!w->events||!w->joint_states) {
        pfail(w,e,"physics allocation failed",0);physics_destroy(w);return false;
    }
    for(size_t i=0;i<d->body_count;i++) {
        PBody* b=&w->bodies[i];b->d=d->bodies[i];
        if(!pbody_valid(&b->d)||pbody(w,b->d.id)!=i) {
            pfail(w,e,"invalid or duplicate physics body",b->d.id);physics_destroy(w);return false;
        }
    }
    w->colliders=pcolliders_build(w,d->colliders,d->collider_count,NULL,e);
    if(!w->colliders){physics_destroy(w);return false;}
    for(size_t i=0;i<d->body_count;i++)if(!pmass(w,i,e)){physics_destroy(w);return false;}
    w->joints=pjoints_build(w,d->joints,d->joint_count,e);
    if(!w->joints||!pccd_valid(w,e)||!pupdate(w,e)||!pjoints_prepare(w,e)){physics_destroy(w);return false;}
    PhysicsState initial;if(!physics_read(w,&initial,e)){physics_destroy(w);return false;}*result=w;return true;
}
void physics_destroy(PhysicsWorld* w) {
    if(!w)return;
    mem_free(w->bodies);pcolliders_destroy(w->colliders,w->d.collider_count);mem_free(w->states);mem_free(w->proxies);mem_free(w->contacts);
    mem_free(w->previous);mem_free(w->pairs);mem_free(w->old_pairs);mem_free(w->events);mem_free(w->joints);mem_free(w->joint_states);mem_free(w);
}

typedef struct PContactWriter {PContact* contacts;size_t* count;size_t capacity;size_t* triangle_tests;} PContactWriter;
static bool pcontact(PhysicsWorld* w,PContactWriter* output,size_t a,size_t b,PhysicsVec point,PhysicsVec normal,double depth,PhysicsError* e) {
    if(*output->count==output->capacity)return pfail(w,e,"physics contact quota exceeded",0);
    PContact c={0};c.a=a;c.b=b;c.point=point;c.normal=normal;c.depth=depth;
    output->contacts[(*output->count)++]=c;return true;
}
static PhysicsVec psupport(PCollider* c,PhysicsVec axis) {
    PhysicsGeometry geometry=pgeometry(c);return physics_geometry_support(&geometry,axis);
}
static double pradius(PCollider* c,PhysicsVec axis) {
    if(c->d.shape!=PHYSICS_BOX) return c->d.radius+(c->d.shape==PHYSICS_CAPSULE?
        fabs(pdot(prot(c->pose.rotation,pv(0,c->d.height/2,0)),axis)):0);
    PhysicsVec l=prot(pqconj(c->pose.rotation),axis),h=pmul(c->d.size,0.5);return fabs(l.x)*h.x+fabs(l.y)*h.y+fabs(l.z)*h.z;
}
static bool pbox_contacts(PhysicsWorld* w,PContactWriter* output,size_t a,size_t b,PhysicsError* e) {
    PCollider *ca=&w->colliders[a],*cb=&w->colliders[b];PhysicsVec delta=psub(cb->pose.position,ca->pose.position),normal=pv(0,1,0);
    double depth=DBL_MAX;unsigned best=0;
    for(unsigned i=0;i<15;i++) {
        PhysicsVec axis=i<3?prot(ca->pose.rotation,pbase(i)):i<6?prot(cb->pose.rotation,pbase(i-3)):
            pcross(prot(ca->pose.rotation,pbase((i-6)/3)),prot(cb->pose.rotation,pbase((i-6)%3)));
        double length=plen(axis);if(length<1e-10)continue;axis=pmul(axis,1/length);
        double distance=pdot(delta,axis),overlap=pradius(ca,axis)+pradius(cb,axis)-fabs(distance);
        if(overlap<0)return true;
        if(overlap<depth){depth=overlap;normal=pmul(axis,distance<0?-1:1);best=i;}
    }
    if(best>=6) {
        // edge contacts use the closest points on the selected finite edges.
        unsigned ia=(best-6)/3,ib=(best-6)%3;
        PhysicsVec ea=prot(ca->pose.rotation,pbase(ia)),eb=prot(cb->pose.rotation,pbase(ib));
        PhysicsVec pa=psupport(ca,normal),pb=psupport(cb,pmul(normal,-1));
        pa=psub(pa,pmul(ea,pdot(psub(pa,ca->pose.position),ea)));
        pb=psub(pb,pmul(eb,pdot(psub(pb,cb->pose.position),eb)));
        double dot=pdot(ea,eb),den=1-dot*dot,ha=paxis(ca->d.size,ia)/2,hb=paxis(cb->d.size,ib)/2;
        PhysicsVec r=psub(pa,pb);double t=den>1e-12?(dot*pdot(r,eb)-pdot(r,ea))/den:0;
        t=fmax(-ha,fmin(ha,t));double u=fmax(-hb,fmin(hb,pdot(r,eb)+dot*t));
        t=fmax(-ha,fmin(ha,dot*u-pdot(r,ea)));
        return pcontact(w,output,a,b,pmul(padd(padd(pa,pmul(ea,t)),padd(pb,pmul(eb,u))),0.5),normal,depth,e);
    }
    PCollider *ref=best<3?ca:cb,*inc=best<3?cb:ca;PhysicsVec n=best<3?normal:pmul(normal,-1);
    unsigned face=best%3,incident=0;double largest=0;
    for(unsigned i=0;i<3;i++){double d=fabs(pdot(prot(inc->pose.rotation,pbase(i)),n));if(d>largest){largest=d;incident=i;}}
    PhysicsVec in=prot(inc->pose.rotation,pbase(incident));double sign=pdot(in,n)>0?-1:1;
    PhysicsVec centre=padd(inc->pose.position,pmul(in,sign*paxis(inc->d.size,incident)/2));
    PhysicsVec u=pmul(prot(inc->pose.rotation,pbase((incident+1)%3)),paxis(inc->d.size,(incident+1)%3)/2);
    PhysicsVec v=pmul(prot(inc->pose.rotation,pbase((incident+2)%3)),paxis(inc->d.size,(incident+2)%3)/2);
    PhysicsVec poly[16],scratch[16];poly[0]=padd(centre,padd(u,v));poly[1]=padd(centre,psub(v,u));
    poly[2]=psub(centre,padd(u,v));poly[3]=padd(centre,psub(u,v));unsigned count=4;
    for(unsigned i=0;i<3;i++)if(i!=face)for(unsigned side=0;side<2;side++) {
        PhysicsVec axis=pmul(prot(ref->pose.rotation,pbase(i)),side?1:-1);
        count=pclip(poly,count,scratch,16,axis,pdot(ref->pose.position,axis)+paxis(ref->d.size,i)/2);
        if(count==UINT_MAX)return pfail(w,e,"physics box clipping quota exceeded",0);
        memcpy(poly,scratch,count*sizeof(PhysicsVec));
    }
    double plane=pdot(ref->pose.position,n)+paxis(ref->d.size,face)/2;
    for(unsigned i=0;i<count;i++){double penetration=plane-pdot(poly[i],n);
        if(penetration>=0&&!pcontact(w,output,a,b,padd(poly[i],pmul(n,penetration/2)),normal,penetration,e))return false;}
    return true;
}
static PhysicsVec pbox_closest(PhysicsVec point,PhysicsVec half) {
    return pv(pclamp(point.x,-half.x,half.x),pclamp(point.y,-half.y,half.y),pclamp(point.z,-half.z,half.z));
}
static void pround_segment(PCollider* c,PhysicsVec* start,PhysicsVec* end) {
    PhysicsGeometry geometry=pgeometry(c);physics_round_segment(&geometry,start,end);
}
static void psegment_box(PhysicsVec start,PhysicsVec end,PhysicsVec half,PhysicsVec* segment,PhysicsVec* box) {
    PhysicsVec d=psub(end,start);double cuts[8]={0,1};unsigned count=2;
    // squared distance to an AABB is quadratic between the six face crossings.
    for(unsigned axis=0;axis<3;axis++)if(paxis(d,axis)!=0)for(unsigned side=0;side<2;side++) {
        double t=((side?1:-1)*paxis(half,axis)-paxis(start,axis))/paxis(d,axis);
        if(t>0&&t<1)cuts[count++]=t;
    }
    for(unsigned i=1;i<count;i++){double t=cuts[i];unsigned j=i;while(j&&cuts[j-1]>t){cuts[j]=cuts[j-1];j--;}cuts[j]=t;}
    double best=DBL_MAX,selected=0;
    for(unsigned i=1;i<count;i++) {
        double mid=(cuts[i-1]+cuts[i])/2,aa=0,bb=0;
        for(unsigned axis=0;axis<3;axis++) {
            double p=paxis(start,axis),v=paxis(d,axis),h=paxis(half,axis),at=p+v*mid;
            if(at< -h||at>h){aa+=v*v;bb+=v*(p-(at<0?-h:h));}
        }
        double t=aa>0?pclamp(-bb/aa,cuts[i-1],cuts[i]):mid;
        PhysicsVec point=padd(start,pmul(d,t)),closest=pbox_closest(point,half),delta=psub(point,closest);
        double distance=pdot(delta,delta);
        if(distance<best||(distance==best&&fabs(t-.5)<fabs(selected-.5))) {
            best=distance;selected=t;*segment=point;*box=closest;
        }
    }
}
static bool pround_box_contacts(PhysicsWorld* w,PContactWriter* output,size_t a,size_t b,PhysicsError* e) {
    bool flip=w->colliders[a].d.shape==PHYSICS_BOX;
    PCollider *box=&w->colliders[flip?a:b],*round=&w->colliders[flip?b:a];
    PhysicsQuat inverse=pqconj(box->pose.rotation);PhysicsVec start,end;
    pround_segment(round,&start,&end);
    start=prot(inverse,psub(start,box->pose.position));end=prot(inverse,psub(end,box->pose.position));
    PhysicsVec h=pmul(box->d.size,.5),segment,closest;
    psegment_box(start,end,h,&segment,&closest);
    PhysicsVec diff=psub(segment,closest),normal;double length=plen(diff),depth=round->d.radius-length;
    if(length>0){if(depth<0)return true;normal=pmul(diff,1/length);}
    else {
        // the segment intersects the box: its Minkowski sum has box-face and segment/edge normals.
        PhysicsVec centre=pmul(padd(start,end),.5),half=pmul(psub(end,start),.5);depth=DBL_MAX;normal=pv(1,0,0);
        for(unsigned i=0;i<6;i++) {
            PhysicsVec axis=i<3?pbase(i):pcross(half,pbase(i-3));double norm=plen(axis);if(norm==0)continue;axis=pmul(axis,1/norm);
            double projected=pdot(centre,axis),extent=fabs(axis.x)*h.x+fabs(axis.y)*h.y+fabs(axis.z)*h.z;
            double overlap=extent+fabs(pdot(half,axis))+round->d.radius-fabs(projected);
            if(overlap<depth){depth=overlap;normal=pmul(axis,projected<0?-1:1);}
        }
        segment=pdot(psub(end,start),normal)>0?start:end;
        if(fabs(pdot(psub(end,start),normal))<1e-12)segment=psegment_closest(start,end,pv(0,0,0));
        closest=padd(segment,pmul(normal,depth-round->d.radius));
    }
    unsigned contacts=0;
    if(round->d.shape==PHYSICS_CAPSULE&&round->d.height>0&&length>0)for(unsigned i=0;i<2;i++) {
        PhysicsVec tip=i?end:start,point=pbox_closest(tip,h),delta=psub(tip,point);double distance=plen(delta);
        if(distance>0&&distance<=round->d.radius&&pdot(pmul(delta,1/distance),normal)>1-1e-10) {
            PhysicsVec world=padd(box->pose.position,prot(box->pose.rotation,point));
            if(!pcontact(w,output,a,b,world,pmul(prot(box->pose.rotation,normal),flip?1:-1),round->d.radius-distance,e))return false;
            contacts++;
        }
    }
    if(contacts)return true;
    return pcontact(w,output,a,b,padd(box->pose.position,prot(box->pose.rotation,closest)),
        pmul(prot(box->pose.rotation,normal),flip?1:-1),depth,e);
}
static bool pcollide(PhysicsWorld* w,PContactWriter* output,size_t a,size_t b,PhysicsError* e) {
    PCollider *ca=&w->colliders[a],*cb=&w->colliders[b];
    if(ca->hull||cb->hull||ca->mesh||cb->mesh) {
        PhysicsGeometry ga=pgeometry(ca),gb=pgeometry(cb);PhysicsGeometryContact contacts[PHYSICS_MANIFOLD_MAX_POINTS];size_t count=0;const char* error=NULL;
        if(!physics_convex_contacts(&ga,&gb,contacts,PHYSICS_MANIFOLD_MAX_POINTS,&count,output->triangle_tests,&error))return pfail(w,e,error,0);
        for(size_t i=0;i<count;i++)if(!pcontact(w,output,a,b,contacts[i].point,contacts[i].normal,contacts[i].depth,e))return false;
        return true;
    }
    if(ca->d.shape==PHYSICS_BOX&&cb->d.shape==PHYSICS_BOX)return pbox_contacts(w,output,a,b,e);
    if(ca->d.shape==PHYSICS_BOX||cb->d.shape==PHYSICS_BOX)return pround_box_contacts(w,output,a,b,e);
    PhysicsVec start,end,other_start,other_end,pa,pb;
    pround_segment(ca,&start,&end);pround_segment(cb,&other_start,&other_end);
    psegments_closest(start,end,other_start,other_end,&pa,&pb);
    PhysicsVec delta=psub(pb,pa);double length=plen(delta),depth=ca->d.radius+cb->d.radius-length;
    if(depth<0)return true;
    PhysicsVec normal=length>0?pmul(delta,1/length):punit(pcross(psub(end,start),psub(other_end,other_start)));
    if(plen(normal)==0) {
        PhysicsVec axis=psub(end,start);if(plen(axis)==0)axis=psub(other_end,other_start);
        unsigned base=0;for(unsigned i=1;i<3;i++)if(fabs(paxis(axis,i))<fabs(paxis(axis,base)))base=i;
        normal=plen(axis)>0?punit(pcross(axis,pbase(base))):pv(1,0,0);
    }
    return pcontact(w,output,a,b,padd(pa,pmul(normal,ca->d.radius-depth/2)),normal,depth,e);
}
static bool pevent(PhysicsWorld* w,PPair pair,bool started,PhysicsError* e) {
    if(w->event_count==w->d.event_limit)return pfail(w,e,"physics event quota exceeded",0);
    PCollider *a=&w->colliders[pair.a],*b=&w->colliders[pair.b];
    PhysicsEvent event={w->tick+1,a->d.id,b->d.id,a->d.body,b->d.body,started,pair.sensor};w->events[w->event_count++]=event;return true;
}
static bool pdetect(PhysicsWorld* w,PhysicsError* e) {
    if(!pupdate(w,e))return false;
    qsort(w->proxies,w->d.collider_count,sizeof(PProxy),pproxy_compare);
    w->contact_count=0;w->pair_count=0;w->candidates=0;
    PContactWriter output={w->contacts,&w->contact_count,w->d.contact_limit,NULL};
    for(size_t i=0;i<w->d.collider_count;i++)for(size_t j=i+1;j<w->d.collider_count&&w->proxies[j].bounds.lo.x<=w->proxies[i].bounds.hi.x;j++) {
        size_t a=w->proxies[i].collider,b=w->proxies[j].collider;if(a>b){size_t t=a;a=b;b=t;}
        PCollider *ca=&w->colliders[a],*cb=&w->colliders[b];
        if(ca->body==cb->body||!(ca->d.membership&cb->d.filter)||!(cb->d.membership&ca->d.filter)||!poverlap(ca->bounds,cb->bounds)||!pjoint_collides(w,ca->body,cb->body))continue;
        if(w->bodies[ca->body].d.motion==PHYSICS_FIXED&&w->bodies[cb->body].d.motion==PHYSICS_FIXED&&!ca->d.sensor&&!cb->d.sensor)continue;
        w->candidates++;size_t before=w->contact_count;
        if(!pcollide(w,&output,a,b,e))return false;
        if(w->contact_count>before) {
            if(w->pair_count==w->d.contact_limit)return pfail(w,e,"physics pair quota exceeded",0);
            PPair pair={a,b,ca->d.sensor||cb->d.sensor};w->pairs[w->pair_count++]=pair;
            if(pair.sensor)w->contact_count=before;
        }
    }
    qsort(w->contacts,w->contact_count,sizeof(PContact),pcontact_compare);
    return true;
}
static bool ppublish_pairs(PhysicsWorld* w,PhysicsError* e) {
    qsort(w->pairs,w->pair_count,sizeof(PPair),ppair_compare);
    size_t i=0,j=0;
    while(i<w->pair_count||j<w->old_pair_count) {
        int order=i==w->pair_count?1:j==w->old_pair_count?-1:ppair_compare(&w->pairs[i],&w->old_pairs[j]);
        if(order<0){if(!pevent(w,w->pairs[i++],true,e))return false;}
        else if(order>0){if(!pevent(w,w->old_pairs[j++],false,e))return false;}else{i++;j++;}
    }
    return true;
}
static PhysicsVec pvelocity(PBody* b,PhysicsVec r) {return padd(b->d.velocity,pcross(b->d.angular_velocity,r));}
static PConstraintRow prow_linear(PhysicsVec axis,PhysicsVec ra,PhysicsVec rb) {
    return (PConstraintRow){.linear=axis,.angular_a=pmul(pcross(ra,axis),-1),.angular_b=pcross(rb,axis)};
}
static double prow_cross(PBody* a,PBody* b,const PConstraintRow* x,const PConstraintRow* y) {
    return (a->inverse_mass+b->inverse_mass)*pdot(x->linear,y->linear)+
        pdot(x->angular_a,pinverse(a,y->angular_a))+pdot(x->angular_b,pinverse(b,y->angular_b));
}
static double prow_effective(PBody* a,PBody* b,const PConstraintRow* row) {return prow_cross(a,b,row,row);}
static bool prow_awake(PBody* a,PBody* b) {return (a->inverse_mass&&!a->sleeping)||(b->inverse_mass&&!b->sleeping);}
static void prow_apply(PBody* a,PBody* b,const PConstraintRow* row,double impulse) {
    pbody_impulse(a,pmul(row->linear,-impulse),pmul(row->angular_a,impulse));
    pbody_impulse(b,pmul(row->linear,impulse),pmul(row->angular_b,impulse));
}
static void prow_solve(PBody* a,PBody* b,PConstraintRow* row) {
    if(!prow_awake(a,b))return;
    double effective=prow_effective(a,b,row)+row->gamma;if(effective<=0)return;
    double speed=pdot(row->linear,psub(b->d.velocity,a->d.velocity))+pdot(row->angular_a,a->d.angular_velocity)+pdot(row->angular_b,b->d.angular_velocity);
    double before=row->impulse;row->impulse=pclamp(before-(speed+row->bias+row->gamma*before)/effective,row->lower,row->upper);
    prow_apply(a,b,row,row->impulse-before);
}
typedef struct PJointGeometry {
    PhysicsPose a,b;
    PhysicsVec delta,arm_a,arm_b,angular,swing,twist_axis;
    double distance,twist,swing_angle;
    bool twist_valid;
} PJointGeometry;
static PhysicsVec pquat_log(PhysicsQuat q) {
    if(q.w<0){q.x=-q.x;q.y=-q.y;q.z=-q.z;q.w=-q.w;}
    PhysicsVec v=pv(q.x,q.y,q.z);double n=plen(v);return pmul(v,n>DBL_MIN?2*atan2(n,q.w)/n:2);
}
static bool pjoint_geometry(PhysicsWorld* w,const PJoint* j,PJointGeometry* g,PhysicsError* e) {
    PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];*g=(PJointGeometry){0};
    g->a=pcompose(a->d.pose,j->d.frame_a);g->b=pcompose(b->d.pose,j->d.frame_b);
    g->delta=psub(g->b.position,g->a.position);g->distance=plen(g->delta);
    g->arm_a=psub(g->a.position,pcentre(a));g->arm_b=psub(g->b.position,pcentre(b));
    PhysicsQuat relative=pqmul(pqconj(g->a.rotation),g->b.rotation);
    if(!pfinite(g->a.position)||!pfinite(g->b.position)||!isfinite(g->distance)||!pqnorm(&relative))return pfail(w,e,"joint frames outside numerical range",j->d.id);
    if(relative.w<0){relative.x=-relative.x;relative.y=-relative.y;relative.z=-relative.z;relative.w=-relative.w;}
    g->angular=pquat_log(relative);
    if(j->d.kind==PHYSICS_JOINT_HINGE||j->d.kind==PHYSICS_JOINT_CONE_TWIST) {
        double n=hypot(relative.w,relative.x);g->twist_valid=n>64*DBL_EPSILON;
        PhysicsQuat twist=g->twist_valid?(PhysicsQuat){relative.x/n,0,0,relative.w/n}:(PhysicsQuat){0,0,0,1};
        g->swing=pquat_log(pqmul(relative,pqconj(twist)));g->swing_angle=hypot(g->swing.y,g->swing.z);
        if(g->twist_valid) {
            g->twist=2*atan2(twist.x,twist.w);
            // derivative of 2 atan2(q.x,q.w), including swing/twist coupling away from the hinge axis.
            g->twist_axis=prot(g->a.rotation,pv(1,(relative.w*relative.z+relative.x*relative.y)/(n*n),
                (relative.x*relative.z-relative.w*relative.y)/(n*n)));
        }
    }
    return true;
}
static bool pjoints_geometry_valid(PhysicsWorld* w,PhysicsError* e) {
    // inactive/broken frames are still exported; validate them before any body snapshot can commit.
    for(size_t i=0;i<w->d.joint_count;i++){PJointGeometry geometry;if(!pjoint_geometry(w,&w->joints[i],&geometry,e))return false;}
    return true;
}
static double pjoint_limit_error(const PhysicsJointDesc* d,double value) {
    return d->limit_enabled?fmax(0,fmax(d->lower_limit-value,value-d->upper_limit)):0;
}
static PhysicsJointState pjoint_state(const PJoint* j,const PJointGeometry* g) {
    PhysicsJointState state={.id=j->d.id,.body_a=j->d.body_a,.body_b=j->d.body_b,.anchor_a=g->a.position,.anchor_b=g->b.position,
        .force=j->force,.torque=j->torque,.linear_error=g->distance,.enabled=pjoint_active(j),.broken=j->broken};
    if(j->d.kind==PHYSICS_JOINT_DISTANCE) {
        state.coordinate_valid=true;state.coordinate=g->distance;state.linear_error=j->d.limit_enabled&&!j->d.spring?pjoint_limit_error(&j->d,g->distance):fabs(g->distance-j->d.length);
    } else if(j->d.kind==PHYSICS_JOINT_PRISMATIC) {
        PhysicsVec axis=prot(g->a.rotation,pv(1,0,0));state.coordinate_valid=true;state.coordinate=pdot(g->delta,axis);
        state.linear_error=hypot(plen(psub(g->delta,pmul(axis,state.coordinate))),pjoint_limit_error(&j->d,state.coordinate));
        state.angular_error=plen(g->angular);
    } else if(j->d.kind==PHYSICS_JOINT_FIXED)state.angular_error=plen(g->angular);
    else if(j->d.kind==PHYSICS_JOINT_HINGE||j->d.kind==PHYSICS_JOINT_CONE_TWIST) {
        state.coordinate_valid=g->twist_valid;state.coordinate=g->twist;state.angular_error=hypot(j->d.kind==PHYSICS_JOINT_HINGE?g->swing_angle:fmax(0,g->swing_angle-j->d.cone_angle),
            pjoint_limit_error(&j->d,g->twist));
    }
    return state;
}
typedef struct PJointRows {
    PhysicsWorld* world;PJoint* joint;PJointGeometry* geometry;PhysicsError* error;
    PConstraintRow previous[PJOINT_MAX_ROWS];unsigned count;
} PJointRows;
static bool pjoint_row(PJointRows* build,PConstraintRow row,unsigned key,double error,double lower,double upper,bool angular,bool soft,bool speculative) {
    PhysicsWorld* w=build->world;PJoint* j=build->joint;
    if(j->row_count==PJOINT_MAX_ROWS)return pfail(w,build->error,"joint row quota exceeded",j->d.id);
    row.key=key;row.lower=lower;row.upper=upper;row.position=!soft;row.position_error=error;
    row.position_cap=.2*(angular?1:w->d.length_scale);double dt=w->d.timestep;
    if(soft) {
        double coefficient=j->d.damping+dt*j->d.stiffness;
        if(!isfinite(coefficient)||coefficient<=0)return pfail(w,build->error,"joint spring coefficient outside numerical range",j->d.id);
        row.gamma=(1/dt)/coefficient;row.bias=error*(j->d.stiffness/coefficient);
    } else {
        // split recovery from allowed travel; speculative limits allow only the remaining in-range displacement.
        double recovery=pclamp(error,-.2*(angular?1:w->d.length_scale),.2*(angular?1:w->d.length_scale));
        row.bias=(speculative?error:.2*recovery)/dt;
    }
    for(unsigned i=0;i<build->count;i++)if(build->previous[i].key==key) {
        PConstraintRow* old=&build->previous[i];PhysicsVec axis=plen(row.linear)>0?row.linear:row.angular_b;
        PhysicsVec previous=plen(old->linear)>0?old->linear:old->angular_b;
        if(pdot(punit(axis),punit(previous))>.99)row.impulse=pclamp(old->impulse,lower,upper);
        break;
    }
    double effective=prow_effective(&w->bodies[j->a],&w->bodies[j->b],&row);
    if(!pfinite(row.linear)||!pfinite(row.angular_a)||!pfinite(row.angular_b)||!isfinite(row.bias)||!isfinite(row.gamma)||
        !isfinite(effective)||effective<=0||!isfinite(effective+row.gamma))return pfail(w,build->error,"joint row is singular or outside numerical range",j->d.id);
    j->rows[j->row_count++]=row;return true;
}
static PConstraintRow pjoint_axis(PJointGeometry* g,PhysicsVec axis,bool angular,bool moving_frame) {
    return angular?(PConstraintRow){.angular_a=pmul(axis,-1),.angular_b=axis}:
        prow_linear(axis,moving_frame?padd(g->arm_a,g->delta):g->arm_a,g->arm_b);
}
static bool pjoint_locks(PJointRows* build,bool angular,unsigned first,unsigned count,unsigned key,PhysicsVec errors,bool moving_frame) {
    for(unsigned i=0;i<count;i++) {
        unsigned axis=first+i;PhysicsVec direction=pbase(axis);
        if(angular||moving_frame)direction=prot(build->geometry->a.rotation,direction);
        double error=angular?paxis(errors,axis):pdot(build->geometry->delta,direction);
        if(!pjoint_row(build,pjoint_axis(build->geometry,direction,angular,moving_frame),key+i,error,-DBL_MAX,DBL_MAX,angular,false,false))return false;
    }
    return true;
}
static bool pjoint_limits(PJointRows* build,PConstraintRow row,unsigned key,double coordinate,bool angular) {
    PhysicsJointDesc* d=&build->joint->d;if(!d->limit_enabled)return true;
    if(d->lower_limit==d->upper_limit)return pjoint_row(build,row,key,coordinate-d->lower_limit,-DBL_MAX,DBL_MAX,angular,false,false);
    double low=coordinate-d->lower_limit,high=coordinate-d->upper_limit;
    return pjoint_row(build,row,key,low,0,DBL_MAX,angular,false,low>=0)&&
        pjoint_row(build,row,key+1,high,-DBL_MAX,0,angular,false,high<=0);
}
static bool pjoint_motor(PJointRows* build,PConstraintRow row,unsigned key,bool angular) {
    PhysicsJointDesc* d=&build->joint->d;if(!d->motor_enabled)return true;
    // torque limits bound the physical vector even when the twist derivative is not a unit axis.
    double magnitude=angular?plen(row.angular_b):plen(row.linear),bound=d->motor_max_force*build->world->d.timestep/magnitude;
    if(!isfinite(bound))return pfail(build->world,build->error,"joint motor impulse outside numerical range",d->id);
    unsigned before=build->joint->row_count;
    if(!pjoint_row(build,row,key,0,-bound,bound,angular,false,false))return false;
    build->joint->rows[before].bias=-d->motor_speed;build->joint->rows[before].position=false;return true;
}
static bool pjoint_build_rows(PhysicsWorld* w,PJoint* j,PhysicsError* e) {
        PJointGeometry geometry;
        if(!pjoint_active(j)){j->row_count=0;return true;}
        if(!pjoint_geometry(w,j,&geometry,e))return false;
        PJointRows build={.world=w,.joint=j,.geometry=&geometry,.error=e,.count=j->row_count};
        memcpy(build.previous,j->rows,j->row_count*sizeof(PConstraintRow));j->row_count=0;
        j->anchor_a=geometry.a.position;j->anchor_b=geometry.b.position;j->arm_b=geometry.arm_b;
        if(j->d.kind==PHYSICS_JOINT_DISTANCE) {
            if(geometry.distance<=1e-9*w->d.length_scale)return pfail(w,e,"distance joint anchors are coincident",j->d.id);
            PConstraintRow row=pjoint_axis(&geometry,pmul(geometry.delta,1/geometry.distance),false,false);
            if((j->d.spring||(!j->d.limit_enabled&&!j->d.motor_enabled))&&
                !pjoint_row(&build,row,0,geometry.distance-j->d.length,-DBL_MAX,DBL_MAX,false,j->d.spring,false))return false;
            if(!pjoint_motor(&build,row,3,false)||!pjoint_limits(&build,row,1,geometry.distance,false))return false;
            return true;
        }
        bool slider=j->d.kind==PHYSICS_JOINT_PRISMATIC;
        if(!pjoint_locks(&build,false,slider?1:0,slider?2:3,0,pv(0,0,0),slider))return false;
        if(j->d.kind==PHYSICS_JOINT_SPHERICAL)return true;
        if(j->d.kind==PHYSICS_JOINT_FIXED||slider) {
            if(!pjoint_locks(&build,true,0,3,slider?2:3,geometry.angular,false))return false;
            if(!slider)return true;
            PhysicsVec axis=prot(geometry.a.rotation,pv(1,0,0));PConstraintRow row=pjoint_axis(&geometry,axis,false,true);
            if(!pjoint_motor(&build,row,7,false)||!pjoint_limits(&build,row,5,pdot(geometry.delta,axis),false))return false;
            return true;
        }
        if(!geometry.twist_valid)return pfail(w,e,"joint swing/twist frame is singular",j->d.id);
        unsigned limit_key=5;
        if(j->d.kind==PHYSICS_JOINT_HINGE) {
            if(!pjoint_locks(&build,true,1,2,3,geometry.swing,false))return false;
        } else {
            limit_key=4;
            if(geometry.swing_angle>0) {
                PhysicsVec axis=prot(geometry.a.rotation,pmul(geometry.swing,1/geometry.swing_angle));
                double error=geometry.swing_angle-j->d.cone_angle;
                if(!pjoint_row(&build,pjoint_axis(&geometry,axis,true,false),3,error,-DBL_MAX,0,true,false,error<=0))return false;
            }
        }
        PConstraintRow twist=pjoint_axis(&geometry,geometry.twist_axis,true,false);
        if(!pjoint_motor(&build,twist,limit_key+2,true)||!pjoint_limits(&build,twist,limit_key,geometry.twist,true))return false;
    return true;
}
static void pbody_position(PBody* b,PhysicsVec linear,PhysicsVec angular) {
    if(!b->inverse_mass||b->sleeping)return;
    PhysicsVec centre=padd(pcentre(b),pmul(linear,b->inverse_mass));
    b->d.pose.rotation=pintegrate(b->d.pose.rotation,pinverse(b,angular),1);
    b->d.pose.position=psub(centre,prot(b->d.pose.rotation,b->com));
}
static bool pjoint_factor(PhysicsWorld* w,PJoint* j,bool position,PhysicsError* e) {
    PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];j->base_count=0;
    for(unsigned i=0;i<j->row_count;i++) {
        PConstraintRow* row=&j->rows[i];
        if(row->lower==-DBL_MAX&&row->upper==DBL_MAX&&(!position||row->position))j->base_indices[j->base_count++]=i;
        for(unsigned k=0;k<=i;k++)j->matrix[k][i]=j->matrix[i][k]=prow_cross(a,b,row,&j->rows[k]);
    }
    for(unsigned i=0;i<j->base_count;i++) {
        unsigned row=j->base_indices[i];double diagonal=j->matrix[row][row]+(position?0:j->rows[row].gamma);
        j->base_scale[i]=sqrt(diagonal);
        if(!isfinite(diagonal)||diagonal<=0)return pfail(w,e,"joint block diagonal outside numerical range",j->d.id);
    }
    // normalize mixed linear/angular units before factoring the small symmetric block.
    memset(j->factor,0,sizeof(j->factor));
    for(unsigned i=0;i<j->base_count;i++)for(unsigned k=0;k<=i;k++) {
        unsigned row=j->base_indices[i],column=j->base_indices[k];
        double value=(j->matrix[row][column]+(i==k&&!position?j->rows[row].gamma:0))/(j->base_scale[i]*j->base_scale[k]);
        for(unsigned m=0;m<k;m++)value-=j->factor[i][m]*j->factor[k][m];
        if(i==k) {
            if(!isfinite(value)||value<=64*DBL_EPSILON)return pfail(w,e,"joint bilateral block is singular",j->d.id);
            j->factor[i][k]=sqrt(value);
        } else j->factor[i][k]=value/j->factor[k][k];
    }
    return true;
}
static void pjoint_base_solve(const PJoint* j,const double input[PJOINT_MAX_ROWS],double output[PJOINT_MAX_ROWS]) {
    double forward[PJOINT_MAX_ROWS];
    for(unsigned i=0;i<j->base_count;i++) {
        double value=input[i]/j->base_scale[i];for(unsigned k=0;k<i;k++)value-=j->factor[i][k]*forward[k];
        forward[i]=value/j->factor[i][i];
    }
    for(unsigned i=j->base_count;i>0;i--) {
        unsigned row=i-1;double value=forward[row];for(unsigned k=row+1;k<j->base_count;k++)value-=j->factor[k][row]*output[k];
        output[row]=value/j->factor[row][row];
    }
    for(unsigned i=0;i<j->base_count;i++)output[i]/=j->base_scale[i];
}
static double prow_speed(PBody* a,PBody* b,const PConstraintRow* row) {
    return pdot(row->linear,psub(b->d.velocity,a->d.velocity))+pdot(row->angular_a,a->d.angular_velocity)+pdot(row->angular_b,b->d.angular_velocity);
}
static void pjoint_delta(PBody* a,PBody* b,const PConstraintRow* row,double delta,bool position) {
    if(position) {
        pbody_position(a,pmul(row->linear,-delta),pmul(row->angular_a,delta));
        pbody_position(b,pmul(row->linear,delta),pmul(row->angular_b,delta));
    } else prow_apply(a,b,row,delta);
}
static bool pjoint_block_solve(PhysicsWorld* w,PJoint* j,bool position,PhysicsError* e) {
    PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];if(!prow_awake(a,b))return true;
    double impulse[PJOINT_MAX_ROWS]={0},target[PJOINT_MAX_ROWS]={0},right[PJOINT_MAX_ROWS]={0},solved[PJOINT_MAX_ROWS]={0};
    for(unsigned i=0;i<j->row_count;i++) {
        PConstraintRow* row=&j->rows[i];impulse[i]=position?0:row->impulse;
        target[i]=position?.2*pclamp(row->position_error,-row->position_cap,row->position_cap):row->bias;
    }
    for(unsigned i=0;i<j->base_count;i++) {
        unsigned index=j->base_indices[i];PConstraintRow* row=&j->rows[index];
        right[i]=position?-target[index]:-(prow_speed(a,b,row)+row->bias+row->gamma*row->impulse);
    }
    pjoint_base_solve(j,right,solved);
    for(unsigned i=0;i<j->base_count;i++) {
        unsigned index=j->base_indices[i];impulse[index]+=solved[i];pjoint_delta(a,b,&j->rows[index],solved[i],position);
    }
    for(unsigned index=0;index<j->row_count;index++) {
        PConstraintRow* row=&j->rows[index];if((position&&!row->position)||(row->lower==-DBL_MAX&&row->upper==DBL_MAX))continue;
        for(unsigned i=0;i<j->base_count;i++)right[i]=j->matrix[index][j->base_indices[i]];
        pjoint_base_solve(j,right,solved);
        // eliminate bilateral reactions so a limit/motor uses inertia about the constrained anchor.
        double effective=j->matrix[index][index]+(position?0:row->gamma);
        for(unsigned i=0;i<j->base_count;i++)effective-=right[i]*solved[i];
        double residual=target[index];
        if(position){for(unsigned i=0;i<j->row_count;i++)residual+=j->matrix[index][i]*impulse[i];}
        else residual+=prow_speed(a,b,row)+row->gamma*impulse[index];
        double next;
        if(effective<=128*DBL_EPSILON*j->matrix[index][index]) {
            if(row->lower<=-DBL_MAX||row->upper>=DBL_MAX)return pfail(w,e,"joint constrained row is singular",j->d.id);
            // a motor stalled by a locked coordinate exerts its bounded effort without moving the lock.
            next=residual<0?row->upper:residual>0?row->lower:impulse[index];
        } else next=pclamp(impulse[index]-residual/effective,row->lower,row->upper);
        double delta=next-impulse[index];impulse[index]=next;pjoint_delta(a,b,row,delta,position);
        for(unsigned i=0;i<j->base_count;i++) {
            unsigned base=j->base_indices[i];double correction=-solved[i]*delta;impulse[base]+=correction;
            pjoint_delta(a,b,&j->rows[base],correction,position);
        }
    }
    if(!position)for(unsigned i=0;i<j->row_count;i++)j->rows[i].impulse=impulse[i];
    return true;
}
static bool pjoint_prepare(PhysicsWorld* w,PJoint* j,PhysicsError* e) {
    return pjoint_build_rows(w,j,e)&&pjoint_factor(w,j,false,e);
}
static bool pjoints_prepare(PhysicsWorld* w,PhysicsError* e) {
    for(size_t i=0;i<w->d.joint_count;i++)if(!pjoint_prepare(w,&w->joints[i],e))return false;
    return true;
}
static void prow_position(PBody* a,PBody* b,const PConstraintRow* row,double error,double beta) {
    if(!prow_awake(a,b))return;double effective=prow_effective(a,b,row);if(effective<=0)return;
    double impulse=pclamp(-beta*error/effective,row->lower,row->upper);
    pbody_position(a,pmul(row->linear,-impulse),pmul(row->angular_a,impulse));
    pbody_position(b,pmul(row->linear,impulse),pmul(row->angular_b,impulse));
}
static bool pjoints_project(PhysicsWorld* w,PhysicsError* e) {
    if(!w->d.joint_count)return true;
    for(unsigned iteration=0;iteration<w->d.iterations;iteration++) {
        for(size_t i=0;i<w->d.joint_count;i++) {
            // projection recomputes current geometry without changing velocity impulses or the reaction cache.
            PJoint probe=w->joints[i];if(!pjoint_prepare(w,&probe,e))return false;
            if(!pjoint_factor(w,&probe,true,e)||!pjoint_block_solve(w,&probe,true,e))return false;
        }
        for(size_t i=0;i<w->pair_count;i++) {
            PPair pair=w->pairs[i];if(pair.sensor)continue;
            PCollider *ca=&w->colliders[pair.a],*cb=&w->colliders[pair.b];
            ca->pose=pcompose(w->bodies[ca->body].d.pose,ca->d.pose);cb->pose=pcompose(w->bodies[cb->body].d.pose,cb->d.pose);
            PContact contacts[PHYSICS_MANIFOLD_MAX_POINTS];size_t count=0;PContactWriter output={contacts,&count,PHYSICS_MANIFOLD_MAX_POINTS,NULL};
            if(!pcollide(w,&output,pair.a,pair.b,e))return false;
            for(size_t k=0;k<count;k++) {
                PContact* c=&contacts[k];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
                PConstraintRow row=prow_linear(c->normal,psub(c->point,pcentre(a)),psub(c->point,pcentre(b)));row.upper=DBL_MAX;
                prow_position(a,b,&row,-fmax(0,c->depth-.001*w->d.length_scale),.15);
            }
        }
    }
    return true;
}
static void pjoints_warm(PhysicsWorld* w) {
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];
        if(prow_awake(a,b))for(unsigned k=0;k<j->row_count;k++)prow_apply(a,b,&j->rows[k],j->rows[k].impulse);
    }
}
static bool pjoints_solve(PhysicsWorld* w,PhysicsError* e) {
    for(size_t i=0;i<w->d.joint_count;i++)if(!pjoint_block_solve(w,&w->joints[i],false,e))return false;
    return true;
}
static bool pjoints_finish(PhysicsWorld* w,PhysicsError* e) {
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];PhysicsVec impulse=pv(0,0,0),angular=pv(0,0,0);
        for(unsigned k=0;k<j->row_count;k++){PConstraintRow* row=&j->rows[k];impulse=padd(impulse,pmul(row->linear,row->impulse));angular=padd(angular,pmul(row->angular_b,row->impulse));}
        j->force=pmul(impulse,1/w->d.timestep);j->torque=pmul(psub(angular,pcross(j->arm_b,impulse)),1/w->d.timestep);
        if(!pfinite(j->force)||!pfinite(j->torque))return pfail(w,e,"joint reaction outside numerical range",j->d.id);
        if(!pjoint_active(j)||(!j->d.break_force&&!j->d.break_torque))continue;
        if((j->d.break_force&&plen(j->force)>j->d.break_force)||(j->d.break_torque&&plen(j->torque)>j->d.break_torque)) {
            if(w->event_count==w->d.event_limit)return pfail(w,e,"physics event quota exceeded",j->d.id);
            j->broken=true;w->events[w->event_count++]=(PhysicsEvent){.tick=w->tick+1,.body_a=j->d.body_a,.body_b=j->d.body_b,
                .joint=j->d.id,.force=j->force,.torque=j->torque};
        }
    }
    return true;
}
static double peffective(PBody* a,PBody* b,PhysicsVec ra,PhysicsVec rb,PhysicsVec n) {
    PConstraintRow row=prow_linear(n,ra,rb);return prow_effective(a,b,&row);
}
static bool psolve(PhysicsWorld* w,PContact* contacts,size_t count,bool warm,PhysicsError* e) {
    for(size_t i=0;i<count;i++) {
        PContact* c=&contacts[i];PCollider *ca=&w->colliders[c->a],*cb=&w->colliders[c->b];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
        if((!a->sleeping&&a->d.motion!=PHYSICS_FIXED)||(!b->sleeping&&b->d.motion!=PHYSICS_FIXED)) {
            if(a->sleeping)pwake(a);if(b->sleeping)pwake(b);
        }
        PhysicsVec ra=psub(c->point,pcentre(a)),rb=psub(c->point,pcentre(b));
        double vn=pdot(psub(pvelocity(b,rb),pvelocity(a,ra)),c->normal);
        c->bounce=vn< -w->d.length_scale? -fmax(ca->d.restitution,cb->d.restitution)*vn:0;
        double nearest=0.01*w->d.length_scale;nearest*=nearest;
        // previous contacts are grouped by collider pair; warm starting only searches that manifold.
        size_t lo=0,hi=warm?w->previous_count:0;
        while(lo<hi){size_t mid=lo+(hi-lo)/2;PContact* old=&w->previous[mid];
            if(old->a<c->a||(old->a==c->a&&old->b<c->b))lo=mid+1;else hi=mid;}
        for(size_t j=lo;warm&&j<w->previous_count&&w->previous[j].a==c->a&&w->previous[j].b==c->b;j++) {
            PContact* old=&w->previous[j];double d=pdot(psub(c->point,old->point),psub(c->point,old->point));
            if(old->a==c->a&&old->b==c->b&&pdot(old->normal,c->normal)>0.99&&d<nearest) {
                nearest=d;c->impulse=old->impulse;c->friction_impulse=old->friction_impulse;
            }
        }
        // sleeping/fixed pairs retain their cache without applying it again to an inactive body.
        if((a->sleeping||a->d.motion==PHYSICS_FIXED)&&(b->sleeping||b->d.motion==PHYSICS_FIXED))continue;
        PhysicsVec impulse=padd(pmul(c->normal,c->impulse),c->friction_impulse);
        pimpulse(a,pmul(impulse,-1),ra);pimpulse(b,impulse,rb);
    }
    if(warm)pjoints_warm(w);
    for(unsigned iteration=0;iteration<w->d.iterations;iteration++) {
    for(size_t i=0;i<count;i++) {
        PContact* c=&contacts[i];PCollider *ca=&w->colliders[c->a],*cb=&w->colliders[c->b];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
        if((a->sleeping||a->d.motion==PHYSICS_FIXED)&&(b->sleeping||b->d.motion==PHYSICS_FIXED))continue;
        PhysicsVec ra=psub(c->point,pcentre(a)),rb=psub(c->point,pcentre(b)),relative=psub(pvelocity(b,rb),pvelocity(a,ra));
        PConstraintRow normal=prow_linear(c->normal,ra,rb);normal.impulse=c->impulse;normal.bias=-c->bounce;normal.upper=DBL_MAX;
        prow_solve(a,b,&normal);c->impulse=normal.impulse;PhysicsVec impulse;
        relative=psub(pvelocity(b,rb),pvelocity(a,ra));PhysicsVec tangent=psub(relative,pmul(c->normal,pdot(relative,c->normal)));
        double speed=plen(tangent);if(speed>1e-12) {
            tangent=pmul(tangent,1/speed);double denom=peffective(a,b,ra,rb,tangent);
            PhysicsVec next=psub(c->friction_impulse,pmul(tangent,speed/denom));
            double limit=(sqrt(ca->d.friction)*sqrt(cb->d.friction))*c->impulse,length=plen(next);if(length>limit)next=pmul(next,limit/length);
            impulse=psub(next,c->friction_impulse);c->friction_impulse=next;pimpulse(a,pmul(impulse,-1),ra);pimpulse(b,impulse,rb);
        }
    }
    if(warm&&!pjoints_solve(w,e))return false;
    }
    // positional correction is separate from velocity impulses, so resting contacts add no kinetic energy.
    for(size_t i=0;i<count;i++) {
        PContact* c=&contacts[i];PBody *a=&w->bodies[w->colliders[c->a].body],*b=&w->bodies[w->colliders[c->b].body];
        double inverse=a->inverse_mass+b->inverse_mass;if(inverse<=0||(a->sleeping&&b->sleeping))continue;
        PhysicsVec correction=pmul(c->normal,0.15*fmax(0,c->depth-0.001*w->d.length_scale)/inverse);
        ptranslate(a,pmul(correction,-a->inverse_mass));ptranslate(b,pmul(correction,b->inverse_mass));
    }
    return true;
}

static size_t pisland_root(PhysicsWorld* w,size_t body) {
    size_t root=body;while(w->bodies[root].island!=root)root=w->bodies[root].island;
    while(w->bodies[body].island!=body){size_t next=w->bodies[body].island;w->bodies[body].island=root;body=next;}
    return root;
}
static void pisland_link(PhysicsWorld* w,size_t a,size_t b) {
    if(!w->bodies[a].inverse_mass||!w->bodies[b].inverse_mass)return;
    a=pisland_root(w,a);b=pisland_root(w,b);if(a>b){size_t swap=a;a=b;b=swap;}w->bodies[b].island=a;
}
static void pislands_wake(PhysicsWorld* w) {
    if(!w->d.joint_count)return;
    for(size_t i=0;i<w->d.body_count;i++){w->bodies[i].island=i;w->bodies[i].island_awake=false;}
    for(size_t i=0;i<w->contact_count;i++)pisland_link(w,w->colliders[w->contacts[i].a].body,w->colliders[w->contacts[i].b].body);
    for(size_t i=0;i<w->d.joint_count;i++)if(pjoint_active(&w->joints[i]))pisland_link(w,w->joints[i].a,w->joints[i].b);
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].inverse_mass&&!w->bodies[i].sleeping)w->bodies[pisland_root(w,i)].island_awake=true;
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];if(!pjoint_active(j))continue;PBody *a=&w->bodies[j->a],*b=&w->bodies[j->b];
        bool moving_a=!a->inverse_mass&&(plen(a->d.velocity)>0||plen(a->d.angular_velocity)>0);
        bool moving_b=!b->inverse_mass&&(plen(b->d.velocity)>0||plen(b->d.angular_velocity)>0);
        bool motor=j->d.motor_enabled&&j->d.motor_speed!=0&&j->d.motor_max_force>0;
        if(a->inverse_mass&&(moving_b||motor))w->bodies[pisland_root(w,j->a)].island_awake=true;
        if(b->inverse_mass&&(moving_a||motor))w->bodies[pisland_root(w,j->b)].island_awake=true;
    }
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].inverse_mass&&w->bodies[i].sleeping&&w->bodies[pisland_root(w,i)].island_awake)pwake(&w->bodies[i]);
}
static void pislands_sleep(PhysicsWorld* w) {
    if(!w->d.joint_count)return;
    for(size_t i=0;i<w->d.body_count;i++)w->bodies[i].island_idle=DBL_MAX;
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].inverse_mass) {
        PBody* root=&w->bodies[pisland_root(w,i)];root->island_idle=fmin(root->island_idle,w->bodies[i].idle);
    }
    for(size_t i=0;i<w->d.joint_count;i++) {
        PJoint* j=&w->joints[i];if(!pjoint_active(j)||!j->d.motor_enabled||j->d.motor_speed==0||j->d.motor_max_force==0)continue;
        if(w->bodies[j->a].inverse_mass)w->bodies[pisland_root(w,j->a)].island_idle=0;
        if(w->bodies[j->b].inverse_mass)w->bodies[pisland_root(w,j->b)].island_idle=0;
    }
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].inverse_mass) {
        PBody* b=&w->bodies[i];b->sleeping=w->bodies[pisland_root(w,i)].island_idle>.5;
        if(b->sleeping)b->d.velocity=b->d.angular_velocity=pv(0,0,0);
    }
}

static bool pmove(PhysicsWorld* w,double dt,PhysicsError* e) {
    for(size_t i=0;i<w->d.body_count;i++) {
        PBody* b=&w->bodies[i];if(b->d.motion==PHYSICS_FIXED||b->sleeping)continue;
        PhysicsVec centre=padd(pcentre(b),pmul(b->d.velocity,dt));
        b->d.pose.rotation=pintegrate(b->d.pose.rotation,b->d.angular_velocity,dt);
        b->d.pose.position=psub(centre,prot(b->d.pose.rotation,b->com));
        if(!pfinite(b->d.pose.position)||!pfinite(b->d.velocity)||!pfinite(b->d.angular_velocity)||!pqnorm(&b->d.pose.rotation))
            return pfail(w,e,"physics numerical overflow",b->d.id);
    }
    return true;
}
typedef struct PCcdImpact {size_t a,b;PhysicsHit hit;} PCcdImpact;
static bool pccd_find(PhysicsWorld* w,double remaining,PCcdImpact* impact,size_t* work,size_t* triangles,PhysicsError* e) {
    *impact=(PCcdImpact){0};
    for(size_t i=0;i<w->d.collider_count;i++)for(size_t j=i+1;j<w->d.collider_count;j++) {
        PCollider *ca=&w->colliders[i],*cb=&w->colliders[j];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
        if((!a->d.ccd&&!b->d.ccd)||ca->body==cb->body||ca->d.sensor||cb->d.sensor||
            !(ca->d.membership&cb->d.filter)||!(cb->d.membership&ca->d.filter)||!pjoint_collides(w,ca->body,cb->body))continue;
        size_t source=a->d.ccd?i:j,target=a->d.ccd?j:i;
        ca=&w->colliders[source];cb=&w->colliders[target];a=&w->bodies[ca->body];b=&w->bodies[cb->body];
        PhysicsVec delta=pmul(psub(a->d.velocity,b->d.velocity),remaining);
        PBox swept=pbox_sweep(ca->bounds,pv(0,0,0),delta);
        swept=pbox_expand(swept,pbox_query_padding(swept,cb->bounds));
        if(!pfinite(delta)||!pfinite(swept.lo)||!pfinite(swept.hi))return pfail(w,e,"CCD sweep outside numerical range",ca->d.id);
        // include possible target rotation in pruning, even when that pair must reject below.
        PhysicsVec bounds_centre=padd(cb->bounds.lo,pmul(psub(cb->bounds.hi,cb->bounds.lo),.5));
        double radius=plen(psub(cb->bounds.hi,cb->bounds.lo))/2+plen(psub(bounds_centre,pcentre(b)));
        double rotation=fmin(2,plen(b->d.angular_velocity)*remaining)*radius;
        PhysicsVec angular=pv(rotation,rotation,rotation);PBox target_bounds={psub(cb->bounds.lo,angular),padd(cb->bounds.hi,angular)};
        if(!pfinite(target_bounds.lo)||!pfinite(target_bounds.hi))return pfail(w,e,"CCD target sweep outside numerical range",cb->d.id);
        if(!poverlap(swept,target_bounds))continue;
        if(++*work>PHYSICS_CCD_PAIR_BUDGET)return pfail(w,e,"CCD pair work quota exceeded",ca->d.id);
        // dynamic responses may rotate their targets; only a single sphere is spin invariant.
        if(pjoint_attached(w,ca->body)||pjoint_attached(w,cb->body))return pfail(w,e,"joint CCD coupling is not supported yet",cb->d.id);
        bool sphere=pbody_sphere(w,cb->body);
        if((b->d.motion==PHYSICS_DYNAMIC&&!sphere)||(!sphere&&plen(b->d.angular_velocity)>0))
            return pfail(w,e,"CCD target requires a single dynamic sphere or nonrotating fixed/kinematic environment",cb->d.id);
        PhysicsGeometry ga=pgeometry(ca),gb=pgeometry(cb);PhysicsHit hit={0};const char* error=NULL;
        if(!physics_geometry_cast(&ga,&gb,delta,PHYSICS_CAST_MAX_ITERATIONS,&hit,&error))return pfail(w,e,error,ca->d.id);
        *triangles+=hit.triangle_tests;
        if(*triangles>PHYSICS_MESH_QUERY_BUDGET)return pfail(w,e,"CCD triangle work quota exceeded",cb->d.id);
        if(!hit.found)continue;
        if(hit.overlapping) {
            // use discrete manifolds for penetration; closest witnesses cover the tiny positive GJK gap.
            // scratch manifolds must not consume or overwrite persistent tick contacts.
            PContact contacts[PHYSICS_MANIFOLD_MAX_POINTS];size_t count=0;
            PContactWriter probe={contacts,&count,PHYSICS_MANIFOLD_MAX_POINTS,NULL};
            if(!pcollide(w,&probe,source,target,e))return false;
            double deepest=-1;PhysicsVec normal=punit(psub(hit.point_b,ca->pose.position));
            for(size_t k=0;k<count;k++)if(contacts[k].depth>deepest) {
                deepest=contacts[k].depth;normal=contacts[k].normal;
                hit.point=hit.point_b=contacts[k].point;
            }
            PhysicsVec ra=psub(hit.point,pcentre(a)),rb=psub(hit.point_b,pcentre(b));
            double closing=pdot(psub(pvelocity(a,ra),pvelocity(b,rb)),normal);
            double roundoff=64*DBL_EPSILON*(plen(a->d.velocity)+plen(b->d.velocity));
            // fixed-orientation convex separation cannot close again after a nonapproaching start contact.
            if(closing<=roundoff||plen(normal)==0)continue;
            hit.normal=pmul(normal,-1);
        }
        if(!impact->hit.found||hit.fraction<impact->hit.fraction)*impact=(PCcdImpact){source,target,hit};
    }
    return true;
}
static bool pccd_move(PhysicsWorld* w,double dt,PhysicsError* e) {
    bool enabled=false;for(size_t i=0;i<w->d.body_count;i++)enabled|=w->bodies[i].d.ccd;
    if(!enabled)return pmove(w,dt,e);
    double remaining=dt;size_t work=0,triangles=0;unsigned impacts=0;
    PContactWriter output={w->contacts,&w->contact_count,w->d.contact_limit,NULL};
    while(remaining>0) {
        if(!pupdate(w,e))return false;
        PCcdImpact impact;
        if(!pccd_find(w,remaining,&impact,&work,&triangles,e))return false;
        if(!impact.hit.found)return pmove(w,remaining,e);
        if(impacts++==PHYSICS_CCD_MAX_IMPACTS)return pfail(w,e,"CCD impact quota exceeded",w->colliders[impact.a].d.id);
        double advance=remaining*impact.hit.fraction;
        PhysicsVec target_shift=pmul(w->bodies[w->colliders[impact.b].body].d.velocity,advance);
        if(!pmove(w,advance,e))return false;
        remaining-=advance;
        size_t a=impact.a,b=impact.b;PhysicsVec normal=pmul(impact.hit.normal,-1);
        if(a>b){size_t swap=a;a=b;b=swap;normal=pmul(normal,-1);}
        PhysicsVec point=padd(pmul(padd(impact.hit.point,impact.hit.point_b),.5),target_shift);
        if(!pcontact(w,&output,a,b,point,normal,0,e))return false;
        if(!psolve(w,&w->contacts[w->contact_count-1],1,false,e))return false;
        bool recorded=false;for(size_t i=0;i<w->pair_count;i++)recorded|=w->pairs[i].a==a&&w->pairs[i].b==b;
        if(!recorded) {
            if(w->pair_count==w->d.contact_limit)return pfail(w,e,"physics pair quota exceeded",0);
            w->pairs[w->pair_count++]=(PPair){a,b,false};
        }
    }
    return true;
}

bool physics_apply(PhysicsWorld* w,const PhysicsCommand* commands,size_t count,PhysicsError* e) {
    if(!w||w->faulted||count>65536||(count&&!commands))return pfail(w,e,"invalid or faulted physics world",0);
    for(size_t i=0;i<count;i++) {
        PhysicsCommand c=commands[i];size_t index=pbody(w,c.body);
        if((unsigned)c.kind>PHYSICS_ANGULAR_VELOCITY||!pfinite(c.value)||(c.at_point&&!pfinite(c.point))||
            (c.kind!=PHYSICS_GRAVITY&&index==SIZE_MAX)||((c.kind==PHYSICS_TELEPORT||c.kind==PHYSICS_TARGET)&&!ppose(&c.pose))||
            (c.kind==PHYSICS_TARGET&&w->bodies[index].d.motion!=PHYSICS_KINEMATIC_POSITION)||
            ((c.kind==PHYSICS_VELOCITY||c.kind==PHYSICS_ANGULAR_VELOCITY)&&w->bodies[index].d.motion==PHYSICS_FIXED&&plen(c.value)>0))
            return pfail(w,e,"invalid physics command",c.body);
    }
    for(size_t i=0;i<count;i++) {
        PhysicsCommand c=commands[i];if(c.kind==PHYSICS_GRAVITY){
            w->d.gravity=c.value;
            for(size_t j=0;j<w->d.body_count;j++)if(w->bodies[j].d.motion==PHYSICS_DYNAMIC)pwake(&w->bodies[j]);
            continue;
        }
        PBody* b=&w->bodies[pbody(w,c.body)];pwake(b);PhysicsVec value=c.local?prot(b->d.pose.rotation,c.value):c.value;
        PhysicsVec point=c.local?padd(b->d.pose.position,prot(b->d.pose.rotation,c.point)):c.point;
        PhysicsVec r=c.at_point?psub(point,pcentre(b)):pv(0,0,0);
        if(c.kind==PHYSICS_IMPULSE)pimpulse(b,value,r);
        else if(c.kind==PHYSICS_FORCE){b->force=padd(b->force,value);b->torque=padd(b->torque,pcross(r,value));}
        else if(c.kind==PHYSICS_TORQUE)b->torque=padd(b->torque,value);
        else if(c.kind==PHYSICS_VELOCITY)b->d.velocity=value;
        else if(c.kind==PHYSICS_ANGULAR_VELOCITY)b->d.angular_velocity=value;
        else if(c.kind==PHYSICS_TELEPORT){ppose(&c.pose);b->d.pose=c.pose;w->previous_count=0;pjoint_invalidate_body(w,pbody(w,c.body));}
        else if(c.kind==PHYSICS_TARGET){ppose(&c.pose);b->target=c.pose;b->target_set=true;}
    }
    if(!pupdate(w,e))return false;
    PhysicsState updated;return physics_read(w,&updated,e);
}
static bool pbody_response_equal(const PBody* a,const PBody* b) {
    return a->d.motion==b->d.motion&&a->inverse_mass==b->inverse_mass&&a->mass==b->mass&&
        !memcmp(a->inertia,b->inertia,sizeof(a->inertia))&&!memcmp(&a->com,&b->com,sizeof(PhysicsVec));
}
static bool pconfigure(PhysicsWorld* w,const PhysicsBodyDesc* bodies,size_t body_count,
        const PhysicsColliderDesc* colliders,size_t collider_count,const PhysicsJointDesc* joints,size_t joint_count,bool full_description,
        const PhysicsVec* gravity,const PhysicsCommand* commands,size_t command_count,PhysicsError* e) {
    if(!w||w->faulted||body_count!=w->d.body_count||collider_count!=w->d.collider_count||(body_count&&!bodies)||(collider_count&&!colliders))
        return pfail(w,e,"invalid or faulted physics world",0);
    for(size_t i=0;i<body_count;i++) {
        PhysicsBodyDesc d=bodies[i];
        if(d.id!=w->bodies[i].d.id||(!full_description&&d.motion!=w->bodies[i].d.motion)||!pbody_valid(&d))
            return pfail(w,e,"physics configuration changes a body's identity or uses an invalid motion",d.id);
    }
    for(size_t i=0;i<collider_count;i++) {
        PhysicsColliderDesc d=colliders[i];
        if(d.id!=w->colliders[i].d.id||d.body!=w->colliders[i].d.body||!pcollider_valid(&d))return pfail(w,e,"physics configuration changes a collider's identity or owner",d.id);
    }
    // mass properties can still reject the edit, so the previous configuration is kept until all succeed
    PBody* bodies_before=mem_calloc(body_count+1,sizeof(PBody),MEM_CAT_EVAL);
    if(!bodies_before)return pfail(w,e,"physics allocation failed",0);
    // geometry and roles belong to one candidate, so a dynamic-to-fixed mesh replacement validates together.
    PCollider* replacement=pcolliders_build(w,colliders,collider_count,bodies,e);
    if(!replacement){mem_free(bodies_before);return false;}
    PhysicsBodyState* snapshots_before=w->states;
    PhysicsBodyState* snapshots=mem_calloc(body_count+1,sizeof(PhysicsBodyState),MEM_CAT_EVAL);
    if(!snapshots){mem_free(bodies_before);pcolliders_destroy(replacement,collider_count);return pfail(w,e,"physics snapshot allocation failed",0);}
    PhysicsVec gravity_before=w->d.gravity;size_t previous_count=w->previous_count;
    memcpy(bodies_before,w->bodies,body_count*sizeof(PBody));w->states=snapshots;
    // stage owned geometry separately: neither rejection nor source destruction may invalidate the old hull.
    PCollider* colliders_before=w->colliders;w->colliders=replacement;
    bool valid=true;
    for(size_t i=0;i<body_count&&valid;i++) {
        PBody* b=&w->bodies[i];
        if(b->d.motion!=bodies[i].motion) {
            // role activation keeps the last achieved pose/velocity, without replaying an older role's target or effort.
            b->d.motion=bodies[i].motion;b->target_set=false;b->force=b->torque=pv(0,0,0);pwake(b);
            if(b->d.motion==PHYSICS_FIXED)b->d.velocity=b->d.angular_velocity=pv(0,0,0);
        }
        b->d.linear_damping=bodies[i].linear_damping;b->d.angular_damping=bodies[i].angular_damping;b->d.gravity_scale=bodies[i].gravity_scale;b->d.ccd=bodies[i].ccd;
        b->mass=b->inverse_mass=0;b->com=pv(0,0,0);memset(b->inertia,0,sizeof(b->inertia));memset(b->inverse,0,sizeof(b->inverse));
        valid=pmass(w,i,e);
    }
    PJoint* joints_before=w->joints;size_t count_before=w->d.joint_count;PhysicsJointState* states_before=w->joint_states;
    if(valid&&(full_description||count_before)) {
        if(!full_description)joint_count=count_before;
        PJoint* staged=full_description?pjoints_build(w,joints,joint_count,e):mem_calloc(joint_count+1,sizeof(PJoint),MEM_CAT_EVAL);
        if(staged&&!full_description)memcpy(staged,joints_before,count_before*sizeof(PJoint));
        PhysicsJointState* states=staged?mem_calloc(joint_count+1,sizeof(PhysicsJointState),MEM_CAT_EVAL):NULL;
        if(!staged||!states){mem_free(staged);valid=false;if(!e||!e->message)pfail(w,e,"physics joint allocation failed",0);}
        else {w->joints=staged;w->d.joint_count=joint_count;w->joint_states=states;}
    }
    if(valid&&gravity&&memcmp(gravity,&w->d.gravity,sizeof(PhysicsVec))) {
        w->d.gravity=*gravity;for(size_t i=0;i<body_count;i++)if(w->bodies[i].inverse_mass)pwake(&w->bodies[i]);
    }
    // commands run against staged owners, so even a numerical fault cannot invalidate the old published views.
    if(valid)valid=pccd_valid(w,e)&&(command_count?physics_apply(w,commands,command_count,e):pupdate(w,e))&&pjoints_prepare(w,e);
    if(!valid) {
        memcpy(w->bodies,bodies_before,body_count*sizeof(PBody));w->colliders=colliders_before;
        if(w->joints!=joints_before){mem_free(w->joints);mem_free(w->joint_states);}
        w->joints=joints_before;w->d.joint_count=count_before;w->joint_states=states_before;
        mem_free(w->states);w->states=snapshots_before;w->d.gravity=gravity_before;w->previous_count=previous_count;
        w->faulted=false;pupdate(w,NULL);
    }
    if(valid) {
        for(size_t i=0;i<body_count;i++) {
            PBody *old=&bodies_before[i],*now=&w->bodies[i];
            bool mass=!pbody_response_equal(old,now);
            if(mass)pjoint_invalidate_body(w,i);
            if(mass||old->d.linear_damping!=now->d.linear_damping||old->d.angular_damping!=now->d.angular_damping||
                old->d.gravity_scale!=now->d.gravity_scale||old->d.ccd!=now->d.ccd)pwake(now);
        }
        for(size_t i=0;i<collider_count;i++)if(!physics_collider_equal(&colliders_before[i].d,&w->colliders[i].d))pwake(&w->bodies[w->colliders[i].body]);
        for(size_t i=0;i<count_before;i++) {
            PJoint* old=&joints_before[i];bool retained=false;
            for(size_t k=0;k<w->d.joint_count;k++)retained|=physics_joint_equal(&old->d,&w->joints[k].d);
            if(!retained){pwake(&w->bodies[old->a]);pwake(&w->bodies[old->b]);}
        }
        for(size_t i=0;i<w->d.joint_count;i++) {
            PJoint* now=&w->joints[i];bool retained=false;
            for(size_t k=0;k<count_before;k++)retained|=physics_joint_equal(&now->d,&joints_before[k].d);
            if(!retained){pwake(&w->bodies[now->a]);pwake(&w->bodies[now->b]);}
        }
    }
    if(valid) {
        // preserve unrelated warm starts; a joint-only edit does not change collision manifolds.
        size_t kept=0;
        for(size_t i=0;i<w->previous_count;i++) {
            PContact contact=w->previous[i];PCollider *a=&w->colliders[contact.a],*b=&w->colliders[contact.b];
            if(physics_collider_equal(&a->d,&colliders_before[contact.a].d)&&physics_collider_equal(&b->d,&colliders_before[contact.b].d)&&
                pbody_response_equal(&w->bodies[a->body],&bodies_before[a->body])&&pbody_response_equal(&w->bodies[b->body],&bodies_before[b->body]))
                w->previous[kept++]=contact;
        }
        w->previous_count=kept;
    }
    mem_free(bodies_before);pcolliders_destroy(valid?colliders_before:replacement,collider_count);
    if(!valid)return false;
    if(w->joints!=joints_before){mem_free(joints_before);mem_free(states_before);}
    mem_free(snapshots_before);
    PhysicsState updated;return physics_read(w,&updated,e);
}
bool physics_configure(PhysicsWorld* w,const PhysicsBodyDesc* bodies,size_t body_count,
        const PhysicsColliderDesc* colliders,size_t collider_count,PhysicsError* e) {
    return pconfigure(w,bodies,body_count,colliders,collider_count,NULL,0,false,NULL,NULL,0,e);
}
bool physics_reconfigure_commands(PhysicsWorld* w,const PhysicsDesc* d,const PhysicsCommand* commands,size_t count,PhysicsError* e) {
    if(!w||!d||!pfinite(d->gravity)||d->timestep!=w->d.timestep||d->length_scale!=w->d.length_scale||d->iterations!=w->d.iterations||
        d->contact_limit!=w->d.contact_limit||d->event_limit!=w->d.event_limit)return pfail(w,e,"physics reconfiguration changes world structure",0);
    return pconfigure(w,d->bodies,d->body_count,d->colliders,d->collider_count,d->joints,d->joint_count,true,&d->gravity,commands,count,e);
}
bool physics_reconfigure(PhysicsWorld* w,const PhysicsDesc* d,PhysicsError* e) {
    return physics_reconfigure_commands(w,d,NULL,0,e);
}
bool physics_read(PhysicsWorld* w,PhysicsState* out,PhysicsError* e) {
    if(!w||!out)return pfail(w,e,"invalid physics read",0);
    if(!w->faulted)for(size_t i=0;i<w->d.body_count;i++) {
        PBody* b=&w->bodies[i];PhysicsBodyState s={b->d.id,b->d.pose,b->d.velocity,b->d.angular_velocity,pcentre(b),b->mass,b->sleeping,b->d.motion,b->target,b->target_set};w->states[i]=s;
    }
    if(!w->faulted)for(size_t i=0;i<w->d.joint_count;i++) {
        PJointGeometry geometry;if(!pjoint_geometry(w,&w->joints[i],&geometry,e))return false;
        w->joint_states[i]=pjoint_state(&w->joints[i],&geometry);
    }
    PhysicsState state={w->tick,w->tick*w->d.timestep,w->states,w->d.body_count,w->events,w->event_count,w->candidates,w->contact_count,w->joint_states,w->d.joint_count};*out=state;return true;
}
bool physics_step(PhysicsWorld* w,size_t ticks,PhysicsState* out,PhysicsError* e) {
    if(!w||!out||w->faulted||ticks>1000000||ticks>UINT64_MAX-w->tick||!isfinite((w->tick+ticks)*w->d.timestep))return pfail(w,e,"invalid physics tick count or faulted world",0);
    w->event_count=0;
    for(size_t tick=0;tick<ticks;tick++) {
        size_t completed_events=w->event_count;
        double dt=w->d.timestep;
        for(size_t i=0;i<w->d.body_count;i++) {
            PBody* b=&w->bodies[i];
            if(b->d.motion==PHYSICS_KINEMATIC_POSITION) {
                b->d.velocity=b->target_set?pmul(psub(b->target.position,b->d.pose.position),1/dt):pv(0,0,0);
                PhysicsQuat dq=pqmul(b->target.rotation,pqconj(b->d.pose.rotation));if(dq.w<0){dq.x=-dq.x;dq.y=-dq.y;dq.z=-dq.z;dq.w=-dq.w;}
                double length=hypot(hypot(dq.x,dq.y),dq.z);b->d.angular_velocity=b->target_set&&length>1e-12?pmul(pv(dq.x,dq.y,dq.z),2*atan2(length,dq.w)/(length*dt)):pv(0,0,0);
            }
        }
        if(w->d.joint_count) {
            if(!pdetect(w,e)){w->event_count=completed_events;w->faulted=true;return false;}
            pislands_wake(w);
        }
        for(size_t i=0;i<w->d.body_count;i++) {
            PBody* b=&w->bodies[i];if(b->d.motion!=PHYSICS_DYNAMIC||b->sleeping)continue;
            b->d.velocity=pmul(padd(b->d.velocity,pmul(padd(pmul(w->d.gravity,b->d.gravity_scale),pmul(b->force,b->inverse_mass)),dt)),1/(1+b->d.linear_damping*dt));
            PhysicsVec local_w=prot(pqconj(b->d.pose.rotation),b->d.angular_velocity);
            PhysicsVec gyro=prot(b->d.pose.rotation,pcross(local_w,pmat(b->inertia,local_w)));
            b->d.angular_velocity=pmul(padd(b->d.angular_velocity,pmul(pinverse(b,psub(b->torque,gyro)),dt)),1/(1+b->d.angular_damping*dt));
            b->force=b->torque=pv(0,0,0);
        }
        if(!pdetect(w,e)){w->event_count=completed_events;w->faulted=true;return false;}
        if(!pjoints_prepare(w,e)){w->event_count=completed_events;w->faulted=true;return false;}
        if(!psolve(w,w->contacts,w->contact_count,true,e)){w->event_count=completed_events;w->faulted=true;return false;}
        if(!pccd_move(w,dt,e)||!pjoints_project(w,e)||!ppublish_pairs(w,e)||!pjoints_finish(w,e)){w->event_count=completed_events;w->faulted=true;return false;}
        for(size_t i=0;i<w->d.body_count;i++) {
            PBody* b=&w->bodies[i];if(b->d.motion==PHYSICS_FIXED||b->sleeping)continue;
            if(b->d.motion==PHYSICS_KINEMATIC_POSITION&&b->target_set){b->d.pose=b->target;b->target_set=false;}
            if(b->d.motion==PHYSICS_DYNAMIC) {
                bool still=plen(b->d.velocity)<0.02*w->d.length_scale&&plen(b->d.angular_velocity)<0.02;
                b->idle=still?b->idle+dt:0;if(!w->d.joint_count&&b->idle>0.5){b->sleeping=true;b->d.velocity=b->d.angular_velocity=pv(0,0,0);}
            }
        }
        pislands_sleep(w);
        // bounds and snapshots publish together; faulted reads retain the last completed body states.
        if(!pupdate(w,e)){w->event_count=completed_events;return false;}
        // only whole completed ticks become visible to readers and event consumers.
        w->tick++;qsort(w->contacts,w->contact_count,sizeof(PContact),pcontact_compare);
        memcpy(w->previous,w->contacts,w->contact_count*sizeof(PContact));w->previous_count=w->contact_count;
        memcpy(w->old_pairs,w->pairs,w->pair_count*sizeof(PPair));w->old_pair_count=w->pair_count;
        PhysicsState completed;physics_read(w,&completed,e);
    }
    return physics_read(w,out,e);
}
static bool pray_ball(PhysicsVec origin,PhysicsVec direction,double radius,double maximum,double* time,PhysicsVec* normal) {
    double b=pdot(origin,direction),value=pdot(origin,origin)-radius*radius,disc=b*b-value;
    if(disc<0)return false;
    double t=value>0?-b-sqrt(disc):0;if(t<0||t>maximum)return false;
    *time=t;*normal=punit(padd(origin,pmul(direction,t)));return true;
}
static bool pray_round(PCollider* c,PhysicsVec origin,PhysicsVec direction,double maximum,double* time,PhysicsVec* normal) {
    double half=c->d.shape==PHYSICS_CAPSULE?c->d.height/2:0;
    PhysicsVec closest=pv(0,pclamp(origin.y,-half,half),0);
    if(plen(psub(origin,closest))<=c->d.radius){*time=0;*normal=punit(psub(origin,closest));return true;}
    bool found=false;double nearest=maximum;
    if(half>0) {
        double aa=direction.x*direction.x+direction.z*direction.z;
        double bb=origin.x*direction.x+origin.z*direction.z,cc=origin.x*origin.x+origin.z*origin.z-c->d.radius*c->d.radius;
        double disc=bb*bb-aa*cc;
        if(aa>0&&disc>=0) {
            double t=(-bb-sqrt(disc))/aa,y=origin.y+direction.y*t;
            if(t>=0&&t<=nearest&&y>=-half&&y<=half) {
                found=true;nearest=t;*normal=punit(pv(origin.x+direction.x*t,0,origin.z+direction.z*t));
            }
        }
    }
    for(unsigned i=0;i<(half>0?2u:1u);i++) {
        PhysicsVec offset=pv(0,i?half:-half,0),n;double t;
        if(pray_ball(psub(origin,offset),direction,c->d.radius,nearest,&t,&n)) {
            double y=origin.y+direction.y*t;
            if(half==0||(i?y>=half:y<=-half)){found=true;nearest=t;*normal=n;}
        }
    }
    *time=nearest;return found;
}
static bool pray(PCollider* c,PhysicsVec origin,PhysicsVec direction,double maximum,PhysicsHit* hit) {
    PhysicsQuat inverse=pqconj(c->pose.rotation);PhysicsVec o=prot(inverse,psub(origin,c->pose.position)),d=prot(inverse,direction),normal=pv(0,0,0);double t=0;
    if(c->hull) {
        PhysicsHit local={0};if(!physics_hull_ray(c->hull,o,d,maximum,&local))return false;t=local.distance;normal=local.normal;
    } else if(c->d.shape!=PHYSICS_BOX) {
        if(!pray_round(c,o,d,maximum,&t,&normal))return false;
    } else {
        double far=maximum;PhysicsVec h=pmul(c->d.size,0.5);
        for(unsigned i=0;i<3;i++) {
            double start=paxis(o,i),v=paxis(d,i),half=paxis(h,i);if(fabs(v)<DBL_MIN){if(start< -half||start>half)return false;continue;}
            double near=(-half-start)/v,end=(half-start)/v,sign=-1;if(near>end){double tmp=near;near=end;end=tmp;sign=1;}
            if(near>t){t=near;normal=pmul(pbase(i),sign);}far=fmin(far,end);if(t>far)return false;
        }
    }
    hit->found=true;hit->distance=t;hit->point=padd(origin,pmul(direction,t));hit->normal=prot(c->pose.rotation,normal);hit->collider=c->d.id;hit->body=c->d.body;return true;
}
static bool pquery_valid(PhysicsWorld* w,const PhysicsQuery* q,PhysicsError* e) {
    bool shape=q->kind==PHYSICS_SHAPE_OVERLAP||q->kind==PHYSICS_SHAPE_CAST;
    double direction=plen(q->direction);
    if((unsigned)q->kind>PHYSICS_SHAPE_CAST||!pfinite(q->origin)||!pfinite(q->direction)||!pfinite(q->size)||
        !isfinite(q->distance)||q->distance<0||!isfinite(direction)||q->cast_iterations>PHYSICS_CAST_MAX_ITERATIONS||
        ((q->kind==PHYSICS_RAY||(q->kind==PHYSICS_SHAPE_CAST&&q->distance>0))&&direction==0)||
        (q->kind==PHYSICS_OVERLAP&&(q->size.x<0||q->size.y<0||q->size.z<0)))return pfail(w,e,"invalid physics query parameters",0);
    if(q->kind==PHYSICS_DISTANCE||shape) {
        size_t source=pcollider(w,q->collider_a);
        if(source==SIZE_MAX||(q->kind==PHYSICS_DISTANCE&&pcollider(w,q->collider_b)==SIZE_MAX))
            return pfail(w,e,"physics shape/distance query references an unknown collider",0);
        if(shape&&w->colliders[source].mesh)return pfail(w,e,"shape queries require a convex source collider",q->collider_a);
    }
    return true;
}
typedef struct PQueryOutput {
    PhysicsHit* hits;
    size_t capacity,count,triangle_tests;
    bool all;
    PhysicsHit nearest;
} PQueryOutput;
static int phit_compare(const void* l,const void* r) {
    const PhysicsHit *a=l,*b=r;
    return a->distance<b->distance?-1:a->distance>b->distance?1:a->collider<b->collider?-1:a->collider>b->collider;
}
static bool pquery_add(PhysicsWorld* w,PQueryOutput* out,PhysicsHit hit,PhysicsError* e) {
    if(!hit.found)return true;
    if(out->all) {
        if(out->count==out->capacity)return pfail(w,e,"physics query result capacity exceeded",hit.collider);
        out->hits[out->count++]=hit;
    } else if(!out->nearest.found||phit_compare(&hit,&out->nearest)<0)out->nearest=hit;
    return true;
}
static bool pquery_run(PhysicsWorld* w,const PhysicsQuery* q,PQueryOutput* out,PhysicsError* e) {
    if(q->kind==PHYSICS_DISTANCE) {
        PCollider *a=&w->colliders[pcollider(w,q->collider_a)],*b=&w->colliders[pcollider(w,q->collider_b)];
        PhysicsGeometry ga=pgeometry(a),gb=pgeometry(b);const char* error=NULL;PhysicsHit hit={0};
        if(!physics_geometry_distance(&ga,&gb,&hit,&error))return pfail(w,e,error,0);
        hit.collider=b->d.id;hit.body=b->d.body;return pquery_add(w,out,hit,e);
    }
    bool shape=q->kind==PHYSICS_SHAPE_OVERLAP||q->kind==PHYSICS_SHAPE_CAST;
    PhysicsVec direction=punit(q->direction);PCollider* source=shape?&w->colliders[pcollider(w,q->collider_a)]:NULL;
    PhysicsGeometry probe=source?pgeometry(source):(PhysicsGeometry){0};PBox swept={0};
    if(shape) {
        probe.pose.position=padd(probe.pose.position,q->origin);
        PhysicsVec delta=q->kind==PHYSICS_SHAPE_CAST?pmul(direction,q->distance):pv(0,0,0);
        swept=pbox_sweep(source->bounds,q->origin,delta);
        if(!pfinite(probe.pose.position)||!pfinite(swept.lo)||!pfinite(swept.hi))return pfail(w,e,"shape query outside numerical range",q->collider_a);
    }
    for(size_t j=0;j<w->d.collider_count;j++) {
        PCollider* c=&w->colliders[j];
        if((source&&source->body==c->body)||(c->d.sensor&&!q->include_sensors)||!(q->membership&c->d.filter)||!(q->filter&c->d.membership))continue;
        PhysicsHit hit={0};bool found=false;double maximum=out->all||!out->nearest.found?q->distance:out->nearest.distance;
        if(shape) {
            // inflate only the broad phase by the distance kernel's scale/roundoff envelope.
            PBox bounds=pbox_expand(swept,pbox_query_padding(swept,c->bounds));
            if(!poverlap(bounds,c->bounds))continue;
            PhysicsGeometry target=pgeometry(c);const char* error=NULL;
            bool valid=q->kind==PHYSICS_SHAPE_CAST?
                physics_geometry_cast(&probe,&target,pmul(direction,q->distance),q->cast_iterations,&hit,&error):
                physics_geometry_distance(&probe,&target,&hit,&error);
            if(!valid)return pfail(w,e,error,c->d.id);
            found=hit.found&&(q->kind==PHYSICS_SHAPE_CAST||hit.overlapping);
        } else if(c->mesh&&(q->kind==PHYSICS_RAY||q->kind==PHYSICS_POINT)) {
            PhysicsQuat inverse=pqconj(c->pose.rotation);PhysicsVec origin=prot(inverse,psub(q->origin,c->pose.position));const char* error=NULL;
            bool valid=q->kind==PHYSICS_RAY?physics_mesh_ray(c->mesh,c->d.double_sided,origin,prot(inverse,direction),maximum,&hit,&error):
                physics_mesh_point(c->mesh,origin,&hit,&error);
            if(!valid)return pfail(w,e,error,c->d.id);found=hit.found;
            hit.point=q->kind==PHYSICS_RAY?padd(q->origin,pmul(direction,hit.distance)):q->origin;
            hit.normal=prot(c->pose.rotation,hit.normal);
        } else if(q->kind==PHYSICS_RAY)found=pray(c,q->origin,direction,maximum,&hit);
        else if(q->kind==PHYSICS_POINT) {
            PhysicsVec p=prot(pqconj(c->pose.rotation),psub(q->origin,c->pose.position)),h=pmul(c->d.size,0.5);
            double half=c->d.shape==PHYSICS_CAPSULE?c->d.height/2:0;
            found=c->hull?physics_hull_contains(c->hull,p):c->d.shape==PHYSICS_BOX?fabs(p.x)<=h.x&&fabs(p.y)<=h.y&&fabs(p.z)<=h.z:
                plen(psub(p,pv(0,pclamp(p.y,-half,half),0)))<=c->d.radius;
            hit.point=q->origin;
        } else {
            PBox box={psub(q->origin,pmul(q->size,0.5)),padd(q->origin,pmul(q->size,0.5))};found=poverlap(box,c->bounds);hit.point=q->origin;
        }
        out->triangle_tests+=hit.triangle_tests;
        if(out->triangle_tests>PHYSICS_MESH_QUERY_BUDGET)return pfail(w,e,"physics query triangle work quota exceeded",c->d.id);
        hit.found=found;hit.collider=c->d.id;hit.body=c->d.body;
        if(!pquery_add(w,out,hit,e))return false;
    }
    out->nearest.triangle_tests=out->triangle_tests;return true;
}
bool physics_query(PhysicsWorld* w,const PhysicsQuery* queries,size_t count,PhysicsHit* results,PhysicsError* e) {
    if(!w||w->faulted||count>65536||(count&&(!queries||!results)))return pfail(w,e,"invalid physics query",0);
    for(size_t i=0;i<count;i++)if(!pquery_valid(w,&queries[i],e))return false;
    if(!pupdate(w,e))return false;
    for(size_t i=0;i<count;i++) {
        PQueryOutput output={0};if(!pquery_run(w,&queries[i],&output,e))return false;results[i]=output.nearest;
    }
    return true;
}
bool physics_query_hits(PhysicsWorld* w,const PhysicsQuery* query,PhysicsHit* results,size_t capacity,size_t* count,PhysicsError* e) {
    if(count)*count=0;
    if(!w||w->faulted||!query||!count||(capacity&&!results)||capacity>65536)return pfail(w,e,"invalid all-hit physics query",0);
    if(!pquery_valid(w,query,e)||!pupdate(w,e))return false;
    PQueryOutput output={.hits=results,.capacity=capacity,.all=true};
    if(!pquery_run(w,query,&output,e))return false;
    if(output.count>1)qsort(results,output.count,sizeof(PhysicsHit),phit_compare);
    *count=output.count;return true;
}

typedef struct PCharacter {
    PhysicsWorld* world;
    const PhysicsCharacterMove* move;
    PCollider* source;
    PhysicsVec up;
    double slope,tolerance;
    size_t ignored,tests,triangles;
} PCharacter;

PhysicsCharacterMove physics_character_defaults(double scale) {
    PhysicsCharacterMove move={0};move.up=pv(0,1,0);move.skin=.001*scale;move.slope_angle=acos(-1.0)/4;
    move.step_height=.3*scale;move.snap_distance=.1*scale;move.max_slides=8;move.recovery_iterations=8;
    move.cast_iterations=PHYSICS_CAST_MAX_ITERATIONS;move.geometry_budget=4096;move.platform_pose.rotation.w=1;return move;
}
static bool pcharacter_eligible(PCharacter* c,size_t index) {
    PCollider* target=&c->world->colliders[index];
    return index!=c->ignored&&target->body!=c->source->body&&!target->d.sensor&&
        (c->source->d.membership&target->d.filter)&&(target->d.membership&c->source->d.filter);
}
static bool pcharacter_work(PCharacter* c,PhysicsError* e) {
    if(c->tests==c->move->geometry_budget)return pfail(c->world,e,"character geometry work quota exceeded",c->move->body);
    c->tests++;return true;
}
static bool pcharacter_near(PCharacter* c,size_t target,PhysicsVec offset,PhysicsVec delta) {
    PBox swept=pbox_sweep(c->source->bounds,offset,delta),bounds=c->world->colliders[target].bounds;
    return poverlap(pbox_expand(swept,c->move->skin+2*c->tolerance+pbox_query_padding(swept,bounds)),bounds);
}
static bool pcharacter_manifold(PCharacter* c,size_t target,PhysicsVec offset,PContact* contacts,size_t* count,PhysicsError* e) {
    if(!pcharacter_work(c,e))return false;
    // a private pair view reuses every analytic/mesh manifold path without writing live collider caches.
    PCollider pair[2]={*c->source,c->world->colliders[target]};pair[0].d.radius+=c->move->skin+2*c->tolerance;
    pair[0].pose.position=padd(pair[0].pose.position,offset);
    PhysicsWorld probe=*c->world;probe.colliders=pair;*count=0;
    PContactWriter writer={contacts,count,PHYSICS_MANIFOLD_MAX_POINTS,&c->triangles};return pcollide(&probe,&writer,0,1,e);
}
static bool pcharacter_record(PCharacter* c,PhysicsCharacterState* state,size_t target,PhysicsVec point,PhysicsVec normal,PhysicsError* e) {
    uint64_t collider=c->world->colliders[target].d.id;
    for(size_t i=0;i<state->contact_count;i++)if(state->contacts[i].collider==collider&&pdot(state->contacts[i].normal,normal)>1-1e-8)return true;
    if(state->contact_count==PHYSICS_CHARACTER_MAX_CONTACTS)return pfail(c->world,e,"character contact quota exceeded",c->move->body);
    state->contacts[state->contact_count++]=(PhysicsCharacterContact){c->world->colliders[target].d.body,collider,point,normal};
    double vertical=pdot(c->up,normal);
    if(vertical<=-c->slope)state->ceiling=true;else if(vertical<c->slope)state->wall=true;
    return true;
}
static bool pcharacter_recover(PCharacter* c,PhysicsVec* offset,PhysicsCharacterState* state,PhysicsError* e) {
    for(unsigned iteration=0;iteration<=c->move->recovery_iterations;iteration++) {
        double deepest=c->tolerance/2;size_t selected=SIZE_MAX;PContact correction={0};
        for(size_t i=0;i<c->world->d.collider_count;i++)if(pcharacter_eligible(c,i)&&pcharacter_near(c,i,*offset,pv(0,0,0))) {
            PContact contacts[PHYSICS_MANIFOLD_MAX_POINTS];size_t count=0;
            if(!pcharacter_manifold(c,i,*offset,contacts,&count,e))return false;
            // the recovery shell covers zero-contact roundoff before casts sample the smaller skin shell.
            for(size_t k=0;k<count;k++)if(contacts[k].depth>deepest) {deepest=contacts[k].depth;correction=contacts[k];selected=i;}
        }
        if(selected==SIZE_MAX)return true;
        if(iteration==c->move->recovery_iterations)return pfail(c->world,e,"character overlap recovery budget exceeded",c->move->body);
        PhysicsVec normal=pmul(correction.normal,-1),delta=pmul(normal,deepest);
        if(!pcharacter_record(c,state,selected,correction.point,normal,e))return false;
        *offset=padd(*offset,delta);state->recovery=padd(state->recovery,delta);
        if(!pfinite(*offset))return pfail(c->world,e,"character recovery outside numerical range",c->move->body);
    }
    return false;
}
static bool pcharacter_cast(PCharacter* c,PhysicsVec offset,PhysicsVec delta,PhysicsHit* result,size_t* selected,PhysicsError* e) {
    *result=(PhysicsHit){0};*selected=SIZE_MAX;
    PhysicsColliderDesc desc=c->source->d;desc.radius+=c->move->skin;
    PhysicsGeometry source=pgeometry(c->source);source.desc=&desc;source.pose.position=padd(source.pose.position,offset);
    for(size_t i=0;i<c->world->d.collider_count;i++)if(pcharacter_eligible(c,i)&&pcharacter_near(c,i,offset,delta)) {
        if(!pcharacter_work(c,e))return false;
        PhysicsGeometry target=pgeometry(&c->world->colliders[i]);PhysicsHit hit={0};const char* message=NULL;
        if(!physics_geometry_cast(&source,&target,delta,c->move->cast_iterations,&hit,&message))return pfail(c->world,e,message,c->move->body);
        c->triangles+=hit.triangle_tests;
        if(c->triangles>PHYSICS_MESH_QUERY_BUDGET)return pfail(c->world,e,"character triangle work quota exceeded",c->move->body);
        if(!hit.found)continue;
        if(hit.overlapping)return pfail(c->world,e,"character cast begins in unresolved overlap",c->move->body);
        hit.collider=target.desc->id;hit.body=target.desc->body;
        if(!result->found||phit_compare(&hit,result)<0){*result=hit;*selected=i;}
    }
    return true;
}
static bool pcharacter_feasible(PhysicsVec v,const PhysicsVec* planes,unsigned count,double tolerance) {
    for(unsigned i=0;i<count;i++)if(pdot(v,planes[i])< -tolerance)return false;return true;
}
static PhysicsVec pcharacter_clip(PhysicsVec v,const PhysicsVec* planes,unsigned count,double tolerance) {
    if(pcharacter_feasible(v,planes,count,tolerance))return v;
    PhysicsVec best=pv(0,0,0);double distance=pdot(v,v);
    // the closest feasible velocity lies on a plane, on two planes' intersection, or at the origin.
    for(unsigned i=0;i<count;i++)for(unsigned j=i;j<count;j++) {
        PhysicsVec candidate;
        if(i==j)candidate=psub(v,pmul(planes[i],pdot(v,planes[i])));
        else {PhysicsVec axis=pcross(planes[i],planes[j]);double length=plen(axis);if(length<1e-10)continue;
            axis=pmul(axis,1/length);candidate=pmul(axis,pdot(v,axis));}
        double error=pdot(psub(candidate,v),psub(candidate,v));
        if(error<distance&&pcharacter_feasible(candidate,planes,count,tolerance)){best=candidate;distance=error;}
    }
    return best;
}
static bool pcharacter_slide(PCharacter* c,PhysicsVec* offset,PhysicsVec remaining,PhysicsCharacterState* state,PhysicsError* e) {
    PhysicsVec planes[32];unsigned count=0;
    for(unsigned iteration=0;iteration<c->move->max_slides;iteration++) {
        if(plen(remaining)<=c->tolerance)return true;
        PhysicsHit hit;size_t target;if(!pcharacter_cast(c,*offset,remaining,&hit,&target,e))return false;
        if(!hit.found){*offset=padd(*offset,remaining);return true;}
        *offset=padd(*offset,padd(pmul(remaining,hit.fraction),pmul(hit.normal,2*c->tolerance)));
        if(!pcharacter_record(c,state,target,hit.point_b,hit.normal,e))return false;
        remaining=pmul(remaining,1-hit.fraction);PhysicsVec plane=hit.normal;
        double vertical=pdot(plane,c->up);
        // steep ascending faces block horizontal travel instead of converting it into an artificial climb.
        if(vertical>0&&vertical<c->slope)plane=punit(psub(plane,pmul(c->up,vertical)));
        bool duplicate=false;for(unsigned i=0;i<count;i++)duplicate|=pdot(planes[i],plane)>1-1e-10;
        if(!duplicate)planes[count++]=plane;
        remaining=pcharacter_clip(remaining,planes,count,c->tolerance*1e-3);
    }
    return plen(remaining)<=c->tolerance||pfail(c->world,e,"character slide budget exceeded",c->move->body);
}
static bool pcharacter_floor(PCharacter* c,PhysicsVec* offset,double distance,PhysicsCharacterState* state,PhysicsError* e) {
    PhysicsVec delta=pmul(c->up,-distance);PhysicsHit hit;size_t target;
    if(!pcharacter_cast(c,*offset,delta,&hit,&target,e))return false;
    if(!hit.found||pdot(hit.normal,c->up)<c->slope)return true;
    *offset=padd(*offset,padd(pmul(delta,hit.fraction),pmul(hit.normal,2*c->tolerance)));
    state->grounded=true;state->ground_normal=hit.normal;state->platform=hit.collider;state->ground_body=hit.body;
    PCollider* support=&c->world->colliders[target];state->platform_pose=support->pose;
    PBody* body=&c->world->bodies[support->body];state->platform_velocity=pvelocity(body,psub(hit.point_b,pcentre(body)));
    return pcharacter_record(c,state,target,hit.point_b,hit.normal,e);
}
bool physics_character_move(PhysicsWorld* w,const PhysicsCharacterMove* move,PhysicsCharacterState* out,PhysicsError* e) {
    if(!w||w->faulted||!move||!out||!pfinite(move->displacement)||!pfinite(move->up)||plen(move->up)==0||!isfinite(plen(move->up))||
        !isfinite(move->skin)||move->skin<=0||!isfinite(move->slope_angle)||move->slope_angle<=0||move->slope_angle>=acos(-1.0)/2||
        !isfinite(move->step_height)||move->step_height<0||!isfinite(move->snap_distance)||move->snap_distance<0||
        !move->max_slides||move->max_slides>32||!move->recovery_iterations||move->recovery_iterations>32||
        !move->cast_iterations||move->cast_iterations>PHYSICS_CAST_MAX_ITERATIONS||!move->geometry_budget||move->geometry_budget>65536)
        return pfail(w,e,"invalid character movement parameters",move?move->body:0);
    size_t body=pbody(w,move->body),source=SIZE_MAX,count=0;
    if(body==SIZE_MAX||w->bodies[body].d.motion!=PHYSICS_KINEMATIC_POSITION)return pfail(w,e,"character requires a position-controlled kinematic body",move->body);
    for(size_t i=0;i<w->d.collider_count;i++)if(w->colliders[i].body==body&&!w->colliders[i].d.sensor){source=i;count++;}
    if(count!=1||w->colliders[source].d.shape!=PHYSICS_CAPSULE)return pfail(w,e,"character requires exactly one solid capsule",move->body);
    if(!pupdate(w,e))return false;
    PCharacter c={w,move,&w->colliders[source],punit(move->up),cos(move->slope_angle),1e-6*w->d.length_scale,SIZE_MAX,0,0};
    if(move->skin<=4*c.tolerance||!isfinite(c.source->d.radius+move->skin)||
        (c.source->d.height>0&&fabs(pdot(prot(c.source->pose.rotation,pv(0,1,0)),c.up))<1-1e-8))
        return pfail(w,e,"character skin is below precision or capsule axis disagrees with up",move->body);
    for(size_t i=0;i<w->d.joint_count;i++)if(pjoint_active(&w->joints[i])&&(w->joints[i].a==body||w->joints[i].b==body))
        return pfail(w,e,"character movement cannot drive an active joint attachment",move->body);
    PhysicsVec offset=pv(0,0,0),carry=pv(0,0,0);PhysicsCharacterState state={0};state.requested=move->displacement;
    if(move->platform) {
        size_t platform=pcollider(w,move->platform);PhysicsPose before=move->platform_pose;
        if(platform==SIZE_MAX||!ppose(&before)||!pcharacter_eligible(&c,platform))return pfail(w,e,"invalid character support snapshot",move->platform);
        PhysicsPose now=w->colliders[platform].pose;
        PhysicsVec local=prot(pqconj(before.rotation),psub(w->bodies[body].d.pose.position,before.position));
        carry=psub(padd(now.position,prot(now.rotation,local)),w->bodies[body].d.pose.position);
        if(!pfinite(carry))return pfail(w,e,"character platform motion outside numerical range",move->platform);
        c.ignored=platform;
        if(!pcharacter_recover(&c,&offset,&state,e))return false;PhysicsVec start=offset;
        if(!pcharacter_slide(&c,&offset,carry,&state,e))return false;state.platform_motion=psub(offset,start);c.ignored=SIZE_MAX;
    }
    if(!pcharacter_recover(&c,&offset,&state,e))return false;
    PhysicsVec initial=offset;PhysicsCharacterState initial_state=state;
    if(!pcharacter_slide(&c,&offset,move->displacement,&state,e))return false;
    double vertical=pdot(move->displacement,c.up);PhysicsVec horizontal=psub(move->displacement,pmul(c.up,vertical));
    if(state.wall&&move->step_height>0&&vertical<=c.tolerance&&plen(horizontal)>c.tolerance) {
        PhysicsVec start=initial;PhysicsCharacterState grounded=initial_state;
        if(!pcharacter_floor(&c,&start,move->snap_distance+4*c.tolerance,&grounded,e))return false;
        if(grounded.grounded) {
            PhysicsVec rise=pmul(c.up,move->step_height);PhysicsHit ceiling;size_t target;
            if(!pcharacter_cast(&c,start,rise,&ceiling,&target,e))return false;
            if(!ceiling.found) {
                PhysicsVec trial=padd(start,rise);PhysicsCharacterState alternate=initial_state;
                if(!pcharacter_slide(&c,&trial,horizontal,&alternate,e)||
                    !pcharacter_floor(&c,&trial,move->step_height+move->snap_distance+fmax(0,-vertical)+4*c.tolerance,&alternate,e))return false;
                if(alternate.grounded&&pdot(psub(trial,offset),horizontal)>c.tolerance*plen(horizontal)&&
                    pdot(psub(trial,initial),c.up)>c.tolerance&&
                    pdot(psub(trial,initial),c.up)<=move->step_height+4*c.tolerance) {
                    state=alternate;state.stepped=true;offset=trial;
                }
            }
        }
    }
    if(!state.grounded&&vertical<=c.tolerance&&!pcharacter_floor(&c,&offset,move->snap_distance+4*c.tolerance,&state,e))return false;
    if(!pfinite(offset))return pfail(w,e,"character target outside numerical range",move->body);
    state.pose=w->bodies[body].d.pose;state.pose.position=padd(state.pose.position,offset);state.achieved=offset;
    if(!pfinite(state.pose.position))return pfail(w,e,"character target outside numerical range",move->body);
    if(state.grounded&&state.platform==move->platform)state.platform_velocity=pmul(carry,1/w->d.timestep);
    state.geometry_tests=c.tests;state.triangle_tests=c.triangles;*out=state;return true;
}
