#pragma once

// retained center/endpoints expand in camera space; camera edits never rewrite vertex buffers.
static const char* scene3d_drawable_vertex = R"GLSL(
#if DRAWABLE_KIND > 0
uniform mat4 view;
uniform vec2 logical_viewport,drawable_size,drawable_center;
uniform float drawable_screen,drawable_source_uvs;
vec3 camera_right() {return vec3(view[0][0],view[1][0],view[2][0]);}
vec3 camera_up() {return vec3(view[0][1],view[1][1],view[2][1]);}
void drawable_hidden() {gl_Position=vec4(2.0,2.0,2.0,1.0);}
vec2 drawable_pixel(vec4 clip) {return clip.xy/clip.w*logical_viewport*0.5;}
vec2 drawable_perpendicular(vec2 direction) {return vec2(-direction.y,direction.x);}
float ribbon_radius(vec2 axis,vec4 clip,vec3 placed,mat4 world) {
    if(drawable_screen>0.5) return drawable_size.x*0.5;
    float scale=max(max(length(world[0].xyz),length(world[1].xyz)),length(world[2].xyz));
    float radius=drawable_size.x*scale*0.5;
    vec4 r=projection*vec4(placed+camera_right()*radius,1.0),u=projection*vec4(placed+camera_up()*radius,1.0);
    vec2 right=drawable_pixel(r)-drawable_pixel(clip),up=drawable_pixel(u)-drawable_pixel(clip);
    return length(vec2(dot(axis,right),dot(axis,up)));
}
void drawable_vertex(mat4 world) {
    vec4 placed=world*vec4(position,1.0);world_position=placed.xyz;
    world_normal=vec3(view[0][2],view[1][2],view[2][2]);world_tangent=world_bitangent=vec3(0.0);
    vertex_color=colored>0.5?color:vec4(1.0);texture_uv=uv;texture_uv1=uv1;
#if DRAWABLE_KIND == POINT_KIND || DRAWABLE_KIND == SPRITE_KIND
    vec4 clip=projection*placed;
    if(clip.w<=0.0) {drawable_hidden();return;}
    vec2 corner=vec2(normal.z==1.0||normal.z==2.0?1.0:0.0,normal.z>=2.0?1.0:0.0);
#if DRAWABLE_KIND == POINT_KIND
    point_coord=corner;
#endif
    vec2 offset=(corner-drawable_center)*normal.xy*drawable_size;
    if(drawable_screen>0.5) clip.xy+=offset*2.0/logical_viewport*clip.w;
    else {
        vec3 displacement=camera_right()*offset.x*length(world[0].xyz)+camera_up()*offset.y*length(world[1].xyz);
        placed.xyz+=displacement;clip=projection*placed;world_position=placed.xyz;
    }
    gl_Position=clip;
#else
    vec4 end=world*vec4(tangent.xyz,1.0),a=projection*placed,b=projection*end;
    float role=skin_indices.x;
    if(role<4.0) {
        float da=a.z+a.w,db=b.z+b.w;
        if(da<0.0&&db<0.0) {drawable_hidden();return;}
        float first=0.0,last=1.0;
        if(da<0.0) first=da/(da-db);
        if(db<0.0) last=da/(da-db);
        vec4 from=mix(a,b,first),to=mix(a,b,last);
        vec2 direction=drawable_pixel(to)-drawable_pixel(from);
        float magnitude=dot(direction,direction);
        if(magnitude<=0.0) {drawable_hidden();return;}
        vec2 axis=drawable_perpendicular(direction*inversesqrt(magnitude));
        // the quad index order starts on its left edge and keeps camera-facing CCW winding.
        float selected=role<2.0?first:last,side=1.0-mod(role,2.0)*2.0;
        vec4 clip=mix(a,b,selected);vec3 center=mix(placed.xyz,end.xyz,selected);
        float radius=ribbon_radius(axis,clip,center,world);
        clip.xy+=axis*side*radius*2.0/logical_viewport*clip.w;gl_Position=clip;
        vertex_color=colored>0.5?mix(color,skin_weights,selected):vec4(1.0);
        texture_uv=vec2(mix(normal.x,normal.y,selected),drawable_source_uvs>0.5?mix(normal.z,tangent.w,selected):side*0.5+0.5);
        texture_uv1=mix(uv1,skin_indices.yz,selected);world_position=center;
    } else {
        // bevels stay inside the two outer offset edges, including at a sharp turn.
        if(a.z+a.w<0.0) {drawable_hidden();return;}
        vec4 before=projection*world*vec4(normal,1.0);
        float da=a.z+a.w,dp=before.z+before.w,dn=b.z+b.w;
        if(dp<0.0) before=mix(a,before,da/(da-dp));
        if(dn<0.0) b=mix(a,b,da/(da-dn));
        vec2 prev=drawable_pixel(a)-drawable_pixel(before),next=drawable_pixel(b)-drawable_pixel(a);
        float lp=dot(prev,prev),ln=dot(next,next);
        if(lp<=0.0||ln<=0.0) {gl_Position=a;return;}
        prev*=inversesqrt(lp);next*=inversesqrt(ln);
        float turn=prev.x*next.y-prev.y*next.x;
        if(turn==0.0||role<4.5) {gl_Position=a;return;}
        // swap the two outer vertices on right turns so every generated triangle stays CCW.
        bool previous=turn>0.0?role<5.5:role>5.5;
        vec2 axis=drawable_perpendicular(previous?prev:next)*(-sign(turn));
        a.xy+=axis*ribbon_radius(axis,a,placed.xyz,world)*2.0/logical_viewport*a.w;gl_Position=a;
        if(drawable_source_uvs<0.5) texture_uv.y=turn>0.0?0.0:1.0;
    }
#endif
}
#endif
)GLSL";
