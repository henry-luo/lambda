#pragma once

// Three.js r186 physical profile: Charlie/Neubelt sheen and its fitted directional albedo.
static const char* scene3d_physical_fragment = R"GLSL(
#ifdef PHYSICAL
uniform vec4 layer_params; // coat factor/roughness, sheen factor/roughness
uniform vec3 coat_normal_params; // map present, scale, authored tangent frame
uniform vec3 sheen_color;
uniform float dielectric_reflectance;
#ifdef TRANSMISSION
uniform vec4 transmission_params; // factor, IOR, thickness, attenuation distance (0 means infinity)
uniform vec3 attenuation_color,transmission_background;
uniform sampler2D opaque_color;
uniform mat4 transmission_projection;
uniform float transmission_captured;
in vec3 transmission_scale;
vec3 transmitted_radiance(vec2 uv,vec3 fallback,float roughness) {
    if(transmission_captured<0.5) return fallback;
    vec2 size=vec2(textureSize(opaque_color,0));
    vec2 radius=vec2(min(size.x,size.y)*0.025*roughness*roughness)/size;
    vec3 radiance=vec3(0.0);
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
        vec2 at=uv+vec2(x,y)*radius;
        if(any(lessThan(at,vec2(0.0)))||any(greaterThan(at,vec2(1.0)))) radiance+=fallback;
        else {vec4 sample_color=texture(opaque_color,at);radiance+=sample_color.rgb+fallback*(1.0-sample_color.a);}
    }
    return radiance/9.0;
}
#endif
float sheen_brdf(float roughness,float dotNH,float dotNV,float dotNL) {
    float invAlpha=1.0/(roughness*roughness);
    float sin2h=max(1.0-dotNH*dotNH,0.0078125);
    float distribution=(2.0+invAlpha)*pow(sin2h,invAlpha*0.5)*0.15915494309189535;
    float visibility=clamp(1.0/max(4.0*(dotNL+dotNV-dotNL*dotNV),1e-6),0.0,1.0);
    return distribution*visibility;
}
float sheen_albedo(float dotNV,float roughness) {
    float r2=roughness*roughness,rInv=1.0/(roughness+0.1);
    float a=-1.9362+1.0678*roughness+0.4573*r2-0.8469*rInv;
    float b=-0.6014+0.5538*roughness-0.4670*r2-0.1255*rInv;
    return clamp(exp(a*dotNV+b),0.0,1.0);
}
#endif
)GLSL";
