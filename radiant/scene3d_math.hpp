#pragma once
#include <math.h>

// column-major matrices match desktop GLSL; scene coordinates are right-handed, Y-up.
struct Scene3dVec { float x, y, z; };
struct Scene3dMatrix { float v[16]; };
inline Scene3dVec scene3d_sub(Scene3dVec a, Scene3dVec b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline float scene3d_dot(Scene3dVec a, Scene3dVec b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Scene3dVec scene3d_cross(Scene3dVec a, Scene3dVec b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
inline bool scene3d_normalize(Scene3dVec* v) {
    float length = sqrtf(scene3d_dot(*v, *v));
    if (!isfinite(length) || length <= 1e-10f) return false;
    v->x /= length; v->y /= length; v->z /= length; return true;
}
inline Scene3dMatrix scene3d_identity() {
    Scene3dMatrix m = {}; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1; return m;
}
inline Scene3dMatrix scene3d_multiply(const Scene3dMatrix& a, const Scene3dMatrix& b) {
    Scene3dMatrix m = {};
    for (unsigned c = 0; c < 4; c++) for (unsigned r = 0; r < 4; r++)
        for (unsigned k = 0; k < 4; k++) m.v[c*4+r] += a.v[k*4+r] * b.v[c*4+k];
    return m;
}
inline bool scene3d_inverse_affine(const Scene3dMatrix& matrix,Scene3dMatrix* inverse) {
    if(matrix.v[3]!=0||matrix.v[7]!=0||matrix.v[11]!=0||matrix.v[15]!=1) return false;
    Scene3dVec columns[3];
    for(unsigned c=0;c<3;c++) columns[c]={matrix.v[c*4],matrix.v[c*4+1],matrix.v[c*4+2]};
    Scene3dVec cofactors[3]={scene3d_cross(columns[1],columns[2]),scene3d_cross(columns[2],columns[0]),scene3d_cross(columns[0],columns[1])};
    float determinant=scene3d_dot(columns[0],cofactors[0]);if(!isfinite(determinant)||fabsf(determinant)<1e-10f) return false;
    *inverse=scene3d_identity();
    for(unsigned r=0;r<3;r++) {
        inverse->v[r]=cofactors[r].x/determinant;inverse->v[4+r]=cofactors[r].y/determinant;inverse->v[8+r]=cofactors[r].z/determinant;
        inverse->v[12+r]=-(inverse->v[r]*matrix.v[12]+inverse->v[4+r]*matrix.v[13]+inverse->v[8+r]*matrix.v[14]);
    }
    for(float component:inverse->v) if(!isfinite(component)) return false;
    return true;
}
inline Scene3dVec scene3d_point(const Scene3dMatrix& m, Scene3dVec p, float w = 1) {
    return {m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z+m.v[12]*w,
        m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z+m.v[13]*w,
        m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z+m.v[14]*w};
}
inline Scene3dMatrix scene3d_transform(Scene3dVec position, Scene3dVec rotation, Scene3dVec scale) {
    float cx = cosf(rotation.x), sx = sinf(rotation.x), cy = cosf(rotation.y), sy = sinf(rotation.y);
    float cz = cosf(rotation.z), sz = sinf(rotation.z);
    // Euler XYZ: R = Rx * Ry * Rz, consistent with the selected scene vocabulary.
    Scene3dMatrix m = scene3d_identity();
    m.v[0]=cy*cz*scale.x; m.v[1]=(sx*sy*cz+cx*sz)*scale.x; m.v[2]=(-cx*sy*cz+sx*sz)*scale.x;
    m.v[4]=-cy*sz*scale.y; m.v[5]=(-sx*sy*sz+cx*cz)*scale.y; m.v[6]=(cx*sy*sz+sx*cz)*scale.y;
    m.v[8]=sy*scale.z; m.v[9]=-sx*cy*scale.z; m.v[10]=cx*cy*scale.z;
    m.v[12]=position.x; m.v[13]=position.y; m.v[14]=position.z; return m;
}
inline bool scene3d_camera(Scene3dVec eye, Scene3dVec target, Scene3dVec up, Scene3dMatrix* view) {
    Scene3dVec z = scene3d_sub(eye, target);
    if (!scene3d_normalize(&z)) return false;
    Scene3dVec x = scene3d_cross(up, z); if (!scene3d_normalize(&x)) return false;
    Scene3dVec y = scene3d_cross(z, x); *view = scene3d_identity();
    view->v[0]=x.x; view->v[4]=x.y; view->v[8]=x.z; view->v[12]=-scene3d_dot(x,eye);
    view->v[1]=y.x; view->v[5]=y.y; view->v[9]=y.z; view->v[13]=-scene3d_dot(y,eye);
    view->v[2]=z.x; view->v[6]=z.y; view->v[10]=z.z; view->v[14]=-scene3d_dot(z,eye);
    return true;
}
inline Scene3dMatrix scene3d_perspective(float fov, float aspect, float near_plane, float far_plane) {
    Scene3dMatrix m = {}; float f = 1 / tanf(fov * .00872664625997165f);
    m.v[0]=f/aspect; m.v[5]=f; m.v[10]=(far_plane+near_plane)/(near_plane-far_plane);
    m.v[11]=-1; m.v[14]=2*far_plane*near_plane/(near_plane-far_plane); return m;
}
