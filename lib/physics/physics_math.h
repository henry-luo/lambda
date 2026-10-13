#ifndef LAMBDA_PHYSICS_MATH_H
#define LAMBDA_PHYSICS_MATH_H
#include "physics.h"
#include <math.h>

static inline PhysicsVec pv(double x,double y,double z) { PhysicsVec v={x,y,z};return v; }
static inline PhysicsVec padd(PhysicsVec a,PhysicsVec b) { return pv(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline PhysicsVec psub(PhysicsVec a,PhysicsVec b) { return pv(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline PhysicsVec pmul(PhysicsVec a,double s) { return pv(a.x*s,a.y*s,a.z*s); }
static inline double pdot(PhysicsVec a,PhysicsVec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline PhysicsVec pcross(PhysicsVec a,PhysicsVec b) { return pv(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
static inline double plen(PhysicsVec a) { return hypot(hypot(a.x,a.y),a.z); }
static inline bool pfinite(PhysicsVec a) { return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z); }
static inline PhysicsVec punit(PhysicsVec a) { double n=plen(a);return n>0?pmul(a,1/n):pv(0,0,0); }
static inline double paxis(PhysicsVec a,unsigned i) { return i==0?a.x:i==1?a.y:a.z; }
static inline PhysicsVec pbase(unsigned i) { return pv(i==0,i==1,i==2); }
static inline PhysicsQuat pqmul(PhysicsQuat a,PhysicsQuat b) {
    PhysicsQuat q={a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};return q;
}
static inline PhysicsQuat pqconj(PhysicsQuat q) { PhysicsQuat r={-q.x,-q.y,-q.z,q.w};return r; }
static inline bool pqnorm(PhysicsQuat* q) {
    double n=hypot(hypot(q->x,q->y),hypot(q->z,q->w));
    if(!isfinite(n)||n==0)return false;
    q->x/=n;q->y/=n;q->z/=n;q->w/=n;return true;
}
static inline PhysicsVec prot(PhysicsQuat q,PhysicsVec v) {
    PhysicsVec u=pv(q.x,q.y,q.z),t=pmul(pcross(u,v),2);
    return padd(v,padd(pmul(t,q.w),pcross(u,t)));
}
static inline PhysicsPose pcompose(PhysicsPose a,PhysicsPose b) {
    PhysicsPose p={padd(a.position,prot(a.rotation,b.position)),pqmul(a.rotation,b.rotation)};return p;
}
static inline PhysicsQuat peuler(PhysicsVec e) {
    double cx=cos(e.x/2),sx=sin(e.x/2),cy=cos(e.y/2),sy=sin(e.y/2),cz=cos(e.z/2),sz=sin(e.z/2);
    PhysicsQuat q={sx*cy*cz+cx*sy*sz,cx*sy*cz-sx*cy*sz,cx*cy*sz+sx*sy*cz,cx*cy*cz-sx*sy*sz};return q;
}
static inline PhysicsQuat pintegrate(PhysicsQuat q,PhysicsVec w,double dt) {
    double speed=plen(w),angle=speed*dt/2;
    double factor=speed>1e-12?sin(angle)/speed:dt/2;
    PhysicsQuat r={w.x*factor,w.y*factor,w.z*factor,cos(angle)};
    q=pqmul(r,q);pqnorm(&q);return q;
}
#endif
