#pragma once

// bounded image kernels shared by every native postprocessing chain.
static const char* scene3d_post_header = R"GLSL(#version 330 core
uniform sampler2D color_input;
uniform vec2 resolution;
out vec4 fragment_color;
)GLSL";
static const char* scene3d_post_threshold = R"GLSL(
uniform float threshold;
void main() {
    vec3 color=texture(color_input,gl_FragCoord.xy/resolution).rgb;
    float luminance=dot(color,vec3(0.2126,0.7152,0.0722));
    fragment_color=vec4(color*max(luminance-threshold,0.0)/max(luminance,0.000001),1.0);
}
)GLSL";
static const char* scene3d_post_blur = R"GLSL(
uniform vec2 direction;
void main() {
    vec2 uv=gl_FragCoord.xy/resolution,step=direction/resolution;
    vec3 color=texture(color_input,uv).rgb*0.375;
    color+=(texture(color_input,uv-step).rgb+texture(color_input,uv+step).rgb)*0.25;
    color+=(texture(color_input,uv-2.0*step).rgb+texture(color_input,uv+2.0*step).rgb)*0.0625;
    fragment_color=vec4(color,1.0);
}
)GLSL";
static const char* scene3d_post_merge = R"GLSL(
uniform sampler2D glow_input;
uniform float strength;
void main() {
    vec2 uv=gl_FragCoord.xy/resolution;
    vec4 color=texture(color_input,uv);
    fragment_color=vec4(color.rgb+texture(glow_input,uv).rgb*strength,color.a);
}
)GLSL";
// directional FXAA: search the luminance edge and reject a sample that creates a new extremum.
static const char* scene3d_post_fxaa = R"GLSL(
float luminance(vec3 color) { return dot(color,vec3(0.299,0.587,0.114)); }
void main() {
    vec2 uv=gl_FragCoord.xy/resolution,pixel=1.0/resolution;
    vec4 center=texture(color_input,uv);
    float nw=luminance(texture(color_input,uv+vec2(-1,-1)*pixel).rgb);
    float ne=luminance(texture(color_input,uv+vec2(1,-1)*pixel).rgb);
    float sw=luminance(texture(color_input,uv+vec2(-1,1)*pixel).rgb);
    float se=luminance(texture(color_input,uv+vec2(1,1)*pixel).rgb),middle=luminance(center.rgb);
    float lo=min(middle,min(min(nw,ne),min(sw,se))),hi=max(middle,max(max(nw,ne),max(sw,se)));
    if (hi-lo<max(0.0312,hi*0.063)) { fragment_color=center;return; }
    vec2 direction=vec2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
    float reduction=max((nw+ne+sw+se)*0.03125,0.0078125);
    direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduction),vec2(-8),vec2(8))*pixel;
    vec4 a=(texture(color_input,uv-direction/6.0)+texture(color_input,uv+direction/6.0))*0.5;
    vec4 b=a*0.5+(texture(color_input,uv-direction*0.5)+texture(color_input,uv+direction*0.5))*0.25;
    float value=luminance(b.rgb);
    fragment_color=value<lo||value>hi?a:b;
}
)GLSL";
