#include "physics.h"
#include "physics_math.h"
#include "../mem.h"
#include <float.h>

typedef struct PBody {
    PhysicsBodyDesc d;
    PhysicsVec force,torque,com;
    double mass,inverse_mass,inertia[9],inverse[9],idle;
    bool sleeping,target_set;
    PhysicsPose target;
} PBody;
typedef struct PBox { PhysicsVec lo,hi; } PBox;
typedef struct PCollider { PhysicsColliderDesc d;size_t body;PhysicsPose pose;PBox bounds; } PCollider;
typedef struct PContact {
    size_t a,b;
    PhysicsVec point,normal,friction_impulse;
    double depth,impulse,bounce;
} PContact;
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
    size_t contact_count,previous_count,pair_count,old_pair_count,event_count,candidates;
    uint64_t tick;
    bool faulted;
};

static bool pfail(PhysicsWorld* w,PhysicsError* e,const char* text,uint64_t id) {
    if(e){e->message=text;e->id=id;e->tick=w?w->tick:0;}return false;
}
static size_t pbody(PhysicsWorld* w,uint64_t id) {
    for(size_t i=0;i<w->d.body_count;i++)if(w->bodies[i].d.id==id)return i;return SIZE_MAX;
}
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
static void pimpulse(PBody* b,PhysicsVec impulse,PhysicsVec r) {
    if(!b->inverse_mass)return;
    b->d.velocity=padd(b->d.velocity,pmul(impulse,b->inverse_mass));
    b->d.angular_velocity=padd(b->d.angular_velocity,pinverse(b,pcross(r,impulse)));
}
static bool poverlap(PBox a,PBox b) {
    return a.lo.x<=b.hi.x&&a.hi.x>=b.lo.x&&a.lo.y<=b.hi.y&&a.hi.y>=b.lo.y&&a.lo.z<=b.hi.z&&a.hi.z>=b.lo.z;
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
        }
        c->bounds.lo=psub(c->pose.position,extent);c->bounds.hi=padd(c->pose.position,extent);
        if(!pfinite(c->pose.position)||!pfinite(c->bounds.lo)||!pfinite(c->bounds.hi)) {
            w->faulted=true;return pfail(w,e,"physics collider bounds overflow",c->d.id);
        }
        w->proxies[i].collider=i;w->proxies[i].bounds=c->bounds;
    }
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
static bool pmass(PhysicsWorld* w,size_t index,PhysicsError* e) {
    PBody* b=&w->bodies[index];PhysicsVec first=pv(0,0,0);
    for(size_t i=0;i<w->d.collider_count;i++) {
        PCollider* c=&w->colliders[i];if(c->body!=index)continue;
        double mass=c->d.density*(c->d.shape==PHYSICS_SPHERE?4.0/3.0*acos(-1.0)*pow(c->d.radius,3):c->d.size.x*c->d.size.y*c->d.size.z);
        b->mass+=mass;first=padd(first,pmul(c->d.pose.position,mass));
    }
    if(b->mass>0)b->com=pmul(first,1/b->mass);
    if(!isfinite(b->mass)||!pfinite(b->com))return pfail(w,e,"collider mass outside numerical range",b->d.id);
    if(b->d.motion!=PHYSICS_DYNAMIC)return true;
    if(!isfinite(b->mass)||b->mass<=0)return pfail(w,e,"dynamic body requires positive finite collider mass",b->d.id);
    b->inverse_mass=1/b->mass;
    if(!isfinite(b->inverse_mass))return pfail(w,e,"inverse mass outside numerical range",b->d.id);
    for(size_t i=0;i<w->d.collider_count;i++) {
        PCollider* c=&w->colliders[i];if(c->body!=index||c->d.density==0)continue;
        double mass=c->d.density*(c->d.shape==PHYSICS_SPHERE?4.0/3.0*acos(-1.0)*pow(c->d.radius,3):c->d.size.x*c->d.size.y*c->d.size.z);
        PhysicsVec diagonal=c->d.shape==PHYSICS_SPHERE?pv(0.4*mass*c->d.radius*c->d.radius,0.4*mass*c->d.radius*c->d.radius,0.4*mass*c->d.radius*c->d.radius):
            pmul(pv(c->d.size.y*c->d.size.y+c->d.size.z*c->d.size.z,c->d.size.x*c->d.size.x+c->d.size.z*c->d.size.z,c->d.size.x*c->d.size.x+c->d.size.y*c->d.size.y),mass/12);
        PhysicsVec offset=psub(c->d.pose.position,b->com);
        for(unsigned j=0;j<3;j++) {
            PhysicsVec axis=prot(c->d.pose.rotation,pbase(j));
            for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)
                b->inertia[row*3+col]+=paxis(diagonal,j)*paxis(axis,row)*paxis(axis,col);
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
    return d->id&&(unsigned)d->shape<=PHYSICS_BOX&&ppose(&d->pose)&&
        (d->shape==PHYSICS_SPHERE?(isfinite(d->radius)&&d->radius>0):(pfinite(d->size)&&d->size.x>0&&d->size.y>0&&d->size.z>0))&&
        isfinite(d->density)&&d->density>=0&&isfinite(d->friction)&&d->friction>=0&&isfinite(d->restitution)&&d->restitution>=0&&d->restitution<=1;
}

bool physics_create(const PhysicsDesc* d,PhysicsWorld** result,PhysicsError* e) {
    if(result)*result=NULL;
    if(!d||!result||!pfinite(d->gravity)||!isfinite(d->timestep)||d->timestep<=0||!isfinite(d->length_scale)||d->length_scale<=0||
        !d->iterations||d->iterations>256||!d->contact_limit||d->contact_limit>1048576||!d->event_limit||d->event_limit>1048576||
        d->body_count>65536||d->collider_count>65536||(d->body_count&&!d->bodies)||(d->collider_count&&!d->colliders))
        return pfail(NULL,e,"invalid physics world configuration",0);
    PhysicsWorld* w=mem_calloc(1,sizeof(*w),MEM_CAT_EVAL);if(!w)return pfail(NULL,e,"physics allocation failed",0);
    w->d=*d;w->d.bodies=NULL;w->d.colliders=NULL;
    w->bodies=mem_calloc(d->body_count+1,sizeof(PBody),MEM_CAT_EVAL);
    w->colliders=mem_calloc(d->collider_count+1,sizeof(PCollider),MEM_CAT_EVAL);
    w->states=mem_calloc(d->body_count+1,sizeof(PhysicsBodyState),MEM_CAT_EVAL);
    w->proxies=mem_calloc(d->collider_count+1,sizeof(PProxy),MEM_CAT_EVAL);
    w->contacts=mem_calloc(d->contact_limit,sizeof(PContact),MEM_CAT_EVAL);
    w->previous=mem_calloc(d->contact_limit,sizeof(PContact),MEM_CAT_EVAL);
    w->pairs=mem_calloc(d->contact_limit,sizeof(PPair),MEM_CAT_EVAL);
    w->old_pairs=mem_calloc(d->contact_limit,sizeof(PPair),MEM_CAT_EVAL);
    w->events=mem_calloc(d->event_limit,sizeof(PhysicsEvent),MEM_CAT_EVAL);
    if(!w->bodies||!w->colliders||!w->states||!w->proxies||!w->contacts||!w->previous||!w->pairs||!w->old_pairs||!w->events) {
        pfail(w,e,"physics allocation failed",0);physics_destroy(w);return false;
    }
    for(size_t i=0;i<d->body_count;i++) {
        PBody* b=&w->bodies[i];b->d=d->bodies[i];
        if(!pbody_valid(&b->d)||pbody(w,b->d.id)!=i) {
            pfail(w,e,"invalid or duplicate physics body",b->d.id);physics_destroy(w);return false;
        }
    }
    for(size_t i=0;i<d->collider_count;i++) {
        PCollider* c=&w->colliders[i];c->d=d->colliders[i];c->body=pbody(w,c->d.body);
        bool duplicate=false;for(size_t j=0;j<i;j++)duplicate|=w->colliders[j].d.id==c->d.id;
        if(duplicate||c->body==SIZE_MAX||!pcollider_valid(&c->d)) {
            pfail(w,e,"invalid or duplicate physics collider",c->d.id);physics_destroy(w);return false;
        }
    }
    for(size_t i=0;i<d->body_count;i++)if(!pmass(w,i,e)){physics_destroy(w);return false;}
    if(!pupdate(w,e)){physics_destroy(w);return false;}
    PhysicsState initial;physics_read(w,&initial,e);*result=w;return true;
}
void physics_destroy(PhysicsWorld* w) {
    if(!w)return;
    mem_free(w->bodies);mem_free(w->colliders);mem_free(w->states);mem_free(w->proxies);mem_free(w->contacts);
    mem_free(w->previous);mem_free(w->pairs);mem_free(w->old_pairs);mem_free(w->events);mem_free(w);
}

static bool pcontact(PhysicsWorld* w,size_t a,size_t b,PhysicsVec point,PhysicsVec normal,double depth,PhysicsError* e) {
    if(w->contact_count==w->d.contact_limit)return pfail(w,e,"physics contact quota exceeded",0);
    PContact c={0};c.a=a;c.b=b;c.point=point;c.normal=normal;c.depth=depth;
    w->contacts[w->contact_count++]=c;return true;
}
static PhysicsVec psupport(PCollider* c,PhysicsVec axis) {
    if(c->d.shape==PHYSICS_SPHERE)return padd(c->pose.position,pmul(punit(axis),c->d.radius));
    PhysicsVec local=prot(pqconj(c->pose.rotation),axis),h=pmul(c->d.size,0.5);
    return padd(c->pose.position,prot(c->pose.rotation,pv(local.x>=0?h.x:-h.x,local.y>=0?h.y:-h.y,local.z>=0?h.z:-h.z)));
}
static double pradius(PCollider* c,PhysicsVec axis) {
    PhysicsVec l=prot(pqconj(c->pose.rotation),axis),h=pmul(c->d.size,0.5);return fabs(l.x)*h.x+fabs(l.y)*h.y+fabs(l.z)*h.z;
}
static unsigned pclip(PhysicsVec* input,unsigned count,PhysicsVec* output,PhysicsVec axis,double plane) {
    unsigned n=0;if(!count)return 0;PhysicsVec prev=input[count-1];double dp=pdot(prev,axis)-plane;
    for(unsigned i=0;i<count;i++){PhysicsVec next=input[i];double dn=pdot(next,axis)-plane;
        if((dp<=0)!=(dn<=0))output[n++]=padd(prev,pmul(psub(next,prev),dp/(dp-dn)));
        if(dn<=0)output[n++]=next;prev=next;dp=dn;}
    return n;
}
static bool pbox_contacts(PhysicsWorld* w,size_t a,size_t b,PhysicsError* e) {
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
        return pcontact(w,a,b,pmul(padd(padd(pa,pmul(ea,t)),padd(pb,pmul(eb,u))),0.5),normal,depth,e);
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
        count=pclip(poly,count,scratch,axis,pdot(ref->pose.position,axis)+paxis(ref->d.size,i)/2);
        memcpy(poly,scratch,count*sizeof(PhysicsVec));
    }
    double plane=pdot(ref->pose.position,n)+paxis(ref->d.size,face)/2;
    for(unsigned i=0;i<count;i++){double penetration=plane-pdot(poly[i],n);
        if(penetration>=0&&!pcontact(w,a,b,padd(poly[i],pmul(n,penetration/2)),normal,penetration,e))return false;}
    return true;
}
static bool pcollide(PhysicsWorld* w,size_t a,size_t b,PhysicsError* e) {
    PCollider *ca=&w->colliders[a],*cb=&w->colliders[b];
    if(ca->d.shape==PHYSICS_BOX&&cb->d.shape==PHYSICS_BOX)return pbox_contacts(w,a,b,e);
    if(ca->d.shape==PHYSICS_SPHERE&&cb->d.shape==PHYSICS_SPHERE) {
        PhysicsVec d=psub(cb->pose.position,ca->pose.position);double length=plen(d),depth=ca->d.radius+cb->d.radius-length;
        if(depth<0)return true;PhysicsVec n=length>0?pmul(d,1/length):pv(1,0,0);
        return pcontact(w,a,b,padd(ca->pose.position,pmul(n,ca->d.radius-depth/2)),n,depth,e);
    }
    bool flip=ca->d.shape==PHYSICS_BOX;PCollider *box=flip?ca:cb,*sphere=flip?cb:ca;
    PhysicsVec local=prot(pqconj(box->pose.rotation),psub(sphere->pose.position,box->pose.position)),h=pmul(box->d.size,0.5);
    PhysicsVec closest=pv(fmax(-h.x,fmin(h.x,local.x)),fmax(-h.y,fmin(h.y,local.y)),fmax(-h.z,fmin(h.z,local.z)));
    PhysicsVec diff=psub(local,closest);double length=plen(diff),depth=sphere->d.radius-length;PhysicsVec n;
    if(length>0){if(depth<0)return true;n=pmul(diff,1/length);}
    else {unsigned axis=0;double margin=h.x-fabs(local.x);for(unsigned i=1;i<3;i++){double d=paxis(h,i)-fabs(paxis(local,i));if(d<margin){margin=d;axis=i;}}
        n=pmul(pbase(axis),paxis(local,axis)<0?-1:1);closest=padd(local,pmul(n,margin));depth=sphere->d.radius+margin;}
    n=prot(box->pose.rotation,n);PhysicsVec point=padd(box->pose.position,prot(box->pose.rotation,closest));
    return pcontact(w,a,b,point,pmul(n,flip?1:-1),depth,e);
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
    for(size_t i=0;i<w->d.collider_count;i++)for(size_t j=i+1;j<w->d.collider_count&&w->proxies[j].bounds.lo.x<=w->proxies[i].bounds.hi.x;j++) {
        size_t a=w->proxies[i].collider,b=w->proxies[j].collider;if(a>b){size_t t=a;a=b;b=t;}
        PCollider *ca=&w->colliders[a],*cb=&w->colliders[b];
        if(ca->body==cb->body||!(ca->d.membership&cb->d.filter)||!(cb->d.membership&ca->d.filter)||!poverlap(ca->bounds,cb->bounds))continue;
        if(w->bodies[ca->body].d.motion==PHYSICS_FIXED&&w->bodies[cb->body].d.motion==PHYSICS_FIXED&&!ca->d.sensor&&!cb->d.sensor)continue;
        w->candidates++;size_t before=w->contact_count;
        if(!pcollide(w,a,b,e))return false;
        if(w->contact_count>before) {
            if(w->pair_count==w->d.contact_limit)return pfail(w,e,"physics pair quota exceeded",0);
            PPair pair={a,b,ca->d.sensor||cb->d.sensor};w->pairs[w->pair_count++]=pair;
            if(pair.sensor)w->contact_count=before;
        }
    }
    qsort(w->pairs,w->pair_count,sizeof(PPair),ppair_compare);
    qsort(w->contacts,w->contact_count,sizeof(PContact),pcontact_compare);
    size_t i=0,j=0;
    while(i<w->pair_count||j<w->old_pair_count) {
        int order=i==w->pair_count?1:j==w->old_pair_count?-1:ppair_compare(&w->pairs[i],&w->old_pairs[j]);
        if(order<0){if(!pevent(w,w->pairs[i++],true,e))return false;}
        else if(order>0){if(!pevent(w,w->old_pairs[j++],false,e))return false;}else{i++;j++;}
    }
    return true;
}
static PhysicsVec pvelocity(PBody* b,PhysicsVec r) {return padd(b->d.velocity,pcross(b->d.angular_velocity,r));}
static double peffective(PBody* a,PBody* b,PhysicsVec ra,PhysicsVec rb,PhysicsVec n) {
    return a->inverse_mass+b->inverse_mass+pdot(n,padd(pcross(pinverse(a,pcross(ra,n)),ra),pcross(pinverse(b,pcross(rb,n)),rb)));
}
static void psolve(PhysicsWorld* w) {
    for(size_t i=0;i<w->contact_count;i++) {
        PContact* c=&w->contacts[i];PCollider *ca=&w->colliders[c->a],*cb=&w->colliders[c->b];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
        if((!a->sleeping&&a->d.motion!=PHYSICS_FIXED)||(!b->sleeping&&b->d.motion!=PHYSICS_FIXED)) {
            if(a->sleeping)pwake(a);if(b->sleeping)pwake(b);
        }
        PhysicsVec ra=psub(c->point,pcentre(a)),rb=psub(c->point,pcentre(b));
        double vn=pdot(psub(pvelocity(b,rb),pvelocity(a,ra)),c->normal);
        c->bounce=vn< -w->d.length_scale? -fmax(ca->d.restitution,cb->d.restitution)*vn:0;
        double nearest=0.01*w->d.length_scale;nearest*=nearest;
        // previous contacts are grouped by collider pair; warm starting only searches that manifold.
        size_t lo=0,hi=w->previous_count;
        while(lo<hi){size_t mid=lo+(hi-lo)/2;PContact* old=&w->previous[mid];
            if(old->a<c->a||(old->a==c->a&&old->b<c->b))lo=mid+1;else hi=mid;}
        for(size_t j=lo;j<w->previous_count&&w->previous[j].a==c->a&&w->previous[j].b==c->b;j++) {
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
    for(unsigned iteration=0;iteration<w->d.iterations;iteration++)for(size_t i=0;i<w->contact_count;i++) {
        PContact* c=&w->contacts[i];PCollider *ca=&w->colliders[c->a],*cb=&w->colliders[c->b];PBody *a=&w->bodies[ca->body],*b=&w->bodies[cb->body];
        if((a->sleeping||a->d.motion==PHYSICS_FIXED)&&(b->sleeping||b->d.motion==PHYSICS_FIXED))continue;
        PhysicsVec ra=psub(c->point,pcentre(a)),rb=psub(c->point,pcentre(b)),relative=psub(pvelocity(b,rb),pvelocity(a,ra));
        double effective=peffective(a,b,ra,rb,c->normal);if(effective<=0)continue;
        double old=c->impulse;c->impulse=fmax(0,old+(c->bounce-pdot(relative,c->normal))/effective);
        PhysicsVec impulse=pmul(c->normal,c->impulse-old);pimpulse(a,pmul(impulse,-1),ra);pimpulse(b,impulse,rb);
        relative=psub(pvelocity(b,rb),pvelocity(a,ra));PhysicsVec tangent=psub(relative,pmul(c->normal,pdot(relative,c->normal)));
        double speed=plen(tangent);if(speed>1e-12) {
            tangent=pmul(tangent,1/speed);double denom=peffective(a,b,ra,rb,tangent);
            PhysicsVec next=psub(c->friction_impulse,pmul(tangent,speed/denom));
            double limit=(sqrt(ca->d.friction)*sqrt(cb->d.friction))*c->impulse,length=plen(next);if(length>limit)next=pmul(next,limit/length);
            impulse=psub(next,c->friction_impulse);c->friction_impulse=next;pimpulse(a,pmul(impulse,-1),ra);pimpulse(b,impulse,rb);
        }
    }
    // positional correction is separate from velocity impulses, so resting contacts add no kinetic energy.
    for(size_t i=0;i<w->contact_count;i++) {
        PContact* c=&w->contacts[i];PBody *a=&w->bodies[w->colliders[c->a].body],*b=&w->bodies[w->colliders[c->b].body];
        double inverse=a->inverse_mass+b->inverse_mass;if(inverse<=0||(a->sleeping&&b->sleeping))continue;
        PhysicsVec correction=pmul(c->normal,0.15*fmax(0,c->depth-0.001*w->d.length_scale)/inverse);
        ptranslate(a,pmul(correction,-a->inverse_mass));ptranslate(b,pmul(correction,b->inverse_mass));
    }
}

bool physics_apply(PhysicsWorld* w,const PhysicsCommand* commands,size_t count,PhysicsError* e) {
    if(!w||w->faulted||count>65536||(count&&!commands))return pfail(w,e,"invalid or faulted physics world",0);
    for(size_t i=0;i<count;i++) {
        PhysicsCommand c=commands[i];size_t index=pbody(w,c.body);
        if((unsigned)c.kind>PHYSICS_ANGULAR_VELOCITY||!pfinite(c.value)||(c.at_point&&!pfinite(c.point))||
            (c.kind!=PHYSICS_GRAVITY&&index==SIZE_MAX)||((c.kind==PHYSICS_TELEPORT||c.kind==PHYSICS_TARGET)&&!ppose(&c.pose))||
            (c.kind==PHYSICS_TARGET&&w->bodies[index].d.motion!=PHYSICS_KINEMATIC_POSITION))
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
        else if(c.kind==PHYSICS_TELEPORT){ppose(&c.pose);b->d.pose=c.pose;w->previous_count=0;}
        else if(c.kind==PHYSICS_TARGET){ppose(&c.pose);b->target=c.pose;b->target_set=true;}
    }
    if(!pupdate(w,e))return false;
    PhysicsState updated;return physics_read(w,&updated,e);
}
bool physics_configure(PhysicsWorld* w,const PhysicsBodyDesc* bodies,size_t body_count,
        const PhysicsColliderDesc* colliders,size_t collider_count,PhysicsError* e) {
    if(!w||w->faulted||body_count!=w->d.body_count||collider_count!=w->d.collider_count||(body_count&&!bodies)||(collider_count&&!colliders))
        return pfail(w,e,"invalid or faulted physics world",0);
    for(size_t i=0;i<body_count;i++) {
        PhysicsBodyDesc d=bodies[i];
        if(d.id!=w->bodies[i].d.id||d.motion!=w->bodies[i].d.motion||!pbody_valid(&d))return pfail(w,e,"physics configuration changes a body's identity or motion",d.id);
    }
    for(size_t i=0;i<collider_count;i++) {
        PhysicsColliderDesc d=colliders[i];
        if(d.id!=w->colliders[i].d.id||d.body!=w->colliders[i].d.body||!pcollider_valid(&d))return pfail(w,e,"physics configuration changes a collider's identity or owner",d.id);
    }
    // mass properties can still reject the edit, so the previous configuration is kept until all succeed
    PBody* bodies_before=mem_calloc(body_count+1,sizeof(PBody),MEM_CAT_EVAL);
    PCollider* colliders_before=mem_calloc(collider_count+1,sizeof(PCollider),MEM_CAT_EVAL);
    if(!bodies_before||!colliders_before){mem_free(bodies_before);mem_free(colliders_before);return pfail(w,e,"physics allocation failed",0);}
    memcpy(bodies_before,w->bodies,body_count*sizeof(PBody));memcpy(colliders_before,w->colliders,collider_count*sizeof(PCollider));
    for(size_t i=0;i<collider_count;i++){w->colliders[i].d=colliders[i];ppose(&w->colliders[i].d.pose);}
    bool valid=true;
    for(size_t i=0;i<body_count&&valid;i++) {
        PBody* b=&w->bodies[i];
        b->d.linear_damping=bodies[i].linear_damping;b->d.angular_damping=bodies[i].angular_damping;b->d.gravity_scale=bodies[i].gravity_scale;
        b->mass=b->inverse_mass=0;b->com=pv(0,0,0);memset(b->inertia,0,sizeof(b->inertia));memset(b->inverse,0,sizeof(b->inverse));
        valid=pmass(w,i,e);if(b->d.motion==PHYSICS_DYNAMIC)pwake(b);
    }
    if(valid)valid=pupdate(w,e);
    if(!valid) {
        memcpy(w->bodies,bodies_before,body_count*sizeof(PBody));memcpy(w->colliders,colliders_before,collider_count*sizeof(PCollider));
        w->faulted=false;pupdate(w,NULL);
    }
    mem_free(bodies_before);mem_free(colliders_before);
    if(!valid)return false;
    // changed shapes invalidate the warm-start cache
    w->previous_count=0;
    PhysicsState updated;return physics_read(w,&updated,e);
}
bool physics_read(PhysicsWorld* w,PhysicsState* out,PhysicsError* e) {
    if(!w||!out)return pfail(w,e,"invalid physics read",0);
    if(!w->faulted)for(size_t i=0;i<w->d.body_count;i++) {
        PBody* b=&w->bodies[i];PhysicsBodyState s={b->d.id,b->d.pose,b->d.velocity,b->d.angular_velocity,pcentre(b),b->mass,b->sleeping};w->states[i]=s;
    }
    PhysicsState state={w->tick,w->tick*w->d.timestep,w->states,w->d.body_count,w->events,w->event_count,w->candidates,w->contact_count};*out=state;return true;
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
            if(b->d.motion!=PHYSICS_DYNAMIC||b->sleeping)continue;
            b->d.velocity=pmul(padd(b->d.velocity,pmul(padd(pmul(w->d.gravity,b->d.gravity_scale),pmul(b->force,b->inverse_mass)),dt)),1/(1+b->d.linear_damping*dt));
            PhysicsVec local_w=prot(pqconj(b->d.pose.rotation),b->d.angular_velocity);
            PhysicsVec gyro=prot(b->d.pose.rotation,pcross(local_w,pmat(b->inertia,local_w)));
            b->d.angular_velocity=pmul(padd(b->d.angular_velocity,pmul(pinverse(b,psub(b->torque,gyro)),dt)),1/(1+b->d.angular_damping*dt));
            b->force=b->torque=pv(0,0,0);
        }
        if(!pdetect(w,e)){w->event_count=completed_events;w->faulted=true;return false;}psolve(w);
        for(size_t i=0;i<w->d.body_count;i++) {
            PBody* b=&w->bodies[i];if(b->d.motion==PHYSICS_FIXED||b->sleeping)continue;
            PhysicsVec centre=padd(pcentre(b),pmul(b->d.velocity,dt));
            b->d.pose.rotation=pintegrate(b->d.pose.rotation,b->d.angular_velocity,dt);
            b->d.pose.position=psub(centre,prot(b->d.pose.rotation,b->com));
            if(b->d.motion==PHYSICS_KINEMATIC_POSITION&&b->target_set){b->d.pose=b->target;b->target_set=false;}
            if(!pfinite(b->d.pose.position)||!pfinite(b->d.velocity)||!pfinite(b->d.angular_velocity)||!pqnorm(&b->d.pose.rotation)) {
                w->event_count=completed_events;w->faulted=true;return pfail(w,e,"physics numerical overflow",b->d.id);
            }
            if(b->d.motion==PHYSICS_DYNAMIC) {
                bool still=plen(b->d.velocity)<0.02*w->d.length_scale&&plen(b->d.angular_velocity)<0.02;
                b->idle=still?b->idle+dt:0;if(b->idle>0.5){b->sleeping=true;b->d.velocity=b->d.angular_velocity=pv(0,0,0);}
            }
        }
        // bounds and snapshots publish together; faulted reads retain the last completed body states.
        if(!pupdate(w,e)){w->event_count=completed_events;return false;}
        // only whole completed ticks become visible to readers and event consumers.
        w->tick++;memcpy(w->previous,w->contacts,w->contact_count*sizeof(PContact));w->previous_count=w->contact_count;
        memcpy(w->old_pairs,w->pairs,w->pair_count*sizeof(PPair));w->old_pair_count=w->pair_count;
        PhysicsState completed;physics_read(w,&completed,e);
    }
    return physics_read(w,out,e);
}
static bool pray(PCollider* c,PhysicsVec origin,PhysicsVec direction,double maximum,PhysicsHit* hit) {
    PhysicsQuat inverse=pqconj(c->pose.rotation);PhysicsVec o=prot(inverse,psub(origin,c->pose.position)),d=prot(inverse,direction),normal=pv(0,0,0);double t=0;
    if(c->d.shape==PHYSICS_SPHERE) {
        double b=pdot(o,d),value=pdot(o,o)-c->d.radius*c->d.radius,disc=b*b-value;if(disc<0)return false;
        if(value>0)t=-b-sqrt(disc);if(t<0||t>maximum)return false;normal=punit(padd(o,pmul(d,t)));
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
bool physics_query(PhysicsWorld* w,const PhysicsQuery* queries,size_t count,PhysicsHit* results,PhysicsError* e) {
    if(!w||w->faulted||count>65536||(count&&(!queries||!results)))return pfail(w,e,"invalid physics query",0);
    for(size_t i=0;i<count;i++)if((unsigned)queries[i].kind>PHYSICS_OVERLAP||!pfinite(queries[i].origin)||!pfinite(queries[i].direction)||!pfinite(queries[i].size)||
        !isfinite(queries[i].distance)||queries[i].distance<0||(queries[i].kind==PHYSICS_RAY&&plen(queries[i].direction)==0)||
        (queries[i].kind==PHYSICS_OVERLAP&&(queries[i].size.x<0||queries[i].size.y<0||queries[i].size.z<0)))return pfail(w,e,"invalid physics query parameters",0);
    if(!pupdate(w,e))return false;
    for(size_t i=0;i<count;i++) {
        PhysicsQuery q=queries[i];PhysicsHit result={0};double closest=q.distance;PhysicsVec direction=punit(q.direction);
        for(size_t j=0;j<w->d.collider_count;j++) {
            PCollider* c=&w->colliders[j];if((c->d.sensor&&!q.include_sensors)||!(q.membership&c->d.filter)||!(q.filter&c->d.membership))continue;
            PhysicsHit hit={0};bool found=false;
            if(q.kind==PHYSICS_RAY)found=pray(c,q.origin,direction,closest,&hit);
            else if(q.kind==PHYSICS_POINT) {
                PhysicsVec p=prot(pqconj(c->pose.rotation),psub(q.origin,c->pose.position)),h=pmul(c->d.size,0.5);
                found=c->d.shape==PHYSICS_SPHERE?plen(p)<=c->d.radius:fabs(p.x)<=h.x&&fabs(p.y)<=h.y&&fabs(p.z)<=h.z;
            } else {
                // overlap is explicitly an AABB broad-phase query; it does not claim exact shape overlap.
                PBox box={psub(q.origin,pmul(q.size,0.5)),padd(q.origin,pmul(q.size,0.5))};found=poverlap(box,c->bounds);
            }
            if(found){if(q.kind!=PHYSICS_RAY){hit.found=true;hit.collider=c->d.id;hit.body=c->d.body;hit.point=q.origin;}
                if(!result.found||hit.distance<closest||(hit.distance==closest&&hit.collider<result.collider)){result=hit;closest=hit.distance;}}
        }
        results[i]=result;
    }
    return true;
}
