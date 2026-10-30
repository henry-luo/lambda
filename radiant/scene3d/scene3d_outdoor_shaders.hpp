#pragma once

// the same local bend runs before world placement in color, environment and shadow passes.
static const char* scene3d_outdoor_vertex = R"GLSL(
uniform vec4 wind_params; // displacement, reference height, spatial frequency, authored phase
uniform float wind_direction;
void outdoor_vertex(inout vec3 p,inout vec3 n,inout vec3 t,mat4 world) {
    if(wind_params.x<=0.0) return;
    vec2 direction=vec2(cos(wind_direction),sin(wind_direction));
    float phase=wind_params.w+dot(world[3].xz,vec2(0.73,0.41))*wind_params.z;
    float height=max(p.y,0.0)/wind_params.y;
    float offset=wind_params.x*sin(phase)*height*height;
    vec2 derivative=direction*(2.0*wind_params.x*sin(phase)*height/wind_params.y);
    p.xz+=direction*offset;
    // inverse-transpose Jacobian for normals, forward Jacobian for tangents.
    n.y-=dot(derivative,n.xz);t.xz+=derivative*t.y;
}
)GLSL";

static const char* scene3d_outdoor_fragment = R"GLSL(
uniform vec4 fog_params; // mode (0/off,1/linear,2/exp2,3/height), near, far, density
uniform vec2 fog_height; // base elevation and inverse-height falloff
uniform vec3 fog_color,fog_eye;
uniform vec4 water_params; // ripple height, spatial frequency, authored phase, direction
uniform vec2 foliage_params; // backlight factor, optical thickness
uniform vec3 foliage_color;
vec3 outdoor_fog(vec3 color,vec3 position) {
    if(fog_params.x<0.5) return color;
    float distance=length(position-fog_eye),amount;
    if(fog_params.x<1.5) amount=clamp((distance-fog_params.y)/(fog_params.z-fog_params.y),0.0,1.0);
    else if(fog_params.x<2.5) amount=1.0-exp(-pow(distance*fog_params.w,2.0));
    else {
        float a=clamp(-fog_height.y*(fog_eye.y-fog_height.x),-40.0,40.0);
        float b=clamp(-fog_height.y*(position.y-fog_height.x),-40.0,40.0);
        float mean=abs(b-a)<0.001?exp((a+b)*0.5):(exp(b)-exp(a))/(b-a);
        amount=1.0-exp(-distance*fog_params.w*mean);
    }
    return mix(color,fog_color,amount);
}
vec3 outdoor_normal(vec3 n,vec3 position) {
    if(water_params.x<=0.0) return n;
    vec2 direction=vec2(cos(water_params.w),sin(water_params.w));
    vec2 across=vec2(-direction.y,direction.x);
    float a=dot(position.xz,direction)*water_params.y-water_params.z;
    float b=dot(position.xz,across)*water_params.y*1.37-water_params.z*2.0;
    vec2 slope=water_params.x*water_params.y*(direction*cos(a)+across*0.35*cos(b));
    return normalize(n-(gl_FrontFacing?1.0:-1.0)*vec3(slope.x,0.0,slope.y));
}
)GLSL";
