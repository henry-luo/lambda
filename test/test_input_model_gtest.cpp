#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/mime-detect.h"
#include "../lib/url.h"

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
        Input* input = input_from_source_n(source, length == SIZE_MAX ? strlen(source) : length,
            filename ? url_parse(filename) : nullptr, format, nullptr);
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
