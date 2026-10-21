#include "input-scene3d.hpp"
#include "input-parsers.h"
#include "../../lib/base64.h"

namespace lambda {
namespace {
struct GltfBuffer { const uint8_t* data; size_t length; };
struct GltfColumn { const char* name=nullptr; unsigned width=0; lam::ArrayList<double> values; };
struct GltfSkin {
    Item value;
    unsigned count = 0;
    int remap[4096];
};
struct Gltf {
    SceneAsset& a;
    ItemReader source;
    lam::ArrayList<GltfBuffer> buffers;
    lam::ArrayList<Item> materials, nodes;
    lam::ArrayList<int> parents;
    lam::ArrayList<unsigned char> visited;
    lam::ArrayList<GltfSkin> skins;
    const uint8_t* binary;
    size_t embedded_length;
    bool quantization=false;
    explicit Gltf(SceneAsset& asset,Item input,const uint8_t* data,size_t length):
        a(asset),source(asset_read(input)),binary(data),embedded_length(length) {}
    ItemReader field(const char* key) {return asset_field(source,key);}
    ItemReader object(const char* key,size_t i) {return asset_at(field(key),i);}
    bool fail(const char* reason) {return a.fail(reason);}
    int integer(ItemReader value,int fallback=-1) {
        if(value.isNull()) return fallback;
        double n=asset_number(value);
        if(!isfinite(n)||n<0||n>2147483647||floor(n)!=n) {fail("invalid nonnegative integer");return -1;}
        return (int)n;
    }
    int integer(ItemReader object,const char* key,int fallback=-1) {return integer(asset_field(object,key),fallback);}
    Item node_id(unsigned n) {return a.id("node",n);}
    Item bone_id(unsigned skin,unsigned n) {
        char id[80];snprintf(id,sizeof(id),"asset-rig-%u-node-%u",skin,n);return a.text(id);
    }
    bool load_buffers() {
        for(size_t i=0;i<asset_count(field("buffers"));i++) {
            auto buffer=object("buffers",i);int length=integer(buffer,"byteLength");
            const char* uri=asset_field(buffer,"uri").cstring();
            if(length<0||(size_t)length>SCENE_ASSET_BUFFER_LIMIT) return fail("buffer byteLength must be no greater than 64 MiB");
            if(!uri) {
                // GLB's first buffer refers to BIN; its final 0..3 alignment bytes are not buffer data.
                if(i!=0||!binary||(size_t)length>embedded_length||embedded_length-(size_t)length>3)
                    return fail("missing or incorrectly sized GLB binary buffer");
                buffers.append({binary,(size_t)length});continue;
            }
            Input* input=a.dependency(uri,"binary");if(!input) return false;
            Binary* binary=input->root.get_binary();size_t actual=binary?binary_length(binary):0;
            if(actual<(size_t)length) return fail("buffer is shorter than its declared byteLength");
            buffers.append({binary?binary_data(binary):nullptr,(size_t)length});
        }
        return true;
    }
    bool view(int index,size_t offset,size_t count,size_t width,size_t stride,const uint8_t** out) {
        if(index<0||(size_t)index>=asset_count(field("bufferViews"))) return fail("bufferView index out of bounds");
        auto v=object("bufferViews",index);int b=integer(v,"buffer"),start=integer(v,"byteOffset",0),length=integer(v,"byteLength");
        if(b<0||(size_t)b>=buffers.size()||start<0||length<0) return fail("invalid bufferView");
        auto buffer=buffers[b];
        if((size_t)start>buffer.length||(size_t)length>buffer.length-start||offset>(size_t)length||
            width>(size_t)length-offset||(count&&count-1>((size_t)length-offset-width)/stride)) return fail("accessor exceeds bufferView bounds");
        *out=buffer.data+start+offset;return true;
    }
    static double decode(const uint8_t* p,unsigned type,bool normalized) {
        unsigned width=type==5120||type==5121?1:type==5122||type==5123?2:4;uint32_t bits=0;
        for(unsigned i=0;i<width;i++) bits|=(uint32_t)p[i]<<(i*8);
        if(type==5126) {float n;memcpy(&n,&bits,4);return n;}
        double n=type==5120?(int8_t)bits:type==5122?(int16_t)bits:(double)bits;
        if(normalized) n=type==5120?fmax(n/127,-1):type==5122?fmax(n/32767,-1):type==5121?n/255:type==5123?n/65535:n/4294967295.0;
        return n;
    }
    bool accessor(int index,lam::ArrayList<double>& values,unsigned* components=nullptr,unsigned* component_type=nullptr) {
        if(index<0||(size_t)index>=asset_count(field("accessors"))) return fail("accessor index out of bounds");
        auto ac=object("accessors",index);const char* type=asset_field(ac,"type").cstring();
        unsigned c=asset_is(type,"SCALAR")?1:asset_is(type,"VEC2")?2:asset_is(type,"VEC3")?3:asset_is(type,"VEC4")?4:asset_is(type,"MAT4")?16:0;
        int ct=integer(ac,"componentType"),count=integer(ac,"count"),offset=integer(ac,"byteOffset",0),vi=integer(ac,"bufferView");
        unsigned width=ct==5120||ct==5121?1:ct==5122||ct==5123?2:ct==5125||ct==5126?4:0;
        auto norm=asset_field(ac,"normalized");bool normalized=norm.isBool()&&norm.asBool();
        if(!c||!width||count<=0||count>(c==1?8388608:1048576)||offset<0||offset%(int)width||
            (!norm.isNull()&&!norm.isBool())||(normalized&&(ct==5125||ct==5126))) return fail("invalid accessor layout or normalization");
        if((size_t)count*c>8388608) return fail("accessor component quota exceeded");
        unsigned stride=c*width;const uint8_t* bytes=nullptr;
        if(vi>=0) {
            auto v=object("bufferViews",vi);int declared=integer(v,"byteStride",stride);
            if(declared<(int)stride||declared>252||declared%(int)width) return fail("invalid accessor stride");
            stride=declared;if(!view(vi,offset,count,c*width,stride,&bytes)) return false;
        } else if(offset) return fail("accessor without bufferView has a byteOffset");
        values.clear();for(int i=0;i<count;i++) for(unsigned k=0;k<c;k++) {
            double n=bytes?decode(bytes+(size_t)i*stride+k*width,ct,normalized):0;
            if(!isfinite(n)) return fail("non-finite accessor component");values.append(n);
        }
        auto sparse=asset_field(ac,"sparse");
        if(!sparse.isNull()) {
            int sc=integer(sparse,"count");auto indices=asset_field(sparse,"indices"),data=asset_field(sparse,"values");
            int it=integer(indices,"componentType"),iw=it==5121?1:it==5123?2:it==5125?4:0;
            const uint8_t* ix;const uint8_t* val;
            int io=integer(indices,"byteOffset",0),vo=integer(data,"byteOffset",0);
            if(sc<=0||sc>count||!iw||io<0||vo<0||io%iw||vo%(int)width||
                !view(integer(indices,"bufferView"),io,sc,iw,iw,&ix)||
                !view(integer(data,"bufferView"),vo,sc,c*width,c*width,&val)) return fail("invalid sparse accessor");
            int64_t previous=-1;
            for(int i=0;i<sc;i++) {
                uint32_t target=(uint32_t)decode(ix+i*iw,it,false);
                if(target>=(unsigned)count||(int64_t)target<=previous) return fail("sparse indices must be increasing and in bounds");previous=target;
                for(unsigned k=0;k<c;k++) {
                    double n=decode(val+((size_t)i*c+k)*width,ct,normalized);
                    if(!isfinite(n)) return fail("non-finite sparse value");values[(size_t)target*c+k]=n;
                }
            }
        }
        if(components) *components=c;if(component_type) *component_type=ct;return true;
    }
    bool attribute_encoding(const char* key,unsigned ct,bool normalized,bool morph=false) {
        if(ct==5126) return true;
        bool signed_type=ct==5120||ct==5122,unsigned_type=ct==5121||ct==5123;
        if(asset_is(key,"TEXCOORD_0"))
            return !morph&&((unsigned_type&&normalized)||(quantization&&(signed_type||unsigned_type)));
        // KHR_mesh_quantization uses the existing accessor decoder and node/skin transforms.
        return quantization&&((asset_is(key,"POSITION")&&(signed_type||(!morph&&unsigned_type)))||
            (asset_is(key,"NORMAL")&&signed_type&&normalized));
    }
    bool components(ItemReader attributes,const char* key,unsigned expected,lam::ArrayList<double>& values,bool morph=false) {
        unsigned count,ct;if(!accessor(integer(attributes,key),values,&count,&ct)) return false;
        bool vertex=asset_is(key,"POSITION")||asset_is(key,"NORMAL");
        bool normalized=asset_field(object("accessors",integer(attributes,key)),"normalized").asBool();
        return (count==expected&&(vertex?attribute_encoding(key,ct,normalized,morph):ct==5126))||fail("invalid vector accessor encoding");
    }
    bool transform(Item out,ItemReader in) {
        const char* keys[]={"translation","rotation","scale","matrix"};const char* attrs[]={"position","quaternion","scale","matrix"};
        bool matrix=!asset_field(in,"matrix").isNull();
        for(unsigned k=0;k<4;k++) {
            auto value=asset_field(in,keys[k]);if(value.isNull()) continue;
            if(matrix&&k<3) return fail("node specifies both matrix and TRS");
            unsigned count=k==3?16:k==1?4:3;if(asset_count(value)!=count) return fail("invalid node transform component count");
            double data[16];for(unsigned c=0;c<count;c++) {data[c]=asset_component(value,c);if(!isfinite(data[c])) return fail("invalid node transform");}
            if(k==1&&hypot(hypot(data[0],data[1]),hypot(data[2],data[3]))<1e-10) return fail("zero node quaternion");
            if(k==2&&(!data[0]||!data[1]||!data[2])) return fail("singular node scale");
            if(k==3&&(data[3]!=0||data[7]!=0||data[11]!=0||data[15]!=1)) return fail("node matrix must be affine");
            a.attr(out,attrs[k],a.array(data,count));
        }
        auto name=asset_field(in,"name");if(name.isString()) a.attr(out,"name",a.text(name.cstring()));return true;
    }
    bool load_materials() {
        for(size_t i=0;i<asset_count(field("materials"));i++) {
            auto m=object("materials",i),pbr=asset_field(m,"pbrMetallicRoughness");
            bool unlit=!asset_field(asset_field(m,"extensions"),"KHR_materials_unlit").isNull();
            Item material=a.material("#ffffff",unlit?"basic":"lambert");
            if(asset_field(m,"name").isString()) a.attr(material,"name",a.text(asset_field(m,"name").cstring()));
            auto color=asset_field(pbr,"baseColorFactor");
            double rgba[4]={1,1,1,1};if(!color.isNull()) {
                if(asset_count(color)!=4) return fail("invalid baseColorFactor");
                for(unsigned c=0;c<4;c++) {rgba[c]=asset_component(color,c);if(!isfinite(rgba[c])||rgba[c]<0||rgba[c]>1) return fail("invalid base color component");}
            }
            a.attr(material,"color",asset_color(a,rgba));
            const char* alpha=asset_field(m,"alphaMode").cstring();
            if(alpha&&!asset_is(alpha,"OPAQUE")&&!asset_is(alpha,"BLEND")) return fail("alpha MASK materials are not supported");
            a.attr(material,"alpha-mode",a.symbol(asset_is(alpha,"BLEND")?"blend":"opaque"));
            a.attr(material,"opacity",a.number(asset_is(alpha,"BLEND")?rgba[3]:1));
            a.attr(material,"side",a.symbol(asset_field(m,"doubleSided").asBool()?"double":"front"));
            auto tex=asset_field(pbr,"baseColorTexture");
            if(!tex.isNull()) {
                if(integer(tex,"texCoord",0)!=0||!asset_field(tex,"extensions").isNull()) return fail("only untransformed TEXCOORD_0 textures are supported");
                int ti=integer(tex,"index");if(ti<0||(size_t)ti>=asset_count(field("textures"))) return fail("texture index out of bounds");
                auto texture=object("textures",ti);int image=integer(texture,"source");
                if(image<0||(size_t)image>=asset_count(field("images"))) return fail("image index out of bounds");
                int sampler=integer(texture,"sampler");
                if(sampler>=0&&(size_t)sampler>=asset_count(field("samplers"))) return fail("sampler index out of bounds");
                auto sampling=sampler<0?ItemReader():object("samplers",sampler);
                int wrap_s=integer(sampling,"wrapS",10497),wrap_t=integer(sampling,"wrapT",10497);
                int min_filter=integer(sampling,"minFilter",9987),mag_filter=integer(sampling,"magFilter",9729);
                if((wrap_s!=10497&&wrap_s!=33071&&wrap_s!=33648)||(wrap_t!=10497&&wrap_t!=33071&&wrap_t!=33648)||
                    (min_filter!=9728&&min_filter!=9729&&(min_filter<9984||min_filter>9987))||
                    (mag_filter!=9728&&mag_filter!=9729)) return fail("invalid texture sampler");
                auto im=object("images",image);const char* uri=asset_field(im,"uri").cstring();
                if(!uri) {
                    int vi=integer(im,"bufferView");auto v=object("bufferViews",vi);int length=integer(v,"byteLength");const uint8_t* bytes;
                    const char* mime=asset_field(im,"mimeType").cstring();
                    if(!mime||(!asset_is(mime,"image/png")&&!asset_is(mime,"image/jpeg"))||length<=0||!view(vi,0,1,length,length,&bytes)) return fail("invalid embedded image");
                    char* encoded=base64_encode_alloc(bytes,length,BASE64_STD);if(!encoded) return fail("image encoding allocation failed");
                    StrBuf* data=strbuf_new();strbuf_append_str(data,"data:");strbuf_append_str(data,mime);strbuf_append_str(data,";base64,");strbuf_append_str(data,encoded);
                    uri=a.ctx.builder.createString(data->str)->chars;strbuf_free(data);mem_free(encoded);
                }
                Item texture_node=ItemNull;Item texture_id=a.texture(uri,nullptr,&texture_node);if(get_type_id(texture_node)==LMD_TYPE_NULL) return false;
                a.attr(material,"texture",texture_id);
                a.attr(texture_node,"wrap-s",a.number(wrap_s));a.attr(texture_node,"wrap-t",a.number(wrap_t));
                a.attr(texture_node,"min-filter",a.number(min_filter));a.attr(texture_node,"mag-filter",a.number(mag_filter));
            }
            if(!unlit) a.ctx.addWarning("scene3d asset: glTF metallic/roughness shading is approximated by Lambert base color");
            if(!asset_field(m,"normalTexture").isNull()||!asset_field(m,"occlusionTexture").isNull()||!asset_field(m,"emissiveTexture").isNull()||
                !asset_field(pbr,"metallicRoughnessTexture").isNull()) a.ctx.addWarning("scene3d asset: secondary PBR texture maps are not rendered");
            materials.append(material);
        }
        materials.append(a.material());return true;
    }
    bool hierarchy() {
        size_t count=asset_count(field("nodes"));if(count>4096) return fail("node quota exceeded");
        for(size_t i=0;i<count;i++) {parents.append(-1);visited.append(0);nodes.append(ItemNull);}
        for(size_t i=0;i<count;i++) {
            auto children=asset_field(object("nodes",i),"children");
            for(size_t j=0;j<asset_count(children);j++) {
                int child=integer(asset_at(children,j));if(child<0||(size_t)child>=count||parents[child]!=-1||child==(int)i) return fail("node hierarchy is not a forest");parents[child]=i;
            }
        }
        for(size_t i=0;i<count;i++) {
            int cursor=i;size_t depth=0;while(cursor>=0) {if(++depth>64) return fail("node hierarchy contains a cycle or exceeds depth 64");cursor=parents[cursor];}
        }
        return true;
    }
    bool skin_bones(GltfSkin& skin,unsigned si,int node,Item parent,const lam::ArrayList<unsigned char>& needed,const lam::ArrayList<double>& inverse,const lam::ArrayList<int>& joints) {
        if(!needed[node]) return true;
        unsigned index=skin.count;if(index>=SCENE_ASSET_BONE_LIMIT) return fail("skin requires more than 256 bones including joint ancestors");
        skin.count++;skin.remap[node]=index;
        Item bone=a.element("bone");a.attr(bone,"id",bone_id(si,node));if(!transform(bone,object("nodes",node))) return false;
        for(size_t j=0;j<joints.size();j++) if(joints[j]==node) {
            double identity[16]={};identity[0]=identity[5]=identity[10]=identity[15]=1;
            a.attr(bone,"inverse-bind-matrix",a.array(inverse.empty()?identity:inverse.data()+j*16,16));break;
        }
        a.append(parent,bone);
        auto children=asset_field(object("nodes",node),"children");
        for(size_t j=0;j<asset_count(children);j++) if(!skin_bones(skin,si,integer(asset_at(children,j)),bone,needed,inverse,joints)) return false;
        return true;
    }
    bool load_skins() {
        if(asset_count(field("skins"))>16) return fail("skin quota exceeded");
        for(unsigned si=0;si<asset_count(field("skins"));si++) {
            auto source=object("skins",si);auto list=asset_field(source,"joints");size_t count=asset_count(list);
            if(!count||count>SCENE_ASSET_BONE_LIMIT) return fail("skin joint count must be 1..256");
            GltfSkin skin;skin.value=a.element("skeleton");a.attr(skin.value,"id",a.id("skeleton",si));
            lam::ArrayList<int> joints;lam::ArrayList<unsigned char> needed;
            for(size_t n=0;n<nodes.size();n++) {needed.append(0);skin.remap[n]=-1;}
            for(size_t j=0;j<count;j++) {
                int node=integer(asset_at(list,j));if(node<0||(size_t)node>=nodes.size()) return fail("skin joint index out of bounds");
                for(int other:joints) if(other==node) return fail("duplicate skin joint");joints.append(node);
                for(int ancestor=node;ancestor>=0;ancestor=parents[ancestor]) needed[ancestor]=1;
            }
            lam::ArrayList<double> inverse;int accessor_index=integer(source,"inverseBindMatrices");
            if(accessor_index>=0) {unsigned c;unsigned ct;if(!accessor(accessor_index,inverse,&c,&ct)||c!=16||ct!=5126||inverse.size()!=count*16) return fail("invalid inverse bind matrices");}
            for(size_t n=0;n<nodes.size();n++) if(parents[n]<0&&!skin_bones(skin,si,n,skin.value,needed,inverse,joints)) return false;
            a.append(a.root,skin.value);skins.append(static_cast<GltfSkin&&>(skin));
        }
        return true;
    }
    void geometry_columns(Item geometry,GltfColumn* columns,const lam::ArrayList<unsigned>* vertices=nullptr) {
        for(unsigned k=0;k<6;k++) if(columns[k].name) {
            auto& column=columns[k];
            if(!vertices) a.attr(geometry,column.name,a.array(column.values.data(),column.values.size()));
            else {
                auto array=a.ctx.builder.array();
                for(unsigned vertex:*vertices) for(unsigned axis=0;axis<column.width;axis++)
                    array.append(a.number(column.values[(size_t)vertex*column.width+axis]));
                a.attr(geometry,column.name,array.final());
            }
        }
    }
    bool face_normal(const lam::ArrayList<double>& points,unsigned p,unsigned q,unsigned r,double* normal) {
        double u[3],v[3];for(unsigned axis=0;axis<3;axis++) {u[axis]=points[q*3+axis]-points[p*3+axis];v[axis]=points[r*3+axis]-points[p*3+axis];}
        for(unsigned axis=0;axis<3;axis++) normal[axis]=u[(axis+1)%3]*v[(axis+2)%3]-u[(axis+2)%3]*v[(axis+1)%3];
        double length=hypot(hypot(normal[0],normal[1]),normal[2]);if(!length) return false;
        for(unsigned axis=0;axis<3;axis++) normal[axis]/=length;return true;
    }
    void static_chunks(Item parent,Item material,GltfColumn* columns,const lam::ArrayList<double>& triangles,size_t vertex_count,bool flat) {
        lam::ArrayList<int> remap;for(size_t i=0;i<vertex_count;i++) remap.append(-1);
        lam::ArrayList<unsigned> vertices;lam::ArrayList<double> indices;
        auto flush=[&]() {
            if(indices.empty()) return;
            Item mesh=a.element("mesh"),geometry=a.element("geometry");
            a.attr(mesh,"id",a.id("mesh",a.serial++));a.attr(mesh,"material",material);
            a.attr(geometry,"type",a.symbol("buffer"));geometry_columns(geometry,columns,&vertices);
            if(flat) {
                auto normals=a.ctx.builder.array();
                for(size_t i=0;i<vertices.size();i+=3) {
                    double normal[3];face_normal(columns[0].values,vertices[i],vertices[i+1],vertices[i+2],normal);
                    for(unsigned corner=0;corner<3;corner++) for(double component:normal) normals.append(a.number(component));
                }
                a.attr(geometry,"normals",normals.final());
            }
            a.attr(geometry,"indices",a.array(indices.data(),indices.size()));a.append(mesh,geometry);a.append(parent,mesh);
            for(unsigned vertex:vertices) remap[vertex]=-1;
            vertices.clear();indices.clear();
        };
        // preserve every triangle and UV seam while bounding each native draw's buffers.
        for(size_t i=0;i<triangles.size();i+=3) {
            unsigned needed=0;for(unsigned j=0;j<3;j++) if(flat||remap[(unsigned)triangles[i+j]]<0) needed++;
            if(vertices.size()+needed>262144||indices.size()+3>1572864) flush();
            for(unsigned j=0;j<3;j++) {
                unsigned vertex=(unsigned)triangles[i+j];
                if(flat||remap[vertex]<0) {remap[vertex]=(int)vertices.size();vertices.append(vertex);}
                indices.append(remap[vertex]);
            }
        }
        flush();
    }
    bool primitive(Item parent,ItemReader node,unsigned node_index,ItemReader mesh,ItemReader primitive,unsigned primitive_index) {
        auto attributes=asset_field(primitive,"attributes");lam::ArrayList<double> positions;
        if(!components(attributes,"POSITION",3,positions)||positions.empty()) return fail("invalid mesh positions");
        size_t vertices=positions.size()/3;Item geometry=a.element("geometry"),out=a.element("mesh");
        GltfColumn columns[6];columns[0].name="positions";columns[0].width=3;
        columns[0].values=static_cast<lam::ArrayList<double>&&>(positions);
        a.attr(out,"id",a.id("mesh",a.serial++));a.attr(geometry,"type",a.symbol("buffer"));
        const char* keys[]={"NORMAL","TEXCOORD_0","COLOR_0","JOINTS_0","WEIGHTS_0"};const char* attrs[]={"normals","uvs","colors","skin-indices","skin-weights"};
        int si=integer(node,"skin");if(si>=0&&(size_t)si>=skins.size()) return fail("skin index out of bounds");
        bool has_joints=!asset_field(attributes,"JOINTS_0").isNull(),has_weights=!asset_field(attributes,"WEIGHTS_0").isNull();
        if(has_joints!=has_weights||(has_joints&&si<0)) return fail("joint/weight attributes require a skin");
        if(!asset_field(attributes,"JOINTS_1").isNull()||!asset_field(attributes,"WEIGHTS_1").isNull()) return fail("more than four skin influences are not supported");
        for(unsigned k=0;k<5;k++) if(!asset_field(attributes,keys[k]).isNull()) {
            lam::ArrayList<double> data;unsigned c,ct;if(!accessor(integer(attributes,keys[k]),data,&c,&ct)) return false;
            unsigned expected=k==1?2:k<3?3:4;
            bool normalized=asset_field(object("accessors",integer(attributes,keys[k])),"normalized").asBool();
            if((k<2&&!attribute_encoding(keys[k],ct,normalized))||(k==3&&normalized)||
                (k>=2&&k!=3&&ct!=5126&&!((ct==5121||ct==5123)&&normalized))) return fail("invalid vertex attribute component encoding");
            if((c!=expected&&!(k==2&&c==4))||data.size()!=vertices*c) return fail("vertex attribute size mismatch");
            // native raster textures use lower-left UVs; glTF image UVs start at the top.
            if(k==1) for(size_t v=0;v<vertices;v++) data[v*2+1]=1-data[v*2+1];
            if(k==3) {
                if(ct!=5121&&ct!=5123) return fail("JOINTS_0 must use unsigned byte/short components");
                auto joints=asset_field(object("skins",si),"joints");
                for(double& value:data) {
                    if(value<0||floor(value)!=value||value>=asset_count(joints)) return fail("skin joint index out of bounds");
                    value=skins[si].remap[integer(asset_at(joints,(size_t)value))];
                }
            }
            if(k==4) for(size_t v=0;v<vertices;v++) {
                double sum=0;for(unsigned n=0;n<4;n++) {double w=data[v*4+n];if(w<0||w>1) return fail("invalid skin weight");sum+=w;}
                if(sum<=0) return fail("vertex has zero total skin weight");for(unsigned n=0;n<4;n++) data[v*4+n]/=sum;
            }
            if(k==2) {
                lam::ArrayList<double> rgb;
                for(size_t v=0;v<vertices;v++) {if(c==4&&data[v*4+3]!=1) return fail("vertex alpha is not supported");for(unsigned n=0;n<3;n++) {
                    double value=data[v*c+n];if(value<0||value>1) return fail("vertex color outside [0,1]");
                    rgb.append(value<=.0031308?12.92*value:1.055*pow(value,1.0/2.4)-.055);
                }}data=static_cast<lam::ArrayList<double>&&>(rgb);
            }
            columns[k+1].name=attrs[k];columns[k+1].width=expected;
            columns[k+1].values=static_cast<lam::ArrayList<double>&&>(data);
        }
        if(si>=0) {if(!has_joints) return fail("skinned primitive lacks joint weights");a.attr(out,"skeleton",a.id("skeleton",si));}
        lam::ArrayList<double> indices;int ii=integer(primitive,"indices");
        if(ii>=0) {unsigned c,ct;if(!accessor(ii,indices,&c,&ct)||c!=1||asset_field(object("accessors",ii),"normalized").asBool()||(ct!=5121&&ct!=5123&&ct!=5125)) return fail("invalid index accessor");}
        else for(size_t i=0;i<vertices;i++) indices.append(i);
        for(double i:indices) if(i<0||floor(i)!=i||i>=vertices) return fail("mesh index out of bounds");
        int mode=integer(primitive,"mode",4);lam::ArrayList<double> triangles;
        if(mode==4) {if(indices.size()%3) return fail("triangle indices must be a multiple of three");triangles=static_cast<lam::ArrayList<double>&&>(indices);}
        else if(mode==5||mode==6) for(size_t i=2;i<indices.size();i++) {
            double p=indices[mode==6?0:i-2],q=indices[i-1],r=indices[i];if(p==q||q==r||r==p) continue;
            triangles.append(mode==5&&(i%2)?q:p);triangles.append(mode==5&&(i%2)?p:q);triangles.append(r);
        } else return fail("only TRIANGLES, TRIANGLE_STRIP and TRIANGLE_FAN primitives are supported");
        if(triangles.empty()||triangles.size()>8388608) return fail("triangle index quota exceeded");
        auto targets=asset_field(primitive,"targets");size_t target_count=asset_count(targets);
        if(target_count>2) return fail("more than two morph targets are not supported");
        bool flat=si<0&&!target_count&&!columns[1].name;
        if(flat) {
            // glTF 2.0 §3.7.2.1 requires flat normals when NORMAL is absent.
            // Opposite faces must not cancel at shared vertices; zero-area faces have no normal.
            lam::ArrayList<double> faces;
            for(size_t i=0;i<triangles.size();i+=3) {
                double normal[3];
                if(!face_normal(columns[0].values,(unsigned)triangles[i],(unsigned)triangles[i+1],(unsigned)triangles[i+2],normal)) continue;
                for(unsigned j=0;j<3;j++) faces.append(triangles[i+j]);
            }
            triangles=static_cast<lam::ArrayList<double>&&>(faces);
            if(triangles.empty()) return fail("mesh contains no nondegenerate triangles");
        }
        int mi=integer(primitive,"material",materials.size()-1);if(mi<0||(size_t)mi>=materials.size()) return fail("material index out of bounds");
        Item material=ElementReader(materials[mi]).get_attr("id").item();
        char id[80];snprintf(id,sizeof(id),"asset-node-%u-primitive-%u",node_index,primitive_index);
        if(flat||vertices>262144||triangles.size()>1572864) {
            if(si>=0||target_count) return fail("large animated primitives exceed the native deformation budget");
            Item group=a.element("group");a.attr(group,"id",a.text(id));
            static_chunks(group,material,columns,triangles,vertices,flat);a.append(parent,group);return true;
        }
        geometry_columns(geometry,columns);a.attr(geometry,"indices",a.array(triangles.data(),triangles.size()));
        if(target_count) {
            lam::ArrayList<double> morph,normals;
            for(size_t t=0;t<target_count;t++) for(unsigned k=0;k<2;k++) {
                auto target=asset_at(targets,t);lam::ArrayList<double> data;
                if(!asset_field(target,k?"NORMAL":"POSITION").isNull()) {if(!components(target,k?"NORMAL":"POSITION",3,data,true)||data.size()!=vertices*3) return fail("invalid morph target");}
                else for(size_t i=0;i<vertices*3;i++) data.append(0);
                auto& destination=k?normals:morph;for(double value:data) destination.append(value);
            }
            a.attr(geometry,"morph-positions",a.array(morph.data(),morph.size()));a.attr(geometry,"morph-normals",a.array(normals.data(),normals.size()));
            auto weights=asset_field(node,"weights");if(weights.isNull()) weights=asset_field(mesh,"weights");
            if(!weights.isNull()&&asset_count(weights)!=target_count) return fail("morph weight count mismatch");
            double values[2]={};for(size_t t=0;t<target_count;t++) values[t]=asset_component(weights,t,0);
            a.attr(out,"morph-weights",a.array(values,target_count));
        }
        a.attr(out,"material",material);
        // retain a stable address for weights channels on every primitive of a node.
        a.attr(out,"id",a.text(id));
        a.append(out,geometry);a.append(parent,out);return true;
    }
    bool build_node(unsigned index,Item parent,unsigned depth=0) {
        if(index>=nodes.size()||depth>64||visited[index]) return fail("selected scene has duplicate or invalid roots");visited[index]=1;
        auto node=object("nodes",index);Item out=a.element("group");a.attr(out,"id",node_id(index));if(!transform(out,node)) return false;
        nodes[index]=out;a.append(parent,out);
        int mesh=integer(node,"mesh");if(mesh>=0) {
            if((size_t)mesh>=asset_count(field("meshes"))) return fail("mesh index out of bounds");
            auto source=object("meshes",mesh),primitives=asset_field(source,"primitives");
            if(!asset_count(primitives)) return fail("mesh has no primitives");
            for(unsigned p=0;p<asset_count(primitives);p++) if(!primitive(out,node,index,source,asset_at(primitives,p),p)) return false;
        }
        if(!asset_field(node,"camera").isNull()) a.ctx.addWarning("scene3d asset: embedded glTF camera is omitted; supply a scene camera");
        auto children=asset_field(node,"children");for(size_t i=0;i<asset_count(children);i++) if(!build_node(integer(asset_at(children,i)),out,depth+1)) return false;
        return true;
    }
    bool load_scene() {
        auto scenes=field("scenes");if(asset_count(scenes)) {
            int si=integer(source,"scene",0);if(si<0||(size_t)si>=asset_count(scenes)) return fail("scene index out of bounds");
            auto roots=asset_field(asset_at(scenes,si),"nodes");
            for(size_t i=0;i<asset_count(roots);i++) {int node=integer(asset_at(roots,i));if(node<0||(size_t)node>=nodes.size()||parents[node]>=0||!build_node(node,a.root)) return fail("invalid scene root");}
        } else for(size_t i=0;i<nodes.size();i++) if(parents[i]<0&&!build_node(i,a.root)) return false;
        return true;
    }
    bool track(Item clip,Item id,const char* property,const char* kind,const char* interpolation,unsigned count,
        const lam::ArrayList<double>& times,const lam::ArrayList<double>& output,bool cubic) {
        Item track=a.element("keyframe-track");StrBuf* path=strbuf_create(asset_read(id).cstring());strbuf_append_char(path,'.');strbuf_append_str(path,property);
        a.attr(track,"path",a.text(path->str));strbuf_free(path);a.attr(track,"type",a.symbol(kind));a.attr(track,"interpolation",a.symbol(interpolation));
        a.attr(track,"times",a.array(times.data(),times.size()));
        if(cubic) {
            lam::ArrayList<double> values,in,out;
            for(size_t k=0;k<times.size();k++) for(unsigned c=0;c<count;c++) {
                in.append(output[(k*3)*count+c]);values.append(output[(k*3+1)*count+c]);out.append(output[(k*3+2)*count+c]);
            }
            a.attr(track,"values",a.array(values.data(),values.size()));a.attr(track,"in-tangents",a.array(in.data(),in.size()));a.attr(track,"out-tangents",a.array(out.data(),out.size()));
        } else a.attr(track,"values",a.array(output.data(),output.size()));
        a.append(clip,track);return true;
    }
    bool load_animations() {
        if(asset_count(field("animations"))>256) return fail("animation clip quota exceeded");
        for(unsigned ai=0;ai<asset_count(field("animations"));ai++) {
            auto animation=object("animations",ai),samplers=asset_field(animation,"samplers"),channels=asset_field(animation,"channels");
            Item clip=a.element("animation-clip");a.attr(clip,"id",a.id("clip",ai));
            auto label=asset_field(animation,"name");if(label.isString()) a.attr(clip,"label",a.text(label.cstring()));
            double duration=0;unsigned tracks=0;
            for(size_t ci=0;ci<asset_count(channels);ci++) {
                auto channel=asset_at(channels,ci),target=asset_field(channel,"target");int node=integer(target,"node"),si=integer(channel,"sampler");
                if(node<0||(size_t)node>=nodes.size()||si<0||(size_t)si>=asset_count(samplers)) return fail("animation target or sampler index out of bounds");
                bool bone=false;for(auto& skin:skins) bone|=skin.remap[node]>=0;
                if(!visited[node]&&!bone) continue;
                const char* path=asset_field(target,"path").cstring();bool weights=asset_is(path,"weights"),rotation=asset_is(path,"rotation");
                const char* property=weights?"morph-weights":rotation?"quaternion":asset_is(path,"translation")?"position":asset_is(path,"scale")?"scale":nullptr;
                if(!property) return fail("unknown animation target property");
                if(!weights&&!asset_field(object("nodes",node),"matrix").isNull()) return fail("TRS animation cannot target a matrix node");
                auto sampler=asset_at(samplers,si);const char* interpolation=asset_field(sampler,"interpolation").cstring();if(!interpolation) interpolation="LINEAR";
                bool cubic=asset_is(interpolation,"CUBICSPLINE");const char* method=cubic?"hermite":asset_is(interpolation,"LINEAR")?"linear":asset_is(interpolation,"STEP")?"discrete":nullptr;
                if(!method) return fail("unknown animation interpolation");
                lam::ArrayList<double> times,output;unsigned tc,oc,tct,oct;
                if(!accessor(integer(sampler,"input"),times,&tc,&tct)||tc!=1||tct!=5126||times.size()>65536||!accessor(integer(sampler,"output"),output,&oc,&oct)||oct!=5126) return false;
                for(size_t i=0;i<times.size();i++) if(times[i]<0||(i&&times[i]<=times[i-1])) return fail("animation times must be nonnegative and strictly increasing");
                unsigned count=rotation?4:3;
                if(weights) {
                    int mi=integer(object("nodes",node),"mesh");auto primitives=asset_field(object("meshes",mi),"primitives");
                    count=asset_count(asset_field(asset_at(primitives,0),"targets"));if(!count||count>2||oc!=1) return fail("weights channel lacks matching morph targets");
                    for(size_t p=0;p<asset_count(primitives);p++) {
                        if(asset_count(asset_field(asset_at(primitives,p),"targets"))!=count) return fail("primitive morph target counts differ");
                    }
                } else if(oc!=count) return fail("animation output vector type mismatch");
                if(output.size()!=times.size()*count*(cubic?3:1)) return fail("animation output count mismatch");
                const char* kind=rotation?"quaternion":"vector";
                if(weights) {
                    auto primitives=asset_field(object("meshes",integer(object("nodes",node),"mesh")),"primitives");
                    for(unsigned p=0;p<asset_count(primitives);p++) {char id[80];snprintf(id,sizeof(id),"asset-node-%u-primitive-%u",node,p);track(clip,a.text(id),property,kind,method,count,times,output,cubic);tracks++;}
                } else {
                    if(visited[node]) {track(clip,node_id(node),property,kind,method,count,times,output,cubic);tracks++;}
                    for(unsigned s=0;s<skins.size();s++) if(skins[s].remap[node]>=0) {track(clip,bone_id(s,node),property,kind,method,count,times,output,cubic);tracks++;}
                }
                duration=fmax(duration,times.back());
            }
            if(tracks>256) return fail("animation channel quota exceeded");
            if(tracks) {a.attr(clip,"duration",a.number(duration));a.append(a.root,clip);}
        }
        return true;
    }
};
}
bool asset_import_gltf(SceneAsset& asset,Item source,const uint8_t* binary,size_t binary_length) {
    Gltf g(asset,source,binary,binary_length);const char* version=asset_field(asset_field(g.source,"asset"),"version").cstring();
    if(!asset_is(version,"2.0")) return g.fail("only glTF 2.0 is supported");
    auto required=g.field("extensionsRequired");for(size_t i=0;i<asset_count(required);i++) {
        const char* extension=asset_at(required,i).cstring();
        if(asset_is(extension,"KHR_mesh_quantization")) g.quantization=true;
        else if(!asset_is(extension,"KHR_materials_unlit")) return g.fail("unsupported required glTF extension");
    }
    auto used=g.field("extensionsUsed");for(size_t i=0;i<asset_count(used);i++) if(!asset_is(asset_at(used,i).cstring(),"KHR_materials_unlit")&&
        !asset_is(asset_at(used,i).cstring(),"KHR_mesh_quantization"))
        asset.ctx.addWarning("scene3d asset: optional glTF extension '%s' uses its core fallback",asset_at(used,i).cstring());
    return g.load_buffers()&&g.load_materials()&&g.hierarchy()&&g.load_skins()&&g.load_scene()&&g.load_animations();
}

static uint32_t glb_word(const char* bytes) {
    const uint8_t* p=(const uint8_t*)bytes;
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
bool asset_import_glb(SceneAsset& asset,const char* source,size_t length) {
    if(length<20||memcmp(source,"glTF",4)||glb_word(source+4)!=2||glb_word(source+8)!=length)
        return asset.fail("invalid GLB 2.0 header or declared length");
    const char* json=nullptr;size_t json_length=0;const uint8_t* binary=nullptr;size_t binary_length=0;
    for(size_t offset=12;offset<length;) {
        if(length-offset<8) return asset.fail("truncated GLB chunk header");
        uint32_t size=glb_word(source+offset),kind=glb_word(source+offset+4);offset+=8;
        if(size%4||size>length-offset) return asset.fail("invalid GLB chunk bounds or alignment");
        if(!json&&kind!=0x4e4f534a) return asset.fail("GLB JSON must be the first chunk");
        if(kind==0x4e4f534a) {
            if(json) return asset.fail("duplicate GLB JSON chunk");
            json=source+offset;json_length=size;
        } else if(kind==0x004e4942) {
            if(binary) return asset.fail("duplicate GLB BIN chunk");
            binary=(const uint8_t*)source+offset;binary_length=size;
        }
        offset+=size;
    }
    if(!json||!input_parse_model(asset.ctx.input(),json,json_length,"gltf")||asset.ctx.input()->parse_failed)
        return asset.fail("invalid GLB JSON document");
    // D4.1.3: import copies decoded values into the destination Input before the source bytes expire.
    return asset_import_gltf(asset,asset.ctx.input()->root,binary,binary_length);
}
}
