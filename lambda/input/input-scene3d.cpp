#include "input-scene3d.hpp"
#include "input-parsers.h"
#include "../../lib/base64.h"
#include "../../lib/mime-detect.h"

namespace lambda {
const char* SceneAsset::absolute(const char* source, Url* base) {
    if (!source || !*source) { fail("empty dependency URI"); return nullptr; }
    if (is_data_uri(source)) return ctx.builder.createString(source)->chars;
    Url* resolved = url_parse_with_base(source, base ? base : (Url*)ctx.input()->url);
    if (!resolved) { fail("invalid dependency URI"); return nullptr; }
    const char* result = ctx.builder.createString(url_get_href(resolved))->chars;
    url_destroy(resolved); return result;
}
Input* SceneAsset::dependency(const char* source, const char* type, Url* base) {
    const char* resolved = absolute(source, base); if (!resolved) return nullptr;
    Input* result;
    if (is_data_uri(resolved)) {
        size_t length = 0; uint8_t* data = parse_data_uri(resolved, nullptr, 0, &length);
        if (!data) { fail("invalid data URI"); return nullptr; }
        result = input_from_source_n((const char*)data, length, nullptr,
            ctx.builder.createString(type), nullptr); mem_free(data);
    } else result = input_from_url(ctx.builder.createString(resolved), ctx.builder.createString(type), nullptr, nullptr);
    if (!result || result->parse_failed) { fail("dependency could not be loaded"); return nullptr; }
    return result;
}
Item SceneAsset::texture(const char* source, Url* base, Item* node) {
    const char* resolved = absolute(source,base); if (!resolved) return ItemNull;
    if(!dependency(resolved,"binary")) return ItemNull;
    Item value = element("texture"), key = id("texture",serial++);
    attr(value,"id",key); attr(value,"src",text(resolved)); append(resources,value); if(node) *node=value; return key;
}
Item SceneAsset::material(const char* color, const char* kind) {
    Item value = element("material"); attr(value,"id",id("material",serial++));
    attr(value,"type",symbol(kind)); attr(value,"color",text(color)); attr(value,"side",symbol("double"));
    append(resources,value); return value;
}
Item asset_color(SceneAsset& asset, const double* color) {
    char hex[8] = "#ffffff"; const char* digits = "0123456789abcdef";
    if(color[0]<0||color[0]>1||color[1]<0||color[1]>1||color[2]<0||color[2]>1)
        asset.ctx.addWarning("scene3d asset: diffuse color outside [0,1] is clipped by the native color representation");
    for (unsigned i=0;i<3;i++) {
        double linear=fmin(1,fmax(0,color[i]));
        double srgb=linear<=.0031308?12.92*linear:1.055*pow(linear,1.0/2.4)-.055;
        unsigned byte=(unsigned)round(srgb*255); hex[1+i*2]=digits[byte>>4]; hex[2+i*2]=digits[byte&15];
    }
    return asset.text(hex);
}
static double cross2(const double* a,const double* b,const double* c,unsigned x,unsigned y) {
    return (b[x]-a[x])*(c[y]-a[y])-(b[y]-a[y])*(c[x]-a[x]);
}
bool asset_triangulate(SceneAsset& asset,const double* p,size_t count,lam::ArrayList<unsigned>& triangles,double* out_normal) {
    if(count<3||count>4096) return asset.fail("polygon corner limit exceeded");
    double normal[3]={};
    for(size_t i=0;i<count;i++) for(unsigned a=0;a<3;a++) {
        unsigned b=(a+1)%3,c=(a+2)%3;const double* q=p+i*3;const double* r=p+((i+1)%count)*3;
        normal[a]+=(q[b]-r[b])*(q[c]+r[c]);
    }
    unsigned axis=0;for(unsigned a=1;a<3;a++) if(fabs(normal[a])>fabs(normal[axis])) axis=a;
    if(fabs(normal[axis])<1e-12) return asset.fail("degenerate polygon");
    if(out_normal) {double length=hypot(hypot(normal[0],normal[1]),normal[2]);for(unsigned i=0;i<3;i++) out_normal[i]=normal[i]/length;}
    unsigned x=(axis+1)%3,y=(axis+2)%3;double sign=normal[axis]>0?1:-1;
    lam::ArrayList<unsigned> remaining;
    for(unsigned i=0;i<count;i++) remaining.append(i);
    // ear clipping preserves winding and handles concave OBJ/A3D polygons.
    while(remaining.size()>3) {
        bool clipped=false;
        for(size_t k=0;k<remaining.size();k++) {
            unsigned a=remaining[(k+remaining.size()-1)%remaining.size()],b=remaining[k],c=remaining[(k+1)%remaining.size()];
            if(sign*cross2(p+a*3,p+b*3,p+c*3,x,y)<=1e-12) continue;
            bool inside=false;
            for(unsigned v:remaining) if(v!=a&&v!=b&&v!=c&&
                sign*cross2(p+a*3,p+b*3,p+v*3,x,y)>=-1e-12&&
                sign*cross2(p+b*3,p+c*3,p+v*3,x,y)>=-1e-12&&
                sign*cross2(p+c*3,p+a*3,p+v*3,x,y)>=-1e-12) {inside=true;break;}
            if(inside) continue;
            triangles.append(a);triangles.append(b);triangles.append(c);remaining.remove(k);clipped=true;break;
        }
        if(!clipped) return asset.fail("polygon is non-simple or has repeated/collinear corners");
    }
    for(unsigned i:remaining) triangles.append(i);
    return true;
}
struct WaveMaterial { const char* name; Item value; };
static bool wave_materials(SceneAsset& a,Item input,Url* base,lam::ArrayList<WaveMaterial>& materials,bool a3d=false) {
    ElementReader root(input);
    for(int64_t i=0;i<root.childCount();i++) {
        ElementReader source=root.childAt(i).asElement(); if(!source.hasTag("material")) continue;
        const char* name=source.get_attr("name").cstring();Item target=a.material();if(name) a.attr(target,"name",a.text(name));
        for(int64_t j=0;j<source.childCount();j++) {
            ElementReader prop=source.childAt(j).asElement();ItemReader value=prop.childAt(0);
            if(prop.hasTag("Kd")) {
                if(a3d) {
                    const char* argb=value.cstring(); if(!argb||strlen(argb)!=9) return a.fail("invalid A3D material color");
                    char rgb[8]="#ffffff";memcpy(rgb+1,argb+3,6);a.attr(target,"color",a.text(rgb));
                    char alpha[3]={argb[1],argb[2],0};a.attr(target,"opacity",a.number(strtol(alpha,nullptr,16)/255.0));
                } else {
                    if(!asset_is(prop.get_attr("space").cstring(),"rgb")) return a.fail("spectral/XYZ material colors require conversion");
                    if(asset_count(value)!=1&&asset_count(value)!=3) return a.fail("MTL diffuse color requires one or three RGB components");
                    double rgb[3];for(unsigned c=0;c<3;c++) rgb[c]=asset_component(value,c,asset_component(value,0));
                    a.attr(target,"color",asset_color(a,rgb));
                }
            } else if(prop.hasTag("d")||prop.hasTag("Tr")) {
                if(prop.get_attr("halo").asBool()) return a.fail("MTL halo dissolve is not supported");
                double opacity=asset_component(value,0);if(prop.hasTag("Tr")) opacity=1-opacity;
                if(!isfinite(opacity)||opacity<0||opacity>1) return a.fail("material opacity outside [0,1]");
                a.attr(target,"opacity",a.number(opacity));
            } else if(prop.hasTag("map_Kd")) {
                if(a3d) return a.fail("A3D texture names require an embedded-asset resolver");
                if(!prop.get_attr("unparsed").isNull()) return a.fail("unknown texture option");
                unsigned wrap=10497;auto options=prop.get_attr("options").asElement();
                for(int64_t k=0;k<options.childCount();k++) {
                    auto option=options.childAt(k).asElement();const char* flag=option.childAt(0).cstring();
                    if(!option.hasTag("clamp")||(!asset_is(flag,"on")&&!asset_is(flag,"off"))) return a.fail("texture option is not supported");
                    wrap=asset_is(flag,"on")?33071:10497;
                }
                const char* file=prop.get_attr("file").cstring();if(!file) return a.fail("missing diffuse texture file");
                Item texture=ItemNull;Item reference=a.texture(file,base,&texture);if(get_type_id(texture)==LMD_TYPE_NULL) return false;
                a.attr(target,"texture",reference);a.attr(texture,"wrap-s",a.number(wrap));a.attr(texture,"wrap-t",a.number(wrap));
            } else if(prop.hasTag("illum")) {
                double model=asset_component(value,0);a.attr(target,"type",a.symbol(model==0?"basic":"lambert"));
                if(model>1) a.ctx.addWarning("scene3d asset: specular illumination is approximated by Lambert shading");
            } else a.ctx.addWarning("scene3d asset: material property '%s' is not represented by Lambert shading",prop.tagName());
        }
        materials.append({name,target});
    }
    return !a.ctx.hasErrors();
}
static Item wave_material(SceneAsset& a,const char* name,lam::ArrayList<WaveMaterial>& materials) {
    if(name) for(size_t i=materials.size();i>0;i--) if(asset_is(materials[i-1].name,name)) return materials[i-1].value;
    if(name) {a.fail("unresolved material name");return ItemNull;}
    return a.material();
}
struct WaveCorner { unsigned v; int64_t vt,vn; };
static bool wave_index(SceneAsset& a,ItemReader value,size_t count,bool relative,int64_t* out) {
    if(!value.isInt()) return a.fail("missing or non-integer vertex reference");
    int64_t index=value.asInt();if(relative) index=index<0?(int64_t)count+index:index-1;
    if(index<0||(uint64_t)index>=count) return a.fail("vertex reference out of bounds");
    *out=index;return true;
}
struct WaveMesh {
    lam::ArrayList<double> positions,normals,uvs,skin,weights,colors;
    bool any_normals=false,any_uvs=false,any_colors=false;
};
static bool wave_face(SceneAsset& a,ElementReader face,bool relative,
    lam::ArrayList<Item>& vertices,lam::ArrayList<Item>& uv,lam::ArrayList<Item>& normals,WaveMesh& mesh,bool skinned=false,const int* remap=nullptr,size_t bone_count=0,bool smoothing=false) {
    lam::ArrayList<WaveCorner> corners;lam::ArrayList<double> polygon;lam::ArrayList<unsigned> triangles;
    for(int64_t i=0;i<face.childCount();i++) {
        MapReader c=face.childAt(i).asMap();int64_t v,vt=-1,vn=-1;
        if(!wave_index(a,c.get("v"),vertices.size(),relative,&v)||
            (!c.get("vt").isNull()&&!wave_index(a,c.get("vt"),uv.size(),relative,&vt))||
            (!c.get("vn").isNull()&&!wave_index(a,c.get("vn"),relative?normals.size():vertices.size(),relative,&vn))) return false;
        corners.append({(unsigned)v,vt,vn});ItemReader p=asset_read(vertices[v]);
        if(!relative) p=p.asElement().childAt(0);
        double w=asset_component(p,3,1);if(relative&&w==0) return a.fail("zero homogeneous vertex coordinate");
        for(unsigned c=0;c<3;c++) polygon.append(asset_component(p,c)/(relative?w:1));
    }
    double face_normal[3];
    if(!asset_triangulate(a,polygon.data(),corners.size(),triangles,face_normal)) return false;
    for(unsigned k:triangles) {
        auto c=corners[k];for(unsigned n=0;n<3;n++) mesh.positions.append(polygon[k*3+n]);
        ItemReader normal=c.vn>=0?asset_read(relative?normals[c.vn]:vertices[c.vn]):ItemReader();
        if(!relative&&c.vn>=0) normal=normal.asElement().childAt(0);
        if(smoothing&&c.vn<0) return a.fail("OBJ smoothing groups require authored normals");
        for(unsigned n=0;n<3;n++) mesh.normals.append(c.vn>=0?asset_component(normal,n):face_normal[n]);
        mesh.any_normals=true;
        for(unsigned n=0;n<2;n++) {
            double value=c.vt<0?0:asset_component(asset_read(uv[c.vt]),n,0);
            mesh.uvs.append(value);
        }
        mesh.any_uvs|=c.vt>=0;
        const char* color=relative?nullptr:ElementReader(vertices[c.v]).get_attr("color").cstring();
        if(color) {
            char alpha[3]={color[1],color[2],0};if(strtol(alpha,nullptr,16)!=255) return a.fail("A3D vertex alpha is not supported");
        }
        for(unsigned n=0;n<3;n++) {char byte[3]={color?color[3+n*2]:'f',color?color[4+n*2]:'f',0};mesh.colors.append(strtol(byte,nullptr,16)/255.0);}
        mesh.any_colors|=color!=nullptr;
        if(skinned) {
            auto influence=asset_read(vertices[c.v]).asElement().get_attr("weights");size_t count=asset_count(influence);
            if(count>4) return a.fail("more than four bone influences per vertex");
            if(!count) return a.fail("skinned vertex has no bone influences");
            double total=0;for(size_t n=0;n<count;n++) total+=asset_number(asset_field(asset_at(influence,n),"weight"));
            if(!(total>0)) return a.fail("vertex has zero total skin weight");
            for(unsigned n=0;n<4;n++) {
                double bone=n<count?asset_number(asset_field(asset_at(influence,n),"bone")):0;
                if(bone<0||bone>=bone_count||floor(bone)!=bone) return a.fail("A3D skin bone index out of bounds");
                mesh.skin.append(remap?remap[(size_t)bone]:bone);
                mesh.weights.append(n<count?asset_number(asset_field(asset_at(influence,n),"weight"))/total:0);
            }
        }
    }
    return true;
}
static void wave_flush(SceneAsset& a,WaveMesh& data,Item material,const char* label=nullptr,Item skeleton=ItemNull) {
    if(data.positions.empty()) return;
    Item mesh=a.element("mesh"),geometry=a.element("geometry");
    a.attr(mesh,"id",a.id("mesh",a.serial++));if(label) a.attr(mesh,"name",a.text(label));
    a.attr(mesh,"material",ElementReader(material).get_attr("id").item());
    a.attr(geometry,"type",a.symbol("buffer"));a.attr(geometry,"positions",a.array(data.positions.data(),data.positions.size()));
    if(data.any_normals) a.attr(geometry,"normals",a.array(data.normals.data(),data.normals.size()));
    if(data.any_colors) a.attr(geometry,"colors",a.array(data.colors.data(),data.colors.size()));
    if(data.any_uvs) a.attr(geometry,"uvs",a.array(data.uvs.data(),data.uvs.size()));
    if(!data.skin.empty()) {
        a.attr(geometry,"skin-indices",a.array(data.skin.data(),data.skin.size()));
        a.attr(geometry,"skin-weights",a.array(data.weights.data(),data.weights.size()));a.attr(mesh,"skeleton",skeleton);
    }
    a.append(mesh,geometry);a.append(a.root,mesh);data=WaveMesh();
}
bool asset_import_wavefront(SceneAsset& a,Item input) {
    ElementReader source(input);lam::ArrayList<WaveMaterial> materials;
    if(source.hasTag("mtl")) return wave_materials(a,input,(Url*)a.ctx.input()->url,materials);
    lam::ArrayList<Item> vertices,uv,normals;WaveMesh mesh;const char* material=nullptr;const char* label=nullptr;bool smoothing=false;
    Item default_material=a.material();
    // material libraries are declarations; their location in the OBJ does not limit usemtl.
    for(int64_t i=0;i<source.childCount();i++) {
        auto statement=source.childAt(i).asElement();
        if(statement.hasTag("mtllib")) {
            auto files=statement.childAt(0).asArray();for(int64_t j=0;j<files.length();j++) {
                Input* mtl=a.dependency(files.get(j).cstring(),"mtl");if(!mtl||!wave_materials(a,mtl->root,(Url*)mtl->url,materials)) return false;
            }
        }
    }
    for(int64_t i=0;i<source.childCount();i++) {
        ElementReader statement=source.childAt(i).asElement();const char* tag=statement.tagName();
        if(asset_is(tag,"v")) vertices.append(statement.childAt(0).item());
        else if(asset_is(tag,"vt")) uv.append(statement.childAt(0).item());
        else if(asset_is(tag,"vn")) normals.append(statement.childAt(0).item());
        else if(asset_is(tag,"f")) {if(!wave_face(a,statement,true,vertices,uv,normals,mesh,false,nullptr,0,smoothing)) return false;}
        else if(asset_is(tag,"mtllib")) {} else if(asset_is(tag,"usemtl")||asset_is(tag,"o")||asset_is(tag,"g")) {
            wave_flush(a,mesh,material?wave_material(a,material,materials):default_material,label);
            if(asset_is(tag,"usemtl")) material=statement.childAt(0).cstring();
            else label=asset_is(tag,"o")?statement.childAt(0).cstring():statement.childAt(0).asArray().get(0).cstring();
        } else if(asset_is(tag,"s")) {
            auto mode=statement.childAt(0).asArray().get(0);
            smoothing=!asset_is(mode.cstring(),"off")&&asset_number(mode,1)!=0;
        } else return a.fail("OBJ statement is not renderable polygon geometry");
        if(vertices.size()>262144||mesh.positions.size()>786432) return a.fail("geometry vertex quota exceeded");
    }
    wave_flush(a,mesh,material?wave_material(a,material,materials):default_material,label);
    return !a.ctx.hasErrors();
}
static bool a3d_pose(SceneAsset& a,lam::ArrayList<Item>& vertices,ItemReader record,double position[3],double quaternion[4]) {
    int64_t pi=asset_field(record,"position").asInt(),qi=asset_field(record,"orientation").asInt();
    if(pi<0||qi<0||(uint64_t)pi>=vertices.size()||(uint64_t)qi>=vertices.size()) return a.fail("A3D bone pose vertex out of bounds");
    auto p=ElementReader(vertices[pi]).childAt(0),q=ElementReader(vertices[qi]).childAt(0);
    for(unsigned c=0;c<3;c++) position[c]=asset_component(p,c);
    double length=0;for(unsigned c=0;c<4;c++) {quaternion[c]=asset_component(q,c);length+=quaternion[c]*quaternion[c];}
    if(!isfinite(length)||length<1e-20) return a.fail("A3D bone has a zero quaternion");
    for(unsigned c=0;c<4;c++) quaternion[c]/=sqrt(length);return true;
}
bool asset_import_a3d(SceneAsset& a,Item input) {
    ElementReader source(input);lam::ArrayList<Item> vertices,uv,normals,bones;lam::ArrayList<WaveMaterial> materials;
    if(!wave_materials(a,input,(Url*)a.ctx.input()->url,materials,true)) return false;
    ElementReader vertex=source.findChildElement("vertices"),textmap=source.findChildElement("textmap"),rig=source.findChildElement("bones");
    for(int64_t i=0;i<vertex.childCount();i++) vertices.append(vertex.childAt(i).item());
    for(int64_t i=0;i<textmap.childCount();i++) uv.append(textmap.childAt(i).item());
    if(vertices.size()>262144||rig.childCount()>16) return a.fail("A3D vertex/bone quota exceeded");
    double scale=asset_number(source.get_attr("scale"),1);if(!isfinite(scale)||scale<=0) return a.fail("A3D scale must be positive");
    double scales[3]={scale,scale,scale};a.attr(a.root,"scale",a.array(scales,3));
    Item skeleton=a.element("skeleton"),skeleton_id=a.id("skeleton",0);a.attr(skeleton,"id",skeleton_id);
    double initial_positions[16][3]={},initial_quaternions[16][4]={};
    for(int64_t i=0;i<rig.childCount();i++) {
        auto bone=rig.childAt(i).asElement();Item out=a.element("bone");a.attr(out,"id",a.id("bone",i));
        MapBuilder pose=a.ctx.builder.map();pose.put("position",bone.get_attr("position").item()).put("orientation",bone.get_attr("orientation").item());
        if(!a3d_pose(a,vertices,asset_read(pose.final()),initial_positions[i],initial_quaternions[i])) return false;
        a.attr(out,"position",a.array(initial_positions[i],3));a.attr(out,"quaternion",a.array(initial_quaternions[i],4));
        if(bone.get_attr("name").isString()) a.attr(out,"name",a.text(bone.get_attr("name").cstring()));
        auto parent=bone.get_attr("parent");int64_t index=parent.isNull()?-1:parent.asInt();
        if(index>=i||index< -1) return a.fail("invalid A3D bone parent");
        a.append(index<0?skeleton:bones[index],out);bones.append(out);
    }
    if(!bones.empty()) a.append(a.root,skeleton);
    // A3D indices follow file order; the renderer addresses the skeleton in preorder.
    lam::ArrayList<int> order;
    auto visit=[&](auto&& self,Item parent)->void {
        ElementReader node(parent);for(int64_t i=0;i<node.childCount();i++) {
            Item bone=node.childAt(i).item();for(size_t j=0;j<bones.size();j++) if(bones[j].element==bone.element) order.append(j);
            self(self,bone);
        }
    };visit(visit,skeleton);
    int remap[16]={};for(size_t i=0;i<order.size();i++) remap[order[i]]=i;
    unsigned action_index=0;Item default_material=a.material();
    for(int64_t i=0;i<source.childCount();i++) {
        auto chunk=source.childAt(i).asElement();
        if(chunk.hasTag("mesh")) {
            WaveMesh mesh;const char* material=nullptr;
            for(int64_t j=0;j<chunk.childCount();j++) {
                auto record=chunk.childAt(j).asElement();
                if(record.hasTag("use")) {
                    wave_flush(a,mesh,material?wave_material(a,material,materials):default_material,chunk.get_attr("name").cstring(),skeleton_id);
                    material=record.childAt(0).cstring();
                } else if(record.hasTag("face")) {
                    for(int64_t k=0;k<record.childCount();k++) if(!record.childAt(k).asMap().get("maximum").isNull()) return a.fail("A3D maximum vertices are not supported");
                    if(!wave_face(a,record,false,vertices,uv,normals,mesh,!bones.empty(),remap,bones.size())) return false;
                } else return a.fail("A3D parametric mesh records are not supported");
            }
            wave_flush(a,mesh,material?wave_material(a,material,materials):default_material,chunk.get_attr("name").cstring(),skeleton_id);
        } else if(chunk.hasTag("action")) {
            if(bones.empty()||!chunk.childCount()) return a.fail("A3D action requires a skeleton and frames");
            double duration=asset_number(chunk.get_attr("duration_ms"))/1000;if(!isfinite(duration)||duration<=0) return a.fail("A3D action duration must be positive");
            Item clip=a.element("animation-clip");a.attr(clip,"id",a.id("clip",action_index++));a.attr(clip,"label",a.text(chunk.get_attr("name").cstring()));a.attr(clip,"duration",a.number(duration));
            lam::ArrayList<double> times,poses,rotations;double positions[16][3],quaternions[16][4];
            memcpy(positions,initial_positions,sizeof(positions));memcpy(quaternions,initial_quaternions,sizeof(quaternions));
            auto snapshot=[&](double time) {
                times.append(time);for(size_t b=0;b<bones.size();b++) {for(double v:positions[b]) poses.append(v);for(double v:quaternions[b]) rotations.append(v);}
            };
            if(asset_number(chunk.childAt(0).asElement().get_attr("time_ms"))>0) snapshot(0);
            for(int64_t j=0;j<chunk.childCount();j++) {
                auto frame=chunk.childAt(j).asElement();double time=asset_number(frame.get_attr("time_ms"))/1000;
                if(!isfinite(time)||time<0||time>duration||(!times.empty()&&time<=times.back())) return a.fail("A3D frame times must increase within the action duration");
                for(int64_t k=0;k<frame.childCount();k++) {
                    auto pose=frame.childAt(k);int64_t b=asset_field(pose,"bone").asInt();
                    if(b<0||(uint64_t)b>=bones.size()||!a3d_pose(a,vertices,pose,positions[b],quaternions[b])) return a.fail("invalid A3D action bone pose");
                }snapshot(time);
            }
            if(times.back()<duration) snapshot(duration);
            for(unsigned b=0;b<bones.size();b++) for(unsigned property=0;property<2;property++) {
                unsigned components=property?4:3;lam::ArrayList<double> values;auto& source_values=property?rotations:poses;
                for(size_t k=0;k<times.size();k++) for(unsigned c=0;c<components;c++) values.append(source_values[(k*bones.size()+b)*components+c]);
                Item track=a.element("keyframe-track");char path[80];snprintf(path,sizeof(path),"asset-bone-%u.%s",b,property?"quaternion":"position");
                a.attr(track,"path",a.text(path));a.attr(track,"type",a.symbol(property?"quaternion":"vector"));a.attr(track,"interpolation",a.symbol("linear"));
                a.attr(track,"times",a.array(times.data(),times.size()));a.attr(track,"values",a.array(values.data(),values.size()));a.append(clip,track);
            }a.append(a.root,clip);
        } else if(chunk.hasTag("shape")||chunk.hasTag("voxel")||chunk.hasTag("procedural")||chunk.hasTag("chunk")) return a.fail("A3D shapes, voxels, procedural or unknown chunks are not supported");
    }
    return !a.ctx.hasErrors();
}

}
void input_scene3d_asset(Input* input,const char* source,size_t length,const char* flavor) {
    using namespace lambda;
    SceneAsset asset(input);const char* type=flavor;
    if(!type||!strcmp(type,"auto")) {
        Url* url=(Url*)input->url;const char* path=url&&url->pathname?url->pathname->chars:"";
        MimeDetector* detector=mime_detector_init();const char* mime=detect_mime_type(detector,path,source,length);
        type=asset_is(mime,"model/obj")?"obj":asset_is(mime,"model/mtl")?"mtl":
            asset_is(mime,"model/gltf+json")?"gltf":asset_is(mime,"text/x-3d-model")?"a3d":"unknown";
        mime_detector_destroy(detector);
    }
    if(!input_parse_model(input,source,length,type)) asset.fail("expected .obj, .mtl, .gltf or .a3d (or an explicit format)");
    else if(!input->parse_failed) {
        Item raw=input->root;
        bool ok=!strcmp(type,"gltf")?asset_import_gltf(asset,raw):
            !strcmp(type,"a3d")?asset_import_a3d(asset,raw):asset_import_wavefront(asset,raw);
        if(!ok&&!asset.ctx.hasErrors()) asset.fail("invalid asset structure");
    }
    if(input->parse_failed&&!asset.ctx.hasErrors()) asset.ctx.addError("scene3d asset: source model parsing failed: %s",
        input->parse_error_message?input->parse_error_message:"invalid syntax");
    asset.finish();
}
