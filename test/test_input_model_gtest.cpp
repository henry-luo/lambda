#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/mime-detect.h"
#include "../lib/url.h"
#include "../lib/strbuf.h"
#include "../lib/base64.h"
#include "../lib/mem.h"
#include "../lambda/input/input-scene3d.hpp"
#include "../lambda/format/format.h"
#include <zlib.h>

class ModelInputTest : public ::testing::Test {
protected:
    void SetUp() override { log_init(nullptr); }

    Input* parse(const char* source, const char* type = nullptr,
                 const char* filename = nullptr, size_t length = SIZE_MAX) {
        String* format = nullptr;
        if (type) {
            format = (String*)malloc(sizeof(String) + strlen(type) + 1);
            format->len = strlen(type);
            memcpy(format->chars, type, format->len + 1);
        }
        Url* cwd=get_current_dir();Url* url=filename?url_parse_with_base(filename,cwd):nullptr;url_destroy(cwd);
        Input* input = input_from_source_n(source, length == SIZE_MAX ? strlen(source) : length,
            url, format, nullptr);
        free(format);
        return input;
    }

    ElementReader parsed(const char* source, const char* type) {
        Input* input = parse(source, type);
        EXPECT_NE(input, nullptr);
        EXPECT_FALSE(input->parse_failed) << (input->parse_error_message ? input->parse_error_message : "");
        return ElementReader(input->root);
    }
};

TEST_F(ModelInputTest, ObjPreservesTopologyStateAndRelativeIndices) {
    ElementReader root = parsed("o My mesh\ng front back\nv 1 2 3\nv 4 5 6 1\n"
        "vt .25 .5\nvn 0 1 0\nusemtl copper\nf -4/1/1 -3//1 -2/1/1 -1//1\n", "obj");
    EXPECT_TRUE(root.hasTag("obj"));
    EXPECT_STREQ(root.childAt(0).asElement().childAt(0).cstring(), "My mesh");
    EXPECT_EQ(root.findChildElement("g").childAt(0).asArray().length(), 2);
    ArrayReader vertex = root.findChildElement("v").childAt(0).asArray();
    EXPECT_EQ(vertex.length(), 3);
    EXPECT_DOUBLE_EQ(vertex.get(2).asFloat(), 3.0);
    ElementReader face = root.findChildElement("f");
    EXPECT_EQ(face.childCount(), 4);
    EXPECT_EQ(face.childAt(0).asMap().get("v").asInt(), -4);
    EXPECT_EQ(face.childAt(0).asMap().get("vt").asInt(), 1);
    EXPECT_TRUE(face.childAt(1).asMap().get("vt").isNull());
    EXPECT_EQ(face.childAt(1).asMap().get("vn").asInt(), 1);
}

TEST_F(ModelInputTest, ObjHandlesBoundedBuffersBomCommentsAndContinuation) {
    char source[] = "\xef\xbb\xbf" "v 1 2 \\\r\n3 # vertex\r\nf 1 2 3";
    Input* input = parse(source, "obj", nullptr, sizeof(source) - 1);
    ASSERT_FALSE(input->parse_failed);
    memset(source, 'x', sizeof(source));
    ElementReader root(input->root);
    EXPECT_DOUBLE_EQ(root.findChildElement("v").childAt(0).asArray().get(2).asFloat(), 3.0);
    EXPECT_EQ(root.findChildElement("f").childCount(), 3);
}

TEST_F(ModelInputTest, ObjFreeFormDefinitionsAndCommandsAreInertData) {
    ElementReader root = parsed("cstype rat bspline\ndeg 3 3\nparm u 0 0 1 1\n"
        "surf 0 1 0 1 1/1/1 2/2/2\ntrim 0 1 2\nend\nmg off\n"
        "csh touch should_never_be_created\ncall library.obj 1\n", "obj");
    EXPECT_STREQ(root.findChildElement("cstype").childAt(0).asArray().get(0).cstring(), "rat");
    EXPECT_STREQ(root.findChildElement("parm").get_attr("axis").cstring(), "u");
    EXPECT_DOUBLE_EQ(root.findChildElement("surf").get_attr("v1").asFloat(), 1.0);
    EXPECT_EQ(root.findChildElement("trim").childAt(0).asMap().get("curve").asInt(), 2);
    EXPECT_STREQ(root.findChildElement("csh").childAt(0).cstring(), "touch should_never_be_created");
}

TEST_F(ModelInputTest, MtlColorsSpectraOptionsAndRepeatedProperties) {
    ElementReader root = parsed("newmtl brushed copper\nKd .8 .4 .2\nKd xyz .2 .3 .4\n"
        "Ks spectral copper.rfl .5\nd -halo .75\nillum 2\nPr .3\n"
        "map_Kd -o -1 .5 -s 2 2 2 -mm 0 1 -clamp on textures/copper grain.png\n", "mtl");
    ElementReader material = root.findChildElement("material");
    EXPECT_STREQ(material.get_attr("name").cstring(), "brushed copper");
    EXPECT_EQ(material.childCount(), 7);
    EXPECT_STREQ(material.childAt(0).asElement().get_attr("space").cstring(), "rgb");
    EXPECT_STREQ(material.childAt(1).asElement().get_attr("space").cstring(), "xyz");
    EXPECT_DOUBLE_EQ(material.findChildElement("Ks").get_attr("factor").asFloat(), .5);
    EXPECT_TRUE(material.findChildElement("d").get_attr("halo").asBool());
    EXPECT_EQ(material.findChildElement("illum").childAt(0).asArray().get(0).asInt(), 2);
    ElementReader texture = material.findChildElement("map_Kd");
    EXPECT_STREQ(texture.get_attr("file").cstring(), "textures/copper grain.png");
    ElementReader options = texture.get_attr("options").asElement();
    EXPECT_EQ(options.childCount(), 4);
    EXPECT_DOUBLE_EQ(options.findChildElement("o").childAt(0).asFloat(), -1.0);
    EXPECT_STREQ(options.findChildElement("clamp").childAt(0).cstring(), "on");
}

TEST_F(ModelInputTest, GltfKeepsJsonAnimationExtensionsAndResourceUris) {
    Input* input = parse(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":12}],
      "images":[{"uri":"data:image/png;base64,AA=="}],"nodes":[{"translation":[0,1,2]}],
      "animations":[{"samplers":[{"input":0,"output":1,"interpolation":"CUBICSPLINE"}],
      "channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}]}],
      "extensions":{"CUSTOM":{"enabled":true}},"extras":{"author":"artist"}})", "gltf");
    ASSERT_FALSE(input->parse_failed);
    MapReader root = ItemReader(input->root.to_const()).asMap();
    EXPECT_STREQ(root.get("buffers").asArray().get(0).asMap().get("uri").cstring(), "mesh.bin");
    EXPECT_STREQ(root.get("images").asArray().get(0).asMap().get("uri").cstring(), "data:image/png;base64,AA==");
    EXPECT_STREQ(root.get("animations").asArray().get(0).asMap().get("samplers")
        .asArray().get(0).asMap().get("interpolation").cstring(), "CUBICSPLINE");
    EXPECT_TRUE(root.get("extensions").asMap().get("CUSTOM").asMap().get("enabled").asBool());
}

TEST_F(ModelInputTest, NamesAndUnknownTextureOptionBoundariesArePreserved) {
    ElementReader obj = parsed("g 001 123\nmtllib 456\no Artist's model # comment\n", "obj");
    EXPECT_STREQ(obj.findChildElement("g").childAt(0).asArray().get(0).cstring(), "001");
    EXPECT_STREQ(obj.findChildElement("mtllib").childAt(0).asArray().get(0).cstring(), "456");
    EXPECT_STREQ(obj.findChildElement("o").childAt(0).cstring(), "Artist's model");
    ElementReader mtl = parsed("newmtl x\nmap_Kd -clamp on -vendor 1 2 image.png\n", "mtl");
    ElementReader texture = mtl.findChildElement("material").findChildElement("map_Kd");
    EXPECT_STREQ(texture.get_attr("unparsed").cstring(), "-vendor 1 2 image.png");
    EXPECT_TRUE(texture.get_attr("file").isNull());
}

TEST_F(ModelInputTest, GltfBoundedJsonDecodesEscapesAndKeepsNumericTypes) {
    const char source[] = "\xef\xbb\xbf" R"({"asset":{"version":"2.0"},"name":"A\u0042\uD83C\uDF0D\n","count":4,"values":[-0,0.25,2e3]})" "suffix";
    Input* input = parse(source, "gltf", nullptr, sizeof(source) - 1 - strlen("suffix"));
    ASSERT_FALSE(input->parse_failed);
    MapReader root = ItemReader(input->root.to_const()).asMap();
    EXPECT_STREQ(root.get("name").cstring(), "AB🌍\n");
    EXPECT_TRUE(root.get("count").isInt());
    EXPECT_DOUBLE_EQ(root.get("values").asArray().get(1).asFloat(), .25);
    EXPECT_DOUBLE_EQ(root.get("values").asArray().get(2).asFloat(), 2000.0);
}

TEST_F(ModelInputTest, A3dAssetAndExtensionLinesDoNotMerge) {
    ElementReader root = parsed("3dmodel 1\nM\nL\nA\n\nAssets\none.png\ntwo.png\n\n"
        "Custom\nfirst\nsecond\n\nEnd\n", "a3d");
    ElementReader assets = root.findChildElement("assets");
    EXPECT_EQ(assets.childCount(), 2);
    EXPECT_STREQ(assets.childAt(1).asElement().childAt(0).cstring(), "two.png");
    ElementReader extension = root.findChildElement("chunk");
    EXPECT_EQ(extension.childCount(), 2);
    EXPECT_STREQ(extension.childAt(1).asElement().childAt(0).cstring(), "second");
}

static const char* a3d_model =
    "3dmodel 1.0\nAnimated model\nCC0\nArtist\nTwo lines\nof description\n\n"
    "Preview\npreview.png\n\nTextmap\n.25 .75\n\n"
    "Vertex\n0 0 0 1 #FF102030 0:.25 1:.75\n0 0 0 1 0\n\n"
    "Bones\n0 1 body\n/0 1 head\n//0 1 mouth\n/0 1 arm\n\n"
    "Material copper\nKd #FFB87333\nNs 32\nmap_Kd copper.png\n\n"
    "Mesh body\nuse copper\n0/0/1 1//1 0///1\nuse\npar size\n\n"
    "Shape solid\ngroup 0\ninc 1 0 1\nuse copper\n\n"
    "VoxTypes\n#FF007F00/01/000 default:chest { 1 default:apple 3 default:spoon }\n\n"
    "Voxel terrain\npos -1 0 0\ndim 3 1 1\nlayer\n0 . -\n\n"
    "Labels notes\ncolor #FFFFFFFF\nlang en_US\n0 Origin point\n\n"
    "Action 1000 nod\nframe 0\n0 0 1\nframe 1000\n1 1 0\n\n"
    "Procedural designer\ngenerator.lua\n\nAssets\ncopper.png\n\n"
    "Extra TEST\n00 FF 41\n\nExtension optional\ncustom record # preserved\n\nEnd\n";

TEST_F(ModelInputTest, A3dGeometrySkeletonAndMaterials) {
    ElementReader root = parsed(a3d_model, "a3d");
    EXPECT_TRUE(root.hasTag("a3d"));
    EXPECT_DOUBLE_EQ(root.get_attr("scale").asFloat(), 1.0);
    EXPECT_STREQ(root.get_attr("description").cstring(), "Two lines\nof description");
    ElementReader vertex = root.findChildElement("vertices").childAt(0).asElement();
    EXPECT_EQ(vertex.childAt(0).asArray().length(), 4);
    EXPECT_STREQ(vertex.get_attr("color").cstring(), "#FF102030");
    EXPECT_DOUBLE_EQ(vertex.get_attr("weights").asArray().get(1).asMap().get("weight").asFloat(), .75);
    ElementReader bones = root.findChildElement("bones");
    EXPECT_TRUE(bones.childAt(0).asElement().get_attr("parent").isNull());
    EXPECT_EQ(bones.childAt(2).asElement().get_attr("parent").asInt(), 1);
    EXPECT_EQ(bones.childAt(3).asElement().get_attr("parent").asInt(), 0);
    EXPECT_STREQ(root.findChildElement("material").findChildElement("Kd").childAt(0).cstring(), "#FFB87333");
    ElementReader mesh = root.findChildElement("mesh");
    EXPECT_EQ(mesh.findChildElement("face").childAt(2).asMap().get("maximum").asInt(), 1);
    EXPECT_TRUE(mesh.childAt(2).asElement().childAt(0).isNull());
}

TEST_F(ModelInputTest, A3dVoxelsActionsLabelsAndOpaqueExtensions) {
    ElementReader root = parsed(a3d_model, "a3d");
    ElementReader type = root.findChildElement("voxtypes").childAt(0).asElement();
    EXPECT_EQ(type.get_attr("rotation").asInt(), 1);
    EXPECT_STREQ(type.get_attr("inventory").asArray().get(1).asMap().get("type").cstring(), "default:spoon");
    ArrayReader row = root.findChildElement("voxel").findChildElement("layer").childAt(0).asArray();
    EXPECT_EQ(row.get(1).asInt(), -1);
    EXPECT_EQ(row.get(2).asInt(), -2);
    ElementReader action = root.findChildElement("action");
    EXPECT_EQ(action.get_attr("duration_ms").asInt(), 1000);
    EXPECT_EQ(action.childAt(1).asElement().get_attr("time_ms").asInt(), 1000);
    EXPECT_EQ(action.childAt(1).asElement().childAt(0).asMap().get("bone").asInt(), 1);
    EXPECT_STREQ(root.findChildElement("labels").findChildElement("label").childAt(0).cstring(), "Origin point");
    EXPECT_STREQ(root.findChildElement("procedural").childAt(0).asElement().childAt(0).cstring(), "generator.lua");
    Item payload = root.findChildElement("extra").childAt(0).item();
    ASSERT_EQ(get_type_id(payload), LMD_TYPE_BINARY);
    EXPECT_EQ(binary_length(payload.get_binary()), 3u);
    EXPECT_EQ(memcmp(binary_data(payload.get_binary()), "\x00\xff\x41", 3), 0);
    EXPECT_STREQ(root.findChildElement("chunk").childAt(0).asElement().childAt(0).cstring(), "custom record # preserved");
}

TEST_F(ModelInputTest, FileDetectionRoutesEachTextualFormat) {
    static const struct { const char* path; const char* source; const char* tag; } files[] = {
        {"file:///scene/MESH.OBJ?version=1", "v 0 0 0\n", "obj"},
        {"file:///scene/material.mtl", "newmtl copper\nKd 1 0 0\n", "mtl"},
        {"file:///scene/model.a3d", "3dmodel 1\nModel\nCC0\nMe\n\nEnd\n", "a3d"},
        {"file:///scene/model.gltf", "{\"asset\":{\"version\":\"2.0\"}}", nullptr}
    };
    for (const auto& file : files) {
        SCOPED_TRACE(file.path);
        Input* input = parse(file.source, nullptr, file.path);
        ASSERT_FALSE(input->parse_failed);
        if (file.tag) EXPECT_TRUE(ElementReader(input->root).hasTag(file.tag));
        else EXPECT_TRUE(ItemReader(input->root.to_const()).isMap());
    }
}

TEST_F(ModelInputTest, MimeTypesAndContentDetectionAvoidAmbiguousObjHeuristics) {
    MimeDetector* detector = mime_detector_init();
    EXPECT_STREQ(detect_mime_from_filename(detector, "scene.gltf"), "model/gltf+json");
    EXPECT_STREQ(detect_mime_from_filename(detector, "scene.mtl"), "model/mtl");
    EXPECT_STREQ(detect_mime_from_content(detector, "3dmodel 1\n", 10), "text/x-3d-model");
    EXPECT_STRNE(detect_mime_from_content(detector, "3dmodeljunk", 11), "text/x-3d-model");
    EXPECT_STREQ(mime_extension_from_content_type("model/gltf+json; charset=utf-8"), ".gltf");
    EXPECT_STREQ(mime_extension_from_content_type("model/obj"), ".obj");
    EXPECT_STRNE(detect_mime_from_filename(detector, "model.glb"), "model/gltf+json");
    EXPECT_STRNE(detect_mime_from_filename(detector, "model.m3d"), "text/x-3d-model");
    mime_detector_destroy(detector);
    Input* input = parse("3dmodel 1\nModel\nCC0\nMe\n\nEnd\n");
    EXPECT_TRUE(ElementReader(input->root).hasTag("a3d"));
    EXPECT_TRUE(ItemReader(parse("v 1 2 3\n")->root.to_const()).isString());
}

TEST_F(ModelInputTest, RejectsMalformedObjAndMtlRecords) {
    static const struct { const char* type; const char* source; } bad[] = {
        {"obj", "v 1 2\n"}, {"obj", "vn 1 nope 3\n"}, {"obj", "f 0 1 2\n"},
        {"obj", "f 1/ 2/ 3/\n"}, {"obj", "f 1 2\n"}, {"obj", "v 1 2 3 \\\n"},
        {"mtl", "Kd 1 1 1\n"}, {"mtl", "newmtl\n"}, {"mtl", "newmtl x\nKd spectral\n"},
        {"mtl", "newmtl x\nmap_Kd -mm 1 texture.png\n"},
        {"mtl", "newmtl x\nmap_Kd -clamp on\n"}
    };
    for (const auto& file : bad) {
        SCOPED_TRACE(file.source);
        Input* input = parse(file.source, file.type);
        EXPECT_TRUE(input->parse_failed);
        EXPECT_EQ(get_type_id(input->root), LMD_TYPE_ERROR);
    }
}

TEST_F(ModelInputTest, RejectsMalformedGltfAndIncompleteJson) {
    static const char* bad[] = {
        "[]", "{}", "{\"asset\":{\"version\":2}}", "{\"asset\":{\"version\":\"\"}}",
        "{\"asset\":{\"version\":\"2.0\"}", "{\"asset\":{\"version\":\"2.0\"},",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[", "{\"asset\":{\"version\":\"2.0\"}} {}",
        "{\"asset\":{\"version\":\"2.0\"},\"x\":01}", "{\"asset\":{\"version\":\"2.0\"},\"x\":1.}",
        "{\"asset\":{\"version\":\"2.0\"},\"x\":-.5}",
        R"({"asset":{"version":"2.0"},"x":"\q"})",
        R"({"asset":{"version":"2.0"},"x":"\uZZZZ"})"
    };
    for (const char* source : bad) {
        SCOPED_TRACE(source);
        EXPECT_TRUE(parse(source, "gltf")->parse_failed);
    }
    EXPECT_TRUE(parse("[1", "json")->parse_failed);
    EXPECT_TRUE(parse("{\"a\":1", "json")->parse_failed);
}

TEST_F(ModelInputTest, RejectsMalformedA3dChunks) {
    static const char* chunks[] = {
        "Vertex\n0 0 0 1 0:.2 1:.2\n\nEnd\n",
        "Bones\n//0 0 skipped\n\nEnd\n",
        "Voxel\ndim 2 1 1\nlayer\n0\n\nEnd\n",
        "Action 10 walk\nframe 11\n0 0 0\n\nEnd\n",
        "Action 10 walk\n0 0 0\n\nEnd\n",
        "Extra TEST\n0G\n\nEnd\n", "Extra BAD\n00\n\nEnd\n",
        "Mesh\n-1 0 1\n\nEnd\n", "End\ntrailing\n", "Vertex\n0 0 0 1\n"
    };
    for (const char* chunk : chunks) {
        SCOPED_TRACE(chunk);
        StrBuf* source = strbuf_new();
        strbuf_append_str(source, "3dmodel 1\nModel\nCC0\nArtist\n\n");
        strbuf_append_str(source, chunk);
        EXPECT_TRUE(parse(source->str, "a3d")->parse_failed);
        strbuf_free(source);
    }
    EXPECT_TRUE(parse("3DMO binary", "a3d")->parse_failed);
    EXPECT_TRUE(parse("3dmodel 1\n", "a3d")->parse_failed);
    EXPECT_EQ(parsed("3dmodel 1\nM\nL\nA\n\nExtra VOID\n\nEnd\n", "a3d")
        .findChildElement("extra").childCount(), 0);
}

TEST_F(ModelInputTest, RejectsEmbeddedNulInEveryTextualFormat) {
    static const char* formats[] = {"obj", "mtl", "gltf", "a3d"};
    const char source[] = "v 1 2 3\0v 4 5 6";
    for (const char* format : formats) {
        SCOPED_TRACE(format);
        EXPECT_TRUE(parse(source, format, nullptr, sizeof(source) - 1)->parse_failed);
    }
}

static Input* load_scene_asset(const char* path) {
    Input* owner=InputManager::create_input(nullptr);MarkBuilder b(owner);
    Url* cwd=get_current_dir();Input* result=input_from_url(b.createString(path),b.createString("scene3d-asset"),nullptr,cwd);
    url_destroy(cwd);return result;
}

static void glb_test_word(StrBuf* out,uint32_t word) {
    for(unsigned i=0;i<4;i++) strbuf_append_char(out,(char)(word>>(i*8)));
}
static StrBuf* glb_test_model(unsigned vertices) {
    StrBuf* json=strbuf_new();
    strbuf_append_format(json,R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":%u}],
        "bufferViews":[{"buffer":0,"byteLength":%u,"byteStride":20}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":%u,"type":"VEC3"},
        {"bufferView":0,"byteOffset":12,"componentType":5126,"count":%u,"type":"VEC2"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1}}]}],
        "nodes":[{"mesh":0}]})",vertices*20,vertices*20,vertices,vertices);
    while(json->length%4) strbuf_append_char(json,' ');
    StrBuf* glb=strbuf_new();strbuf_append_str(glb,"glTF");glb_test_word(glb,2);
    glb_test_word(glb,28+json->length+vertices*20);glb_test_word(glb,json->length);glb_test_word(glb,0x4e4f534a);
    strbuf_append_str_n(glb,json->str,json->length);strbuf_free(json);
    glb_test_word(glb,vertices*20);glb_test_word(glb,0x004e4942);
    for(unsigned i=0;i<vertices;i++) {
        float values[5]={i%3==1?1.0f:0.0f,i%3==2?1.0f:0.0f,0.0f,0.25f,0.75f};
        for(float value:values) {uint32_t bits;memcpy(&bits,&value,4);glb_test_word(glb,bits);}
    }
    return glb;
}

static StrBuf* quantized_test_model(const char* extension="\"KHR_mesh_quantization\"",
    unsigned normal_type=5120,bool normalize_normal=true,bool signed_uv=false) {
    StrBuf* json=strbuf_new();
    // Three padded vertices: signed-short positions, signed-byte normals, and ushort UVs.
    strbuf_append_format(json,R"({"asset":{"version":"2.0"},"extensionsUsed":[%s],"extensionsRequired":[%s],
        "buffers":[{"byteLength":48,"uri":"data:application/octet-stream;base64,AIAAAAAAAAAAAIAAAAD///9/AAAAAAAAAACAAACAAAAAAP9/AAAAAAAAgAD//wCA"}],
        "bufferViews":[{"buffer":0,"byteLength":48,"byteStride":16}],
        "accessors":[{"bufferView":0,"componentType":5122,"count":3,"type":"VEC3","normalized":true},
        {"bufferView":0,"byteOffset":8,"componentType":%u,"count":3,"type":"VEC3","normalized":%s},
        {"bufferView":0,"byteOffset":12,"componentType":%u,"count":3,"type":"VEC2","normalized":%s}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
        "targets":[{"POSITION":0,"NORMAL":1}]}],"weights":[0.25]}],
        "nodes":[{"mesh":0,"translation":[10,20,30],"scale":[2,2,2]}]})",
        extension,extension,normal_type,normalize_normal?"true":"false",signed_uv?5122:5123,signed_uv?"false":"true");
    return json;
}

TEST_F(ModelInputTest, SceneAssetQuantizedAttributesPreserveSignedValuesMorphsAndTransforms) {
    StrBuf* source=quantized_test_model();
    Input* input=parse(source->str,"scene3d-asset","quantized.gltf");strbuf_free(source);
    ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto node=ElementReader(input->root).findChildElement("group");
    EXPECT_DOUBLE_EQ(node.get_attr("position").asArray().get(0).asFloat(),10);
    EXPECT_DOUBLE_EQ(node.get_attr("scale").asArray().get(0).asFloat(),2);
    auto geometry=node.findChildElement("mesh").findChildElement("geometry");
    auto positions=geometry.get_attr("positions").asArray(),normals=geometry.get_attr("normals").asArray();
    ASSERT_EQ(positions.length(),9);ASSERT_EQ(normals.length(),9);
    EXPECT_DOUBLE_EQ(positions.get(0).asFloat(),-1);EXPECT_DOUBLE_EQ(positions.get(3).asFloat(),1);
    EXPECT_DOUBLE_EQ(positions.get(7).asFloat(),1);EXPECT_DOUBLE_EQ(normals.get(2).asFloat(),-1);
    EXPECT_DOUBLE_EQ(geometry.get_attr("morph-positions").asArray().get(0).asFloat(),-1);
    EXPECT_DOUBLE_EQ(geometry.get_attr("morph-normals").asArray().get(2).asFloat(),-1);
    auto uv=geometry.get_attr("uvs").asArray();
    EXPECT_DOUBLE_EQ(uv.get(1).asFloat(),0);EXPECT_DOUBLE_EQ(uv.get(2).asFloat(),32768.0/65535);
}

TEST_F(ModelInputTest, SceneAssetQuantizedSignedIntegerUvsRetainTheirRangeAndOrientation) {
    StrBuf* source=quantized_test_model("\"KHR_mesh_quantization\"",5120,true,true);
    Input* input=parse(source->str,"scene3d-asset","quantized-uv.gltf");strbuf_free(source);
    ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto geometry=ElementReader(input->root).findChildElement("group").findChildElement("mesh").findChildElement("geometry");
    auto uv=geometry.get_attr("uvs").asArray();ASSERT_EQ(uv.length(),6);
    EXPECT_DOUBLE_EQ(uv.get(1).asFloat(),2);EXPECT_DOUBLE_EQ(uv.get(2).asFloat(),-32768);
    EXPECT_DOUBLE_EQ(uv.get(4).asFloat(),-1);EXPECT_DOUBLE_EQ(uv.get(5).asFloat(),32769);
}

TEST_F(ModelInputTest, SceneAssetRejectsUndeclaredQuantizationAndInvalidIntegerNormals) {
    struct Case {const char* extension;unsigned normal_type;bool normalized;};
    const Case cases[]={{"",5120,true},{"\"KHR_mesh_quantization\"",5121,true},
        {"\"KHR_mesh_quantization\"",5120,false}};
    for(const auto& c:cases) {
        SCOPED_TRACE(c.normal_type);StrBuf* source=quantized_test_model(c.extension,c.normal_type,c.normalized);
        Input* input=parse(source->str,"scene3d-asset","invalid-quantized.gltf");strbuf_free(source);
        ASSERT_NE(input,nullptr);EXPECT_TRUE(input->parse_failed);
    }
}

TEST_F(ModelInputTest, SceneAssetRejectsUnsignedQuantizedMorphDisplacements) {
    StrBuf* source=quantized_test_model();
    char* type=strstr(source->str,"\"componentType\":5122");ASSERT_NE(type,nullptr);
    type[strlen("\"componentType\":512")]='3';
    Input* input=parse(source->str,"scene3d-asset","unsigned-morph.gltf");strbuf_free(source);
    ASSERT_NE(input,nullptr);ASSERT_TRUE(input->parse_failed);
    EXPECT_NE(strstr(input->parse_error_message,"invalid morph target"),nullptr);
}

TEST_F(ModelInputTest, SceneAssetGlbOwnsBinaryGeometryAndPreservesUvOrientation) {
    StrBuf* glb=glb_test_model(3);
    Input* input=parse(glb->str,"scene3d-asset","model.glb",glb->length);
    strbuf_free(glb);ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto geometry=ElementReader(input->root).findChildElement("group").findChildElement("group").findChildElement("mesh").findChildElement("geometry");
    EXPECT_EQ(geometry.get_attr("positions").asArray().length(),9);
    EXPECT_DOUBLE_EQ(geometry.get_attr("positions").asArray().get(3).asFloat(),1);
    EXPECT_DOUBLE_EQ(geometry.get_attr("uvs").asArray().get(1).asFloat(),0.25);
}

TEST_F(ModelInputTest, SceneAssetGlbRejectsCorruptContainers) {
    StrBuf* glb=glb_test_model(3);
    const unsigned offsets[]={0,4,8,12,16};
    for(unsigned offset:offsets) {
        SCOPED_TRACE(offset);char saved=glb->str[offset];glb->str[offset]^=1;
        Input* input=parse(glb->str,"scene3d-asset","model.glb",glb->length);
        ASSERT_NE(input,nullptr);EXPECT_TRUE(input->parse_failed);glb->str[offset]=saved;
    }
    Input* truncated=parse(glb->str,"scene3d-asset","model.glb",glb->length-1);
    ASSERT_NE(truncated,nullptr);EXPECT_TRUE(truncated->parse_failed);
    strbuf_free(glb);
}

TEST_F(ModelInputTest, SceneAssetDropsZeroAreaFacesAndTheirOrphanedVertices) {
    StrBuf* glb=glb_test_model(6);
    // The second triangle consists of three coincident positions, with independent UVs.
    size_t last=glb->length-60;
    for(unsigned v=0;v<3;v++) memset(glb->str+last+v*20,0,12);
    Input* input=parse(glb->str,"scene3d-asset","degenerate.glb",glb->length);
    strbuf_free(glb);ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto geometry=ElementReader(input->root).findChildElement("group").findChildElement("group")
        .findChildElement("mesh").findChildElement("geometry");
    EXPECT_EQ(geometry.get_attr("positions").asArray().length(),9);
    EXPECT_EQ(geometry.get_attr("indices").asArray().length(),3);
    EXPECT_EQ(geometry.get_attr("uvs").asArray().length(),6);
}

TEST_F(ModelInputTest, SceneAssetMissingNormalsPreservesOppositeFaces) {
    StrBuf* glb=glb_test_model(6);
    // Reverse the second face: averaging these shared positions would cancel both normals.
    char corner[20];size_t last=glb->length-40;
    memcpy(corner,glb->str+last,20);memcpy(glb->str+last,glb->str+last+20,20);memcpy(glb->str+last+20,corner,20);
    Input* input=parse(glb->str,"scene3d-asset","opposite.glb",glb->length);
    strbuf_free(glb);ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto geometry=ElementReader(input->root).findChildElement("group").findChildElement("group")
        .findChildElement("mesh").findChildElement("geometry");
    auto normals=geometry.get_attr("normals").asArray();ASSERT_EQ(normals.length(),18);
    for(unsigned vertex=0;vertex<6;vertex++) EXPECT_DOUBLE_EQ(normals.get(vertex*3+2).asFloat(),vertex<3?1:-1);
    EXPECT_EQ(geometry.get_attr("indices").asArray().length(),6);
}

TEST_F(ModelInputTest, SceneAssetPartitionsLargeStaticMeshWithoutDroppingTrianglesOrUvs) {
    const unsigned vertices=262146;StrBuf* glb=glb_test_model(vertices);
    Input* input=parse(glb->str,"scene3d-asset","large.glb",glb->length);
    strbuf_free(glb);ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    auto group=ElementReader(input->root).findChildElement("group").findChildElement("group");
    ASSERT_EQ(group.childCount(),2);size_t count=0;
    for(int64_t i=0;i<group.childCount();i++) {
        auto geometry=group.childAt(i).asElement().findChildElement("geometry");
        auto positions=geometry.get_attr("positions").asArray(),uvs=geometry.get_attr("uvs").asArray();
        auto indices=geometry.get_attr("indices").asArray();
        EXPECT_LE(positions.length()/3,262144);EXPECT_EQ(uvs.length(),positions.length()/3*2);
        EXPECT_EQ(indices.length()%3,0);count+=indices.length();
        EXPECT_DOUBLE_EQ(indices.get(indices.length()-1).asFloat(),positions.length()/3-1);
        EXPECT_DOUBLE_EQ(uvs.get(uvs.length()-1).asFloat(),0.25);
    }
    EXPECT_EQ(count,vertices);
}

TEST_F(ModelInputTest, SceneAssetResolvesObjMaterialTextureAndConcaveNegativeCorners) {
    Input* input=load_scene_asset("test/demo/scene3d/assets/loading/concave.obj");ASSERT_NE(input,nullptr);
    ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    ElementReader root(input->root),mesh=root.findChildElement("mesh");
    EXPECT_TRUE(root.hasTag("group"));
    auto geometry=mesh.findChildElement("geometry");
    EXPECT_EQ(geometry.get_attr("positions").asArray().length(),45);
    EXPECT_EQ(geometry.get_attr("normals").asArray().length(),45);
    EXPECT_EQ(geometry.get_attr("uvs").asArray().length(),30);
    auto texture=root.findChildElement("resources").findChildElement("texture");
    ASSERT_NE(texture.get_attr("src").cstring(),nullptr);
    EXPECT_NE(strstr(texture.get_attr("src").cstring(),"/loading/textures/checker.png"),nullptr);
    EXPECT_EQ(strstr(texture.get_attr("src").cstring(),"/../"),nullptr);
}

TEST_F(ModelInputTest, SceneAssetDecodesExternalAndEmbeddedGltfSkinMorphAndCubicTracks) {
    const char* files[]={"test/demo/scene3d/assets/loading/articulated.gltf","test/demo/scene3d/assets/loading/embedded.gltf"};
    for(const char* file:files) {
        SCOPED_TRACE(file);Input* input=load_scene_asset(file);ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
        ElementReader root(input->root),rig=root.findChildElement("skeleton"),bone=rig.findChildElement("bone");
        EXPECT_EQ(bone.get_attr("inverse-bind-matrix").asArray().length(),16);
        EXPECT_NEAR(bone.get_attr("inverse-bind-matrix").asArray().get(12).asFloat(),-.2,1e-6);
        ElementReader node=root.findChildElement("group");
        EXPECT_STREQ(node.get_attr("id").cstring(),"asset-node-0");
        ElementReader mesh=root.childAt(3).asElement().findChildElement("mesh");
        auto geometry=mesh.findChildElement("geometry");
        auto positions=geometry.get_attr("positions").asArray();ASSERT_EQ(positions.length(),24);EXPECT_NEAR(positions.get(0).asFloat(),-.8,1e-6);
        EXPECT_EQ(geometry.get_attr("skin-indices").asArray().get(0).asFloat(),0);
        EXPECT_EQ(geometry.get_attr("skin-indices").asArray().get(16).asFloat(),1);
        EXPECT_EQ(geometry.get_attr("skin-weights").asArray().get(0).asFloat(),1);
        EXPECT_NEAR(geometry.get_attr("morph-positions").asArray().get(18).asFloat(),.5,1e-6);
        auto clip=root.findChildElement("animation-clip");EXPECT_EQ(clip.childCount(),5);
        auto cubic=clip.childAt(2).asElement();EXPECT_EQ(cubic.get_attr("in-tangents").asArray().length(),6);
        EXPECT_EQ(cubic.get_attr("interpolation").getType(),LMD_TYPE_SYMBOL);
        auto texture=root.findChildElement("resources").findChildElement("texture");
        EXPECT_EQ(strncmp(texture.get_attr("src").cstring(),"data:image/png;base64,",22),0);
        EXPECT_STREQ(root.findChildElement("resources").findChildElement("material").get_attr("alpha-mode").cstring(),"opaque");
    }
}

TEST_F(ModelInputTest, SceneAssetBuildsA3dSkeletonAndPersistentActionTracks) {
    Input* input=load_scene_asset("test/demo/scene3d/assets/loading/puppet.a3d");ASSERT_NE(input,nullptr);ASSERT_FALSE(input->parse_failed)<<input->parse_error_message;
    ElementReader root(input->root),rig=root.findChildElement("skeleton"),clip=root.findChildElement("animation-clip");
    EXPECT_EQ(rig.findChildElement("bone").findChildElement("bone").childCount(),0);
    ASSERT_EQ(clip.childCount(),4);EXPECT_EQ(clip.get_attr("duration").asFloat(),2);
    EXPECT_EQ(clip.childAt(0).asElement().get_attr("times").asArray().length(),5);
    auto child_positions=clip.childAt(2).asElement().get_attr("values").asArray();
    EXPECT_EQ(child_positions.get(3).asFloat(),0);
    EXPECT_NEAR(child_positions.get(6).asFloat(),.6,1e-6);
    EXPECT_NEAR(child_positions.get(9).asFloat(),.6,1e-6);
    auto geometry=root.findChildElement("mesh").findChildElement("geometry");
    EXPECT_EQ(geometry.get_attr("positions").asArray().length(),36);
    EXPECT_EQ(geometry.get_attr("skin-indices").asArray().length(),48);
}

TEST_F(ModelInputTest, SceneAssetRejectsUnsupportedRequiredExtensionsAndBrokenTopology) {
    const char* invalid[]={
        "{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[1]},{\"children\":[0]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,AAAA\",\"byteLength\":8}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],\"accessors\":[{\"count\":3,\"type\":\"VEC3\",\"componentType\":5126,\"bufferView\":0}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":4}],\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,AAAAAA==\",\"byteLength\":4}]}"
    };
    for(const char* source:invalid) {Input* input=parse(source,"scene3d-asset","fixture.gltf");ASSERT_NE(input,nullptr);EXPECT_TRUE(input->parse_failed);EXPECT_EQ(get_type_id(input->root),LMD_TYPE_ERROR);}
    Input* obj=parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -4 2 3\n","scene3d-asset","fixture.obj");ASSERT_NE(obj,nullptr);EXPECT_TRUE(obj->parse_failed);
    Input* command=parse("csh do-not-execute\n","scene3d-asset","fixture.obj");ASSERT_NE(command,nullptr);EXPECT_TRUE(command->parse_failed);
}

TEST_F(ModelInputTest, SceneAssetMaterialDeclarationsAndClampOptionsResolveIndependentlyOfOrder) {
    Input* obj=parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl painted\nf 1/1 2/1 3/1\no next\nmtllib materials/palette.mtl\n",
        "scene3d-asset","test/demo/scene3d/assets/loading/virtual.obj");
    ASSERT_NE(obj,nullptr);ASSERT_FALSE(obj->parse_failed)<<obj->parse_error_message;
    auto texture=ElementReader(obj->root).findChildElement("resources").findChildElement("texture");
    EXPECT_EQ(texture.get_attr("wrap-s").asFloat(),10497);
    Input* mtl=parse("newmtl checker\nmap_Kd -clamp on textures/checker.png\n",
        "scene3d-asset","test/demo/scene3d/assets/loading/virtual.mtl");
    ASSERT_NE(mtl,nullptr);ASSERT_FALSE(mtl->parse_failed)<<mtl->parse_error_message;
    auto clamped=ElementReader(mtl->root).findChildElement("resources").findChildElement("texture");
    EXPECT_EQ(clamped.get_attr("wrap-s").asFloat(),33071);EXPECT_EQ(clamped.get_attr("wrap-t").asFloat(),33071);
    Input* missing=parse("mtllib definitely_missing_loader_fixture.mtl\n","scene3d-asset","fixture.obj");
    ASSERT_NE(missing,nullptr);EXPECT_TRUE(missing->parse_failed);
}

// A minimal binary FBX writer for property-kind and malformed-record cases
// (Lambda_Pkg_Motion.md §4.2, §4.3, §11).
struct FbxTestWriter {
    StrBuf* out = strbuf_new();
    bool wide;
    struct Record { size_t start, props; uint64_t count; size_t name; };
    explicit FbxTestWriter(uint32_t version = 7400) : wide(version >= 7500) {
        strbuf_append_str_n(out, "Kaydara FBX Binary  \0\x1a\0", 23); le(version, 4);
    }
    ~FbxTestWriter() { strbuf_free(out); }
    size_t header() const { return wide ? 25 : 13; }
    void le(uint64_t value, int bytes) { for (int i = 0; i < bytes; i++) strbuf_append_char(out, (char)(value >> (8 * i))); }
    void patch(size_t at, uint64_t value, int bytes) { for (int i = 0; i < bytes; i++) out->str[at + i] = (char)(value >> (8 * i)); }
    void zeros(size_t n) { for (size_t i = 0; i < n; i++) strbuf_append_char(out, 0); }
    Record open(const char* name) {
        Record r = {out->length, 0, 0, strlen(name)};
        zeros(header()); strbuf_append_str(out, name); r.props = out->length;
        return r;
    }
    void scalar(Record& r, char code, uint64_t bits, int bytes) { strbuf_append_char(out, code); le(bits, bytes); r.count++; }
    void bytes(Record& r, char code, const char* data, size_t n) {
        strbuf_append_char(out, code); le(n, 4); strbuf_append_str_n(out, data, n); r.count++;
    }
    void array(Record& r, char code, uint32_t count, const void* data, size_t n, uint32_t encoding = 0) {
        strbuf_append_char(out, code); le(count, 4); le(encoding, 4); le(n, 4);
        strbuf_append_str_n(out, (const char*)data, n); r.count++;
    }
    // ends the property list; children follow, then close()
    size_t props_done(Record& r) { return out->length - r.props; }
    void close(Record& r, size_t props_length, bool sentinel) {
        if (sentinel) zeros(header());
        int w = wide ? 8 : 4;
        patch(r.start, out->length, w); patch(r.start + w, r.count, w); patch(r.start + 2 * w, props_length, w);
        out->str[r.start + 3 * w] = (char)r.name;
    }
    void leaf(const char* name, int32_t value) {
        Record r = open(name); scalar(r, 'I', (uint32_t)value, 4); close(r, props_done(r), false);
    }
    void end() { zeros(header()); }
};

TEST_F(ModelInputTest, FbxBinaryPreservesPropertyKindsAndRewritesObjectNames) {
    FbxTestWriter fbx;
    FbxTestWriter::Record r = fbx.open("Kinds");
    double real = 0.25; float single = 0.5f; uint32_t single_bits; memcpy(&single_bits, &single, 4);
    uint64_t real_bits; memcpy(&real_bits, &real, 8);
    fbx.scalar(r, 'C', 1, 1); fbx.scalar(r, 'Y', (uint16_t)-7, 2); fbx.scalar(r, 'I', (uint32_t)-70000, 4);
    fbx.scalar(r, 'F', single_bits, 4); fbx.scalar(r, 'D', real_bits, 8); fbx.scalar(r, 'L', 46186158000ull, 8);
    fbx.bytes(r, 'S', "mixamorig:Hips\0\1Model", 21); fbx.bytes(r, 'S', "", 0);
    fbx.bytes(r, 'R', "\x89PNG", 4); fbx.bytes(r, 'R', "", 0);
    int32_t ints[3] = {0, 1, -3}; int64_t longs[2] = {0, 1539538600}; uint8_t flags[3] = {0, 2, 1};
    double values[4] = {1.0, 2.0, 3.0, 4.0};
    fbx.array(r, 'i', 3, ints, sizeof(ints)); fbx.array(r, 'l', 2, longs, sizeof(longs));
    fbx.array(r, 'b', 3, flags, sizeof(flags));
    uLongf packed_length = compressBound(sizeof(values)); Bytef packed[256];
    ASSERT_EQ(compress(packed, &packed_length, (const Bytef*)values, sizeof(values)), Z_OK);
    fbx.array(r, 'd', 4, packed, packed_length, 1);
    size_t length = fbx.props_done(r);
    fbx.leaf("Child", 5);
    fbx.close(r, length, true); fbx.end();
    Input* input = parse(fbx.out->str, nullptr, "kinds.fbx", fbx.out->length);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    ElementReader root(input->root);
    EXPECT_TRUE(root.hasTag("fbx")); EXPECT_TRUE(root.get_attr("binary").asBool());
    EXPECT_EQ(root.get_attr("version").asInt(), 7400);
    ElementReader kinds = root.findChildElement("Kinds");
    ArrayReader props = kinds.get_attr("props").asArray();
    ASSERT_EQ(props.length(), 14);
    EXPECT_TRUE(props.get(0).asBool());
    Item i16 = props.get(1).item(), i32 = props.get(2).item(), f32 = props.get(3).item();
    EXPECT_EQ(NUM_SIZED_SUBTYPE(i16.item), NUM_INT16); EXPECT_EQ(props.get(1).asInt(), -7);
    EXPECT_EQ(NUM_SIZED_SUBTYPE(i32.item), NUM_INT32); EXPECT_EQ(props.get(2).asInt(), -70000);
    EXPECT_EQ(NUM_SIZED_SUBTYPE(f32.item), NUM_FLOAT32); EXPECT_DOUBLE_EQ(props.get(3).asFloat(), 0.5);
    EXPECT_DOUBLE_EQ(props.get(4).asFloat(), 0.25);
    EXPECT_EQ(props.get(5).getType(), LMD_TYPE_INT64); EXPECT_EQ(props.get(5).asInt(), 46186158000ll);
    EXPECT_STREQ(props.get(6).asString()->chars, "Model::mixamorig:Hips");
    EXPECT_TRUE(props.get(7).isNull());
    EXPECT_EQ(props.get(8).getType(), LMD_TYPE_BINARY); EXPECT_TRUE(props.get(9).isNull());
    const ArrayNumElemType kinds_expected[] = {ELEM_INT32, ELEM_INT64, ELEM_BOOL, ELEM_FLOAT64};
    for (int i = 0; i < 4; i++) {
        Item array = props.get(10 + i).item();
        ASSERT_EQ(get_type_id(array), LMD_TYPE_ARRAY_NUM);
        EXPECT_EQ(array.array_num->get_elem_type(), kinds_expected[i]);
    }
    EXPECT_EQ(props.get(10).asArray().get(2).asInt(), -3);
    EXPECT_TRUE(props.get(12).asArray().get(1).asBool());
    EXPECT_DOUBLE_EQ(props.get(13).asArray().get(3).asFloat(), 4.0);
    EXPECT_EQ(kinds.findChildElement("Child").get_attr("props").asArray().get(0).asInt(), 5);
}

TEST_F(ModelInputTest, FbxBinaryRejectsMalformedRecordsArraysAndVersions) {
    auto rejects = [&](FbxTestWriter& fbx, const char* reason, size_t length = SIZE_MAX) {
        Input* input = parse(fbx.out->str, "fbx", nullptr, length == SIZE_MAX ? fbx.out->length : length);
        ASSERT_NE(input, nullptr); EXPECT_TRUE(input->parse_failed) << reason;
        ASSERT_NE(input->parse_error_message, nullptr) << reason;
        EXPECT_NE(strstr(input->parse_error_message, reason), nullptr) << input->parse_error_message;
    };
    double values[4] = {1, 2, 3, 4};
    uLongf packed_length = compressBound(sizeof(values)); Bytef packed[256];
    ASSERT_EQ(compress(packed, &packed_length, (const Bytef*)values, sizeof(values)), Z_OK);
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.array(r, 'd', 3, packed, packed_length, 1);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "compressed array does not inflate to exactly its element count"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.array(r, 'd', 5, packed, packed_length, 1);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "compressed array does not inflate to exactly its element count"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.array(r, 'd', 3, values, sizeof(values));
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "raw array size does not match"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.array(r, 'd', 8388609, packed, packed_length, 1);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "array of 8388609 elements exceeds 8388608"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.array(r, 'd', 4, packed, packed_length, 2);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "unknown array encoding"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.scalar(r, 'Z', 0, 4);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "unknown property type code"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.scalar(r, 'I', 1, 4);
      fbx.close(r, fbx.props_done(r) + 1, false); fbx.end();
      rejects(fbx, "property list overruns the record"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.scalar(r, 'I', 1, 4); fbx.scalar(r, 'I', 2, 4);
      size_t length = fbx.props_done(r); r.count = 1; fbx.close(r, length, false); fbx.end();
      rejects(fbx, "property list length does not match"); }
    { FbxTestWriter fbx; FbxTestWriter::Record r = fbx.open("A"); fbx.bytes(r, 'S', "abc", 3);
      fbx.patch(r.props + 1, 400, 4); fbx.close(r, fbx.props_done(r), false); fbx.end();
      rejects(fbx, "string data overruns its record"); }
    { // a child whose end offset lies past its parent
      FbxTestWriter fbx; FbxTestWriter::Record parent = fbx.open("Parent"); size_t length = fbx.props_done(parent);
      FbxTestWriter::Record child = fbx.open("Child"); fbx.scalar(child, 'I', 1, 4); fbx.close(child, fbx.props_done(child), false);
      fbx.close(parent, length, true); fbx.patch(child.start, fbx.out->length + 10, 4); fbx.end();
      rejects(fbx, "record end offset outside its parent"); }
    { // nested records that stop short of their parent's end
      FbxTestWriter fbx; FbxTestWriter::Record parent = fbx.open("Parent"); size_t length = fbx.props_done(parent);
      fbx.leaf("Child", 1); fbx.close(parent, length, true); fbx.zeros(4);
      fbx.patch(parent.start, fbx.out->length, 4); fbx.end();
      rejects(fbx, "nested records do not end at the record end"); }
    { // 65 levels of nesting
      FbxTestWriter fbx; FbxTestWriter::Record levels[65]; size_t lengths[65];
      for (int i = 0; i < 65; i++) { levels[i] = fbx.open("N"); lengths[i] = fbx.props_done(levels[i]); }
      for (int i = 64; i >= 0; i--) fbx.close(levels[i], lengths[i], true);
      fbx.end();
      rejects(fbx, "records nest deeper than 64"); }
    { FbxTestWriter fbx; fbx.leaf("A", 1); fbx.end();
      for (size_t cut : {(size_t)20, (size_t)30, fbx.out->length - 14, fbx.out->length - 1}) {
          SCOPED_TRACE(cut);
          Input* input = parse(fbx.out->str, "fbx", nullptr, cut);
          ASSERT_NE(input, nullptr); EXPECT_TRUE(input->parse_failed);
      } }
    { FbxTestWriter fbx(6100); fbx.end(); rejects(fbx, "FBX 6.x and older use another object model"); }
    { FbxTestWriter fbx(7800); fbx.end(); rejects(fbx, "newer than FBX 7.7"); }
    { FbxTestWriter fbx(7500); FbxTestWriter::Record r = fbx.open("Wide"); fbx.scalar(r, 'I', 9, 4);
      fbx.close(r, fbx.props_done(r), false); fbx.end();
      Input* input = parse(fbx.out->str, "fbx", nullptr, fbx.out->length);
      ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
      EXPECT_EQ(ElementReader(input->root).findChildElement("Wide").get_attr("props").asArray().get(0).asInt(), 9); }
}

TEST_F(ModelInputTest, FbxAsciiMapsNumbersArraysAndBareWordsAndRejectsMalformedText) {
    const char* header = "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXVersion: 7400\n}\n";
    char text[512];
    snprintf(text, sizeof(text), "%sObjects:  {\n\tGeometry: 7, \"Geometry::Body\", \"Mesh\" {\n"
        "\t\tVertices: *4 {\n\t\t\ta: 0,1,\n\t\t\t2.5,-3e1\n\t\t} \n\t\tShading: T ; comment\n\t}\n}\n", header);
    Input* input = parse(text, nullptr, "scene.fbx");
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    ElementReader root(input->root);
    EXPECT_FALSE(root.get_attr("binary").asBool()); EXPECT_EQ(root.get_attr("version").asInt(), 7400);
    ElementReader geometry = root.findChildElement("Objects").findChildElement("Geometry");
    EXPECT_EQ(geometry.get_attr("props").asArray().get(0).getType(), LMD_TYPE_INT);
    Item vertices = geometry.findChildElement("Vertices").get_attr("props").asArray().get(0).item();
    ASSERT_EQ(get_type_id(vertices), LMD_TYPE_ARRAY_NUM);
    EXPECT_EQ(vertices.array_num->get_elem_type(), ELEM_FLOAT64);
    EXPECT_DOUBLE_EQ(ItemReader(vertices.to_const()).asArray().get(3).asFloat(), -30.0);
    EXPECT_TRUE(geometry.findChildElement("Shading").get_attr("props").asArray().get(0).isSymbol());
    static const struct { const char* body; const char* reason; } invalid[] = {
        {"A: *3 {\n a: 1,2\n}\n", "array has fewer values than its length"},
        {"A: *1 {\n a: 1,2\n}\n", "array has more values than its length"},
        {"A: 1.#INF\n", "malformed number"},
        {"A: \"open\n", "unterminated string"},
        {"A:  {\n B: 1\n", "unterminated record body"},
        {"A: 1 2\n", "unexpected text after a record"},
        {"}\n", "unmatched '}'"},
    };
    for (const auto& c : invalid) {
        SCOPED_TRACE(c.body);
        snprintf(text, sizeof(text), "%s%s", header, c.body);
        Input* bad = parse(text, "fbx");
        ASSERT_NE(bad, nullptr); EXPECT_TRUE(bad->parse_failed);
        ASSERT_NE(bad->parse_error_message, nullptr);
        EXPECT_NE(strstr(bad->parse_error_message, c.reason), nullptr) << bad->parse_error_message;
    }
    Input* unversioned = parse("; FBX 7.4.0 project file\nObjects:  {\n}\n", "fbx");
    ASSERT_NE(unversioned, nullptr); EXPECT_TRUE(unversioned->parse_failed);
    EXPECT_NE(strstr(unversioned->parse_error_message, "missing FBXHeaderExtension FBXVersion"), nullptr);
}

TEST_F(ModelInputTest, FbxDetectionUsesTheBinarySignatureAndTheAsciiHeader) {
    MimeDetector* detector = mime_detector_init();
    EXPECT_STREQ(detect_mime_from_filename(detector, "Character.FBX"), "model/x-fbx");
    EXPECT_STREQ(detect_mime_from_content(detector, "Kaydara FBX Binary  \0\x1a\0\xe8\x1c\0\0", 27), "model/x-fbx");
    EXPECT_STREQ(detect_mime_from_content(detector, "; FBX 7.4.0 project file\n", 25), "model/x-fbx");
    EXPECT_STRNE(detect_mime_from_content(detector, "Kaydara FBX Binary  X", 21), "model/x-fbx");
    EXPECT_STREQ(mime_extension_from_content_type("model/x-fbx"), ".fbx");
    mime_detector_destroy(detector);
    FbxTestWriter fbx; fbx.leaf("A", 1); fbx.end();
    Input* input = parse(fbx.out->str, nullptr, nullptr, fbx.out->length);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    EXPECT_TRUE(ElementReader(input->root).hasTag("fbx"));
}

// ---- EXT_meshopt_compression, KHR texture/emission/light extensions, STL, PLY, glTF export ----

// a mode-3 (raw delta) attribute stream with a zero baseline: valid for any input
static void meshopt_test_attributes(StrBuf* out, const uint8_t* data, size_t count, size_t stride) {
    strbuf_append_char(out, (char)0xa0);
    size_t block = (8192 / stride) & ~(size_t)15; if (block > 256) block = 256;
    uint8_t last[256] = {};
    for (size_t offset = 0; offset < count; offset += block) {
        size_t n = count - offset < block ? count - offset : block, groups = (n + 15) / 16;
        for (size_t k = 0; k < stride; k++) {
            for (size_t h = 0; h < (groups + 3) / 4; h++) strbuf_append_char(out, (char)0xff);
            for (size_t i = 0; i < groups * 16; i++) {
                uint8_t delta = i < n ? (uint8_t)(data[(offset + i) * stride + k] - (i ? data[(offset + i - 1) * stride + k] : last[k])) : 0;
                strbuf_append_char(out, (char)(uint8_t)((delta << 1) ^ (uint8_t)((int8_t)delta >> 7)));
            }
            last[k] = data[(offset + n - 1) * stride + k];
        }
    }
    size_t tail = stride < 32 ? 32 : stride;
    for (size_t i = 0; i < tail; i++) strbuf_append_char(out, 0);
}

TEST_F(ModelInputTest, MeshoptAttributeGroupsDecodeEveryModeAndEscape) {
    // lane 0 raw, lane 1 4-bit with an escaped 40, lane 2 2-bit with an escaped 60, lane 3 raw.
    StrBuf* s = strbuf_new(); strbuf_append_char(s, (char)0xa0);
    const uint8_t lane0[] = {3, 20, 2}, lane1[] = {2, 0xf3, 0, 0, 0, 0, 0, 0, 0, 40}, lane2[] = {1, 0xc0, 0, 0, 0, 60}, lane3[] = {3, 80, 81};
    strbuf_append_str_n(s, (const char*)lane0, 3); for (int i = 0; i < 14; i++) strbuf_append_char(s, 0);
    strbuf_append_str_n(s, (const char*)lane1, sizeof(lane1)); strbuf_append_str_n(s, (const char*)lane2, sizeof(lane2));
    strbuf_append_str_n(s, (const char*)lane3, 3); for (int i = 0; i < 14; i++) strbuf_append_char(s, 0);
    for (int i = 0; i < 32; i++) strbuf_append_char(s, 0);
    uint8_t out[8] = {};
    EXPECT_EQ(lambda::meshopt_decode(out, 2, 4, (const uint8_t*)s->str, s->length, "ATTRIBUTES", nullptr), nullptr);
    const uint8_t expected[] = {10, 20, 30, 40, 11, 18, 30, 255};
    EXPECT_EQ(memcmp(out, expected, 8), 0);
    // a missing tail byte or an unknown codec version is rejected.
    EXPECT_NE(lambda::meshopt_decode(out, 2, 4, (const uint8_t*)s->str, s->length - 1, "ATTRIBUTES", nullptr), nullptr);
    s->str[0] = (char)0xa1; EXPECT_NE(lambda::meshopt_decode(out, 2, 4, (const uint8_t*)s->str, s->length, "ATTRIBUTES", nullptr), nullptr);
    strbuf_free(s);
}

TEST_F(ModelInputTest, MeshoptAttributesCarryTheBaselineAcrossBlocks) {
    // 600 elements of stride 12 span three 256-element blocks.
    const size_t count = 600, stride = 12; uint8_t source[count * stride], out[count * stride];
    for (size_t i = 0; i < sizeof(source); i++) source[i] = (uint8_t)(i * 37 + (i / stride) * 11);
    StrBuf* s = strbuf_new(); meshopt_test_attributes(s, source, count, stride);
    EXPECT_EQ(lambda::meshopt_decode(out, count, stride, (const uint8_t*)s->str, s->length, "ATTRIBUTES", "NONE"), nullptr);
    EXPECT_EQ(memcmp(out, source, sizeof(source)), 0);
    strbuf_free(s);
}

TEST_F(ModelInputTest, MeshoptTrianglesFollowEdgeAndVertexFifos) {
    // codes: fe(00) reset, edge+next, edge+last+1, edge+free index, codeaux table, ff with three free indices.
    const uint8_t stream[] = {0xe1, 0xfe, 0x10, 0x0e, 0x0f, 0xf0, 0xff, 0x00, 0x08, 0xff, 0x04, 0x02, 0x02,
        0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t out[18] = {};
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)out, 18, 4, stream, sizeof(stream), "TRIANGLES", nullptr), nullptr);
    const uint32_t expected[] = {0, 1, 2, 2, 1, 3, 2, 3, 1, 2, 1, 5, 4, 5, 1, 7, 8, 9};
    for (unsigned i = 0; i < 18; i++) EXPECT_EQ(out[i], expected[i]) << i;
    uint16_t narrow[18] = {};
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)narrow, 18, 2, stream, sizeof(stream), "TRIANGLES", nullptr), nullptr);
    EXPECT_EQ(narrow[17], 9);
    // trailing data, a non-multiple-of-three count and a filter are all invalid.
    uint8_t padded[sizeof(stream) + 1]; memcpy(padded, stream, 13); padded[13] = 0; memcpy(padded + 14, stream + 13, 16);
    EXPECT_NE(lambda::meshopt_decode((uint8_t*)out, 18, 4, padded, sizeof(padded), "TRIANGLES", nullptr), nullptr);
    EXPECT_NE(lambda::meshopt_decode((uint8_t*)out, 17, 4, stream, sizeof(stream), "TRIANGLES", nullptr), nullptr);
    EXPECT_NE(lambda::meshopt_decode((uint8_t*)out, 18, 4, stream, sizeof(stream), "TRIANGLES", "OCTAHEDRAL"), nullptr);
}

TEST_F(ModelInputTest, MeshoptIndexSequencesUseTwoBaselines) {
    const uint8_t stream[] = {0xd1, 20, 4, 0x91, 0x03, 4, 0, 0, 0, 0};
    uint32_t out[4] = {};
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)out, 4, 4, stream, sizeof(stream), "INDICES", nullptr), nullptr);
    EXPECT_EQ(out[0], 5u); EXPECT_EQ(out[1], 6u); EXPECT_EQ(out[2], 100u); EXPECT_EQ(out[3], 7u);
    EXPECT_NE(lambda::meshopt_decode((uint8_t*)out, 5, 4, stream, sizeof(stream), "INDICES", nullptr), nullptr);
}

TEST_F(ModelInputTest, MeshoptFiltersReconstructNormalsQuaternionsAndFloats) {
    // octahedral: +Z, +X, and a folded lower-hemisphere direction.
    StrBuf* s = strbuf_new();
    const int8_t oct[] = {0, 0, 127, 5, 127, 0, 127, -7, -100, 50, 127, 0};
    meshopt_test_attributes(s, (const uint8_t*)oct, 3, 4);
    int8_t normals[12];
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)normals, 3, 4, (const uint8_t*)s->str, s->length, "ATTRIBUTES", "OCTAHEDRAL"), nullptr);
    const int8_t expected_oct[] = {0, 0, 127, 5, 127, 0, 0, -7, -115, 40, -34, 0};
    for (unsigned i = 0; i < 12; i++) EXPECT_EQ(normals[i], expected_oct[i]) << i;
    // quaternion: largest w with x = 1/sqrt2, and largest x stored in slot 0.
    strbuf_reset(s);
    const int16_t quat[] = {8191, 0, 0, 0x1fff, 8191, 0, 0, 0x1ffc};
    meshopt_test_attributes(s, (const uint8_t*)quat, 2, 8);
    int16_t q[8];
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)q, 2, 8, (const uint8_t*)s->str, s->length, "ATTRIBUTES", "QUATERNION"), nullptr);
    EXPECT_EQ(q[0], 23170); EXPECT_EQ(q[1], 0); EXPECT_EQ(q[2], 0); EXPECT_EQ(q[3], 23170);
    EXPECT_EQ(q[4], 23170); EXPECT_EQ(q[5], 23170); EXPECT_EQ(q[6], 0); EXPECT_EQ(q[7], 0);
    // exponential: 6 * 2^-2 and -3 * 2^1.
    strbuf_reset(s);
    const uint32_t exp[] = {0xfe000006u, 0x01fffffdu};
    meshopt_test_attributes(s, (const uint8_t*)exp, 1, 8);
    float f[2];
    ASSERT_EQ(lambda::meshopt_decode((uint8_t*)f, 1, 8, (const uint8_t*)s->str, s->length, "ATTRIBUTES", "EXPONENTIAL"), nullptr);
    EXPECT_FLOAT_EQ(f[0], 1.5f); EXPECT_FLOAT_EQ(f[1], -6.f);
    EXPECT_NE(lambda::meshopt_decode((uint8_t*)f, 1, 8, (const uint8_t*)s->str, s->length, "ATTRIBUTES", "OCTAGONAL"), nullptr);
    strbuf_free(s);
}

static StrBuf* meshopt_test_model(bool required) {
    // compressed positions and triangle indices in buffer 0; buffer 1 is the fallback.
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    StrBuf* data = strbuf_new(); meshopt_test_attributes(data, (const uint8_t*)positions, 3, 12);
    size_t attributes = data->length;
    const uint8_t triangles[] = {0xe1, 0xfe, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    strbuf_append_str_n(data, (const char*)triangles, sizeof(triangles));
    char* encoded = base64_encode_alloc((const uint8_t*)data->str, data->length, BASE64_STD);
    StrBuf* json = strbuf_new();
    strbuf_append_format(json, R"({"asset":{"version":"2.0"},"extensionsUsed":["EXT_meshopt_compression"],%s
        "buffers":[{"byteLength":%zu,"uri":"data:application/octet-stream;base64,%s"},
            {"byteLength":44,"extensions":{"EXT_meshopt_compression":{"fallback":true}}}],
        "bufferViews":[{"buffer":1,"byteLength":36,"byteStride":12,"extensions":{"EXT_meshopt_compression":
            {"buffer":0,"byteLength":%zu,"byteStride":12,"count":3,"mode":"ATTRIBUTES"}}},
            {"buffer":1,"byteOffset":36,"byteLength":6,"extensions":{"EXT_meshopt_compression":
            {"buffer":0,"byteOffset":%zu,"byteLength":%zu,"byteStride":2,"count":3,"mode":"TRIANGLES"}}}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}]})",
        required ? R"("extensionsRequired":["EXT_meshopt_compression"],)" : "",
        data->length, encoded, attributes, attributes, sizeof(triangles));
    mem_free(encoded); strbuf_free(data); return json;
}

TEST_F(ModelInputTest, SceneAssetDecodesMeshoptViewsInsteadOfFallbackBuffers) {
    for (bool required : {true, false}) {
        SCOPED_TRACE(required);
        StrBuf* json = meshopt_test_model(required);
        Input* input = parse(json->str, "scene3d-asset", "meshopt.gltf"); strbuf_free(json);
        ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
        auto mesh = ElementReader(input->root).findChildElement("group").findChildElement("group").findChildElement("mesh");
        auto positions = mesh.findChildElement("geometry").get_attr("positions").asArray();
        ASSERT_EQ(positions.length(), 9);
        EXPECT_DOUBLE_EQ(positions.get(3).asFloat(), 1); EXPECT_DOUBLE_EQ(positions.get(7).asFloat(), 1);
    }
    // an unknown meshopt mode is refused.
    StrBuf* json = meshopt_test_model(true);
    char* compressed = strstr(json->str, "\"mode\":\"ATTRIBUTES\"");
    ASSERT_NE(compressed, nullptr); memcpy(compressed, "\"mode\":\"ATTRIBUTEX\"", 19);
    Input* bad = parse(json->str, "scene3d-asset", "meshopt-bad.gltf"); strbuf_free(json);
    ASSERT_NE(bad, nullptr); EXPECT_TRUE(bad->parse_failed);
}

static const char* textured_extension_model = R"({"asset":{"version":"2.0"},
    "extensionsUsed":["KHR_texture_transform","KHR_materials_emissive_strength","KHR_lights_punctual"],
    "extensions":{"KHR_lights_punctual":{"lights":[{"type":"directional","intensity":2},
        {"type":"point","color":[1,0,0],"range":5,"name":"lamp"},
        {"type":"spot","spot":{"innerConeAngle":0.2,"outerConeAngle":0.4}}]}},
    "buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],
    "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],
    "images":[{"uri":"data:image/png;base64,AAAA"}],"textures":[{"source":0}],
    "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"extensions":{"KHR_texture_transform":{"offset":[0.5,0],"scale":[2,2]}}}},
        "emissiveFactor":[1,0,0],"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":3}}}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],
    "nodes":[{"mesh":0},{"extensions":{"KHR_lights_punctual":{"light":0}}},
        {"extensions":{"KHR_lights_punctual":{"light":1}},"translation":[0,2,0]},{"extensions":{"KHR_lights_punctual":{"light":2}}}]})";

TEST_F(ModelInputTest, SceneAssetBakesTextureTransformsAndMapsEmissionAndPunctualLights) {
    Input* input = parse(textured_extension_model, "scene3d-asset", "extensions.gltf");
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    ElementReader root(input->root);
    ElementReader geometry, material = root.findChildElement("resources").findChildElement("material"), lights[3];
    unsigned light_count = 0;
    auto visit = [&](auto&& self, ElementReader e) -> void {
        for (int64_t i = 0; i < e.childCount(); i++) {
            if (!e.childAt(i).isElement()) continue;
            ElementReader child = e.childAt(i).asElement();
            if (child.hasTag("geometry")) geometry = child;
            if (child.hasTag("light") && light_count < 3) lights[light_count++] = child;
            self(self, child);
        }
    };
    visit(visit, root);
    // uv' = (2u + 0.5, 2v), then the native lower-left flip.
    auto uv = geometry.get_attr("uvs").asArray(); ASSERT_EQ(uv.length(), 6);
    EXPECT_DOUBLE_EQ(uv.get(0).asFloat(), .5); EXPECT_DOUBLE_EQ(uv.get(1).asFloat(), 1);
    EXPECT_DOUBLE_EQ(uv.get(2).asFloat(), 2.5); EXPECT_DOUBLE_EQ(uv.get(5).asFloat(), -1);
    EXPECT_STREQ(material.get_attr("emissive").cstring(), "#ff0000");
    EXPECT_DOUBLE_EQ(material.get_attr("emissive-intensity").asFloat(), 3);
    ASSERT_EQ(light_count, 3u);
    EXPECT_STREQ(lights[0].get_attr("type").cstring(), "directional");
    EXPECT_DOUBLE_EQ(lights[0].get_attr("intensity").asFloat(), 2);
    EXPECT_EQ(lights[0].get_attr("target").asArray().get(2).asFloat(), -1);
    EXPECT_STREQ(lights[1].get_attr("color").cstring(), "#ff0000");
    EXPECT_DOUBLE_EQ(lights[1].get_attr("distance").asFloat(), 5);
    EXPECT_TRUE(lights[1].get_attr("target").isNull());
    EXPECT_STREQ(lights[1].get_attr("name").cstring(), "lamp");
    EXPECT_DOUBLE_EQ(lights[2].get_attr("angle").asFloat(), .4);
    EXPECT_DOUBLE_EQ(lights[2].get_attr("penumbra").asFloat(), .5);
}

TEST_F(ModelInputTest, SceneAssetOmitsTexturedEmissionAndRejectsInvalidExtensionValues) {
    StrBuf* source = strbuf_new(); strbuf_append_str(source, textured_extension_model);
    char* factor = strstr(source->str, "\"emissiveFactor\""); ASSERT_NE(factor, nullptr);
    StrBuf* textured = strbuf_new(); strbuf_append_str_n(textured, source->str, factor - source->str);
    strbuf_append_str(textured, "\"emissiveTexture\":{\"index\":0},"); strbuf_append_str(textured, factor);
    Input* input = parse(textured->str, "scene3d-asset", "textured-emission.gltf");
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    EXPECT_TRUE(ElementReader(input->root).findChildElement("resources").findChildElement("material").get_attr("emissive").isNull());
    const char* replacements[][2] = {{"\"outerConeAngle\":0.4", "\"outerConeAngle\":0.1"}, {"\"range\":5", "\"range\":0"},
        {"\"scale\":[2,2]", "\"scale\":[2]"}, {"\"type\":\"point\"", "\"type\":\"area\""}, {"\"light\":2", "\"light\":9"}};
    for (auto& r : replacements) {
        SCOPED_TRACE(r[1]); StrBuf* bad = strbuf_new(); char* at = strstr(source->str, r[0]); ASSERT_NE(at, nullptr);
        strbuf_append_str_n(bad, source->str, at - source->str); strbuf_append_str(bad, r[1]); strbuf_append_str(bad, at + strlen(r[0]));
        Input* failed = parse(bad->str, "scene3d-asset", "bad-extension.gltf"); strbuf_free(bad);
        ASSERT_NE(failed, nullptr); EXPECT_TRUE(failed->parse_failed);
    }
    strbuf_free(textured); strbuf_free(source);
}

static StrBuf* binary_stl(unsigned facets, const char* header = "test part") {
    StrBuf* out = strbuf_new(); char head[80] = {}; strncpy(head, header, 79); strbuf_append_str_n(out, head, 80);
    glb_test_word(out, facets);
    for (unsigned f = 0; f < facets; f++) {
        float values[12] = {0, 0, 1, (float)f, 0, 0, (float)f + 1, 0, 0, (float)f, 1, 0};
        for (float v : values) { uint32_t bits; memcpy(&bits, &v, 4); glb_test_word(out, bits); }
        strbuf_append_char(out, (char)(f == 1 ? 7 : 0)); strbuf_append_char(out, 0);
    }
    return out;
}

TEST_F(ModelInputTest, StlAsciiAndBinaryMapToTypedFacetArrays) {
    ElementReader ascii = parsed("solid wedge\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n"
        "  endloop\n endfacet\nendsolid wedge\nsolid second\nendsolid\n", "stl");
    EXPECT_STREQ(ascii.get_attr("format").cstring(), "ascii"); EXPECT_STREQ(ascii.get_attr("name").cstring(), "wedge");
    EXPECT_EQ(ascii.get_attr("solids").asInt(), 2);
    EXPECT_EQ(ascii.get_attr("positions").asArray().length(), 3);
    EXPECT_DOUBLE_EQ(ascii.get_attr("normals").asArray().get(0).asArray().get(2).asFloat(), 1);
    StrBuf* stl = binary_stl(3);
    Input* input = parse(stl->str, "stl", nullptr, stl->length);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
    ElementReader binary(input->root);
    EXPECT_STREQ(binary.get_attr("format").cstring(), "binary"); EXPECT_STREQ(binary.get_attr("header").cstring(), "test part");
    EXPECT_EQ(binary.get_attr("positions").asArray().length(), 9);
    EXPECT_EQ(binary.get_attr("attributes").asArray().get(1).asInt(), 7);
    // a header that starts like ASCII must not hide a size-consistent binary body.
    strbuf_free(stl); stl = binary_stl(2, "solid binary");
    EXPECT_FALSE(parse(stl->str, "stl", nullptr, stl->length)->parse_failed);
    EXPECT_TRUE(parse(stl->str, "stl", nullptr, stl->length - 1)->parse_failed);
    strbuf_free(stl);
    const char* invalid[] = {"solid x\n facet normal 0 0 1\n outer loop\n vertex 0 0\n", "solid x\n facet normal 0 0 nan\n",
        "solid x\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n vertex 0 1 0\n vertex 1 1 0\n endloop\n endfacet\nendsolid\n",
        "polygon\n"};
    for (const char* source : invalid) { SCOPED_TRACE(source); EXPECT_TRUE(parse(source, "stl")->parse_failed); }
}

TEST_F(ModelInputTest, PlyAsciiAndBinaryKeepPropertyTypesListsAndComments) {
    ElementReader ascii = parsed("ply\nformat ascii 1.0\ncomment hello\nobj_info scan 3\nelement vertex 2\nproperty float x\n"
        "property uchar red\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0.5 255\n1 7\n3 0 1 1\n", "ply");
    EXPECT_STREQ(ascii.get_attr("format").cstring(), "ascii");
    EXPECT_STREQ(ascii.get_attr("comments").asArray().get(0).cstring(), "hello");
    EXPECT_STREQ(ascii.get_attr("obj_info").asArray().get(0).cstring(), "scan 3");
    ElementReader vertex = ascii.childAt(0).asElement(), face = ascii.childAt(1).asElement();
    EXPECT_TRUE(vertex.hasTag("vertex")); EXPECT_EQ(vertex.get_attr("count").asInt(), 2);
    EXPECT_DOUBLE_EQ(vertex.get_attr("x").asArray().get(0).asFloat(), .5);
    EXPECT_EQ(vertex.get_attr("red").asArray().get(1).asInt(), 7);
    EXPECT_EQ(face.get_attr("vertex_indices").asArray().get(0).asArray().get(2).asInt(), 1);
    for (bool big : {false, true}) {
        SCOPED_TRACE(big);
        StrBuf* ply = strbuf_new();
        strbuf_append_format(ply, "ply\nformat binary_%s_endian 1.0\nelement vertex 1\nproperty short x\nproperty double y\n"
            "element face 1\nproperty list uchar ushort vertex_indices\nend_header\n", big ? "big" : "little");
        const uint8_t le[] = {0xfe, 0xff, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f, 2, 0x34, 0x12, 1, 0},
            be[] = {0xff, 0xfe, 0x3f, 0xf8, 0, 0, 0, 0, 0, 0, 2, 0x12, 0x34, 0, 1};
        strbuf_append_str_n(ply, (const char*)(big ? be : le), sizeof(le));
        Input* input = parse(ply->str, "ply", nullptr, ply->length);
        ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed) << input->parse_error_message;
        ElementReader root(input->root);
        EXPECT_EQ(root.childAt(0).asElement().get_attr("x").asArray().get(0).asInt(), -2);
        EXPECT_DOUBLE_EQ(root.childAt(0).asElement().get_attr("y").asArray().get(0).asFloat(), 1.5);
        auto list = root.childAt(1).asElement().get_attr("vertex_indices").asArray().get(0).asArray();
        EXPECT_EQ(list.get(0).asInt(), 0x1234); EXPECT_EQ(list.get(1).asInt(), 1);
        // truncating the last list item is an error.
        EXPECT_TRUE(parse(ply->str, "ply", nullptr, ply->length - 1)->parse_failed);
        strbuf_free(ply);
    }
    const char* invalid[] = {"ply\nformat ascii 2.0\nend_header\n", "ply\nformat ascii 1.0\nelement vertex 1\nproperty float count\nend_header\n1\n",
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty uchar x\nend_header\n256\n", "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\n",
        "ply\nformat ascii 1.0\nelement vertex 2000000000\nend_header\n", "obj\n"};
    for (const char* source : invalid) { SCOPED_TRACE(source); EXPECT_TRUE(parse(source, "ply")->parse_failed); }
    MimeDetector* detector = mime_detector_init();
    EXPECT_STREQ(detect_mime_from_filename(detector, "scan.ply"), "model/x-ply");
    EXPECT_STREQ(detect_mime_from_filename(detector, "part.stl"), "model/stl");
    mime_detector_destroy(detector);
}

TEST_F(ModelInputTest, SceneAssetConvertsStlFacetsAndPlyPolygonsWithColorsAndNormals) {
    StrBuf* stl = binary_stl(2);
    Input* part = parse(stl->str, "scene3d-asset", "part.stl", stl->length); strbuf_free(stl);
    ASSERT_NE(part, nullptr); ASSERT_FALSE(part->parse_failed) << part->parse_error_message;
    auto geometry = ElementReader(part->root).findChildElement("mesh").findChildElement("geometry");
    // facet normals come from the winding, not the stored (0,0,1).
    EXPECT_EQ(geometry.get_attr("positions").asArray().length(), 18);
    EXPECT_DOUBLE_EQ(geometry.get_attr("normals").asArray().get(2).asFloat(), 1);
    Input* quad = parse("ply\nformat ascii 1.0\ncomment TextureFile data:image/png;base64,AAAA\nelement vertex 4\nproperty float x\nproperty float y\n"
        "property float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nproperty float s\nproperty float t\n"
        "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
        "0 0 0 255 0 0 0 0\n1 0 0 0 255 0 1 0\n1 1 0 0 0 255 1 1\n0 1 0 255 255 255 0 1\n4 0 1 2 3\n3 0 0 1\n", "scene3d-asset", "quad.ply");
    ASSERT_NE(quad, nullptr); ASSERT_FALSE(quad->parse_failed) << quad->parse_error_message;
    ElementReader root(quad->root);
    auto mesh = root.findChildElement("mesh"); geometry = mesh.findChildElement("geometry");
    // the quad triangulates to six indices; the zero-area face is dropped.
    EXPECT_EQ(geometry.get_attr("indices").asArray().length(), 6);
    EXPECT_EQ(geometry.get_attr("colors").asArray().length(), 12);
    EXPECT_EQ(geometry.get_attr("uvs").asArray().length(), 8);
    EXPECT_DOUBLE_EQ(geometry.get_attr("normals").asArray().get(2).asFloat(), 1);
    EXPECT_FALSE(root.findChildElement("resources").findChildElement("texture").get_attr("src").isNull());
    const char* invalid[] = {"ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n",
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n0 0 0\n3 0 0 4\n",
        "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nproperty float red\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0 1\n1 0 0 1\n0 1 0 1\n3 0 1 2\n"};
    for (const char* source : invalid) { SCOPED_TRACE(source); EXPECT_TRUE(parse(source, "scene3d-asset", "invalid.ply")->parse_failed); }
}

TEST_F(ModelInputTest, GltfExportRoundTripsImportedGeometryMaterialsAndLights) {
    Input* source = parse(textured_extension_model, "scene3d-asset", "extensions.gltf");
    ASSERT_NE(source, nullptr); ASSERT_FALSE(source->parse_failed) << source->parse_error_message;
    for (bool binary : {false, true}) {
        SCOPED_TRACE(binary);
        StrBuf* out = strbuf_new(); const char* error = nullptr;
        ASSERT_TRUE(format_gltf_bytes(source->root, binary, out, &error)) << error;
        if (binary) { ASSERT_GE(out->length, 20u); EXPECT_EQ(memcmp(out->str, "glTF", 4), 0); EXPECT_EQ(out->length % 4, 0u); }
        else EXPECT_NE(strstr(out->str, "\"KHR_lights_punctual\""), nullptr);
        Input* again = parse(out->str, "scene3d-asset", binary ? "export.glb" : "export.gltf", out->length); strbuf_free(out);
        ASSERT_NE(again, nullptr); ASSERT_FALSE(again->parse_failed) << again->parse_error_message;
        // nodes nest one level deeper under the exported root group.
        ElementReader root(again->root), material = root.findChildElement("resources").findChildElement("material");
        EXPECT_STREQ(material.get_attr("emissive").cstring(), "#ff0000");
        EXPECT_DOUBLE_EQ(material.get_attr("emissive-intensity").asFloat(), 3);
        unsigned lights = 0; ElementReader geometry;
        auto visit = [&](auto&& self, ElementReader e) -> void {
            for (int64_t i = 0; i < e.childCount(); i++) {
                if (!e.childAt(i).isElement()) continue;
                ElementReader child = e.childAt(i).asElement();
                if (child.hasTag("light")) lights++;
                if (child.hasTag("geometry")) geometry = child;
                self(self, child);
            }
        };
        visit(visit, root);
        EXPECT_EQ(lights, 3u);
        ASSERT_TRUE(geometry.isValid());
        auto uv = geometry.get_attr("uvs").asArray(); ASSERT_EQ(uv.length(), 6);
        EXPECT_NEAR(uv.get(2).asFloat(), 2.5, 1e-6); EXPECT_NEAR(uv.get(5).asFloat(), -1, 1e-6);
    }
}

TEST_F(ModelInputTest, GltfExportRefusesSkinsAndAnimationClips) {
    Input* rigged = load_scene_asset("test/demo/scene3d/assets/loading/articulated.gltf");
    ASSERT_NE(rigged, nullptr); ASSERT_FALSE(rigged->parse_failed);
    StrBuf* out = strbuf_new(); const char* error = nullptr;
    EXPECT_FALSE(format_gltf_bytes(rigged->root, false, out, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(strstr(error, "skin") || strstr(error, "skeleton") || strstr(error, "animation")) << error;
    strbuf_free(out);
}
