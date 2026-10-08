#include <gtest/gtest.h>
#include "../radiant/scene3d.hpp"
#include "../radiant/scene3d_math.hpp"
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/input/input.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/module/radiant/radiant_dom_bridge.hpp"
#include "../lambda/module/radiant/radiant_webgl_bridge.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"

class Scene3dTest : public ::testing::Test {
protected:
    UiContext ui{};
    Input input{};
    DomDocument doc{};
    DomElement* root=nullptr;
    DomElement* camera=nullptr;
    DomDocument* page=nullptr;
    void SetUp() override {
        // rendered tests explicitly opt into native graphics; absence is a failure.
        setenv("LAMBDA_HEADLESS_GLFW_WINDOW","1",1);
        ASSERT_EQ(ui_context_init(&ui,true,1),0);
        ASSERT_TRUE(doc.init(&input));
        doc.url=lam::own(get_current_dir());ASSERT_NE(doc.url,nullptr);
        root=element("scene3d"); ASSERT_NE(root,nullptr);doc.root=lam::up(root);
        ui.document=lam::up(&doc);doc.js.host_ui_context=&ui;
        camera=element("camera",root);ASSERT_NE(camera,nullptr);
        attr(camera,"type","perspective");attr(camera,"id","main");attr(camera,"position","0 0 4");attr(camera,"target","0 0 0");attr(camera,"fov","90");
        attr(root,"camera","main");
    }
    void TearDown() override {
        ui.document=nullptr;if (page) free_document(page);url_destroy(doc.url);doc.url=nullptr;doc.destroy();ui_context_cleanup(&ui);
        unsetenv("LAMBDA_HEADLESS_GLFW_WINDOW");
    }
    DomElement* element(const char* tag, DomElement* parent=nullptr) {
        DomElement* result=DomElement::create(&doc,tag,nullptr);
        if (result && parent) EXPECT_TRUE(parent->append_child(result));
        return result;
    }
    void attr(DomElement* node,const char* name,const char* value) {
        ASSERT_TRUE(node->set_attribute(name,value));doc.mutation_epoch++;
    }
    DomElement* mesh(const char* geometry="plane",const char* color="#ff0000",const char* material="basic",DomElement* parent=nullptr) {
        DomElement* result=element("mesh",parent?parent:root);
        DomElement* g=element("geometry",result);attr(g,"type",geometry);attr(g,"size","2 2 2");
        DomElement* m=element("material",result);attr(m,"type",material);attr(m,"color",color);
        return result;
    }
    DomDocument* load_page(const char* path, float width=320, float height=240) {
        DocumentJsHostConfig host={};host.ui_context=&ui;host.resource_policy=INPUT_RESOURCE_LOCAL_ONLY;
        page=load_html_doc(doc.url,(char*)path,width,height,&host);
        if (!page) return nullptr;
        ui.document=lam::up(page);ui.viewport_width=width;ui.viewport_height=height;ui.window_width=width;ui.window_height=height;ui.create_surface(width,height);
        layout_html_doc(&ui,page,false);return page;
    }
    ImageSurface* snapshot(float width=128,float height=128,float scale=1) {
        ImageSurface* image=scene3d_snapshot(root,&ui,width,height,scale);
        EXPECT_NE(image,nullptr)<<scene3d_diagnostic(root);return image;
    }
    void pixel(ImageSurface* image,unsigned x,unsigned y,unsigned r,unsigned g,unsigned b,unsigned a=255,unsigned tolerance=3) {
        ASSERT_NE(image,nullptr);ASSERT_LT(x,(unsigned)image->width);ASSERT_LT(y,(unsigned)image->height);
        const uint8_t* p=(const uint8_t*)image->pixels+y*image->pitch+x*4;
        const unsigned expected[]={r,g,b,a};
        for (unsigned c=0;c<4;c++) EXPECT_LE((unsigned)abs((int)p[c]-(int)expected[c]),tolerance)<<"channel "<<c<<" at "<<x<<","<<y;
    }
};

TEST_F(Scene3dTest, IndexedCubeRendersRealPixelsAndCachesUnchangedFrame) {
    mesh("box");attr(root,"background","#203040");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    pixel(image,64,64,255,0,0);pixel(image,4,4,32,48,64);
    Scene3dStats first{},second{};ASSERT_TRUE(scene3d_stats(root,&first));
    EXPECT_GT(first.graphics.gpu_bytes,0u);EXPECT_GT(first.graphics.cpu_bytes,0u);EXPECT_GT(first.projection_bytes,0u);
    EXPECT_EQ(first.snapshot_bytes,128u*128u*4u);
    RecordProperty("cube_gpu_bytes",(int64_t)first.graphics.gpu_bytes);
    RecordProperty("cube_core_cpu_bytes",(int64_t)first.graphics.cpu_bytes);
    RecordProperty("cube_projection_live_bytes",(int64_t)first.projection_bytes);
    RecordProperty("cube_projection_reserved_bytes",(int64_t)first.projection_reserved_bytes);
    RecordProperty("cube_snapshot_bytes",(int64_t)first.snapshot_bytes);
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),image);ASSERT_TRUE(scene3d_stats(root,&second));
    EXPECT_EQ(first.snapshot_generation,second.snapshot_generation);EXPECT_EQ(second.graphics.frames,1u);
    EXPECT_GT(second.graphics.draws,0u);EXPECT_GT(second.graphics.resources,0u);EXPECT_NE(second.graphics.driver[0],0);
    RecordProperty("renderer",second.graphics.driver);RecordProperty("OpenGL",second.graphics.version);RecordProperty("GLSL",second.graphics.shading_language);
}
TEST_F(Scene3dTest, ParentTransformAndVisibilityUpdatePixels) {
    DomElement* group=element("group",root);DomElement* object=mesh("plane","#00ff00","basic",group);
    attr(group,"position","1 0 0");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    pixel(image,80,64,0,255,0);pixel(image,40,64,0,0,0,0);
    attr(object,"visible","false");image=snapshot();pixel(image,80,64,0,0,0,0);
    attr(object,"visible","true");ASSERT_TRUE(root->remove_child(camera));ASSERT_TRUE(group->append_child(camera));doc.mutation_epoch++;
    image=snapshot();pixel(image,64,64,0,255,0);
    ASSERT_TRUE(camera->remove_attribute("target"));doc.mutation_epoch++;
    image=snapshot();pixel(image,64,64,0,255,0);
    attr(camera,"rotation","0 3.14159265 0");image=snapshot();pixel(image,64,64,0,0,0,0);
}
TEST_F(Scene3dTest, LambertAmbientDirectionalAndNonuniformNormals) {
    DomElement* object=mesh("plane","#808080","lambert");attr(object,"rotation","0 0.5 0");attr(object,"scale","2 1 0.5");
    DomElement* ambient=element("light",root);attr(ambient,"type","ambient");attr(ambient,"intensity","0.25");
    DomElement* light=element("light",root);attr(light,"type","directional");attr(light,"position","0 0 4");attr(light,"intensity","0.5");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    float linear=powf((128.0f/255+.055f)/1.055f,2.4f)*(.25f+.5f*cosf(.5f));
    unsigned expected=(unsigned)lroundf((1.055f*powf(linear,1/2.4f)-.055f)*255);
    pixel(image,64,64,expected,expected,expected,255,4);
}
TEST_F(Scene3dTest, TransparencyIsOrderedAndPremultipliedOverBackground) {
    DomElement* front=mesh("plane","#ff0000");attr(front,"position","0 0 1");
    attr(front->last_child->as_element(),"opacity","0.5");
    DomElement* back=mesh("plane","#0000ff");attr(back->last_child->as_element(),"opacity","0.5");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    // linear red=0.5, blue=0.25, alpha=0.75; encode straight then premultiply for page composition.
    unsigned red=(unsigned)lroundf((1.055f*powf(2/3.0f,1/2.4f)-.055f)*.75f*255);
    unsigned blue=(unsigned)lroundf((1.055f*powf(1/3.0f,1/2.4f)-.055f)*.75f*255);
    pixel(image,64,64,red,0,blue,191,4);EXPECT_EQ(image->alpha_mode,IMAGE_ALPHA_PREMULTIPLIED);
}
TEST_F(Scene3dTest, SharedGeometryMaterialAndActualInstancedDraw) {
    DomElement* resources=element("resources",root);
    DomElement* g=element("geometry",resources);attr(g,"id","plane");attr(g,"type","plane");
    DomElement* m=element("material",resources);attr(m,"id","red");attr(m,"type","basic");attr(m,"color","#ff0000");
    DomElement* object=element("mesh",root);attr(object,"geometry","plane");attr(object,"material","red");
    attr(object,"instances","1 0 0 0 0 1 0 0 0 0 1 0 -1 0 0 1  1 0 0 0 0 1 0 0 0 0 1 0 1 0 0 1");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,48,64,255,0,0);pixel(image,80,64,255,0,0);pixel(image,64,64,0,0,0,0);
    Scene3dStats stats{};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.geometries,1u);EXPECT_EQ(stats.graphics.draws,1u);
}
TEST_F(Scene3dTest, ViewBoxFramingMeetNoneSliceOriginAndDensity) {
    mesh();attr(root,"viewBox","0 0 100 100");
    ImageSurface* image=snapshot(200,100);pixel(image,100,50,255,0,0);pixel(image,60,50,0,0,0,0);
    Scene3dStats stats{};scene3d_stats(root,&stats);EXPECT_FLOAT_EQ(stats.camera_aspect,1);
    attr(root,"preserveAspectRatio","xMaxYMax meet");image=snapshot(200,100);pixel(image,150,50,255,0,0);pixel(image,100,50,0,0,0,0);
    attr(root,"preserveAspectRatio","xMinYMin meet");image=snapshot(200,100);pixel(image,50,50,255,0,0);pixel(image,100,50,0,0,0,0);
    // at fov=90 and z=4, the plane occupies one quarter of the projected frame.
    attr(root,"preserveAspectRatio","none");image=snapshot(200,100);pixel(image,80,50,255,0,0);
    attr(root,"preserveAspectRatio","xMidYMid slice");image=snapshot(200,100);pixel(image,100,30,255,0,0);
    attr(root,"viewBox","25 0 100 100");attr(root,"preserveAspectRatio","none");image=snapshot(200,100);pixel(image,50,50,255,0,0);pixel(image,110,50,0,0,0,0);
    image=snapshot(200,100,2);ASSERT_NE(image,nullptr);EXPECT_EQ(image->width,400);EXPECT_EQ(image->height,200);
    scene3d_stats(root,&stats);EXPECT_FLOAT_EQ(stats.camera_aspect,1);
    attr(root,"viewBox","0 0 0 100");EXPECT_EQ(scene3d_snapshot(root,&ui,200,100,1),nullptr);
    attr(root,"viewBox","0 0 -100 100");image=snapshot(200,100);ASSERT_NE(image,nullptr);scene3d_stats(root,&stats);EXPECT_FLOAT_EQ(stats.camera_aspect,2);
}
TEST_F(Scene3dTest, InvalidDescriptionsFailAndClearPriorPixels) {
    DomElement* object=mesh();ASSERT_NE(snapshot(),nullptr);
    attr(camera,"far","0.01");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"camera"),nullptr);
    attr(camera,"far","100");attr(object,"scale","nan 1 1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    attr(object,"scale","1 1 1");attr(object,"position","3e38 0 0");
    attr(object,"instances","1 0 0 0 0 1 0 0 0 0 1 0 1e38 0 0 1  1 0 0 0 0 1 0 0 0 0 1 0 1e38 0 0 1");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"overflow"),nullptr);
    attr(object,"position","0 0 0");ASSERT_TRUE(object->remove_attribute("instances"));doc.mutation_epoch++;
    DomElement* nested=element("scene3d",root);EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"nested"),nullptr);ASSERT_TRUE(root->remove_child(nested));doc.mutation_epoch++;
    DomElement* duplicate=element("camera",root);attr(duplicate,"id","main");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"duplicate"),nullptr);
}
TEST_F(Scene3dTest, ContextIsolationLossRebuildAndResourceTeardown) {
    mesh();ASSERT_NE(snapshot(),nullptr);Scene3dStats first{};scene3d_stats(root,&first);
    scene3d_context_lost(root);ImageSurface* image=snapshot();pixel(image,64,64,255,0,0);
    Scene3dStats second{};scene3d_stats(root,&second);EXPECT_NE(first.graphics.generation,second.graphics.generation);
    EXPECT_EQ(first.graphics.resources,second.graphics.resources);
    EXPECT_EQ(scene3d_snapshot(root,&ui,5000,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"quota"),nullptr);
}
TEST_F(Scene3dTest, ShaderReflectionUniformsFailuresAndForeignGenerations) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    GLFWwindow* previous=glfwGetCurrentContext();
    NativeGlResource program=native_gl_program(graphics,
        "#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}",
        "#version 330 core\nuniform vec4 color;out vec4 result;void main(){result=color;}");
    ASSERT_NE(program.id,0u)<<native_gl_diagnostic(graphics);EXPECT_EQ(native_gl_uniform_count(graphics,program),1u);
    char name[128];unsigned type;int size;EXPECT_TRUE(native_gl_uniform_info(graphics,program,0,name,sizeof(name),&type,&size));EXPECT_STREQ(name,"color");
    NativeGlUniform uniform=native_gl_uniform(graphics,program,"color");float color[]={1,0,0,1};EXPECT_TRUE(native_gl_uniform_set(graphics,uniform,color,4));
    float invalid_color[]={NAN,0,0,1};EXPECT_FALSE(native_gl_uniform_set(graphics,uniform,invalid_color,4));
    EXPECT_NE(strstr(native_gl_diagnostic(graphics),"finite"),nullptr);
    EXPECT_EQ(native_gl_program(graphics,"invalid vertex source","invalid fragment source").id,0u);EXPECT_NE(native_gl_diagnostic(graphics)[0],0);
    EXPECT_EQ(native_gl_program(graphics,
        "#version 330 core\nout vec3 vary;void main(){vary=vec3(1);gl_Position=vec4(0,0,0,1);}",
        "#version 330 core\nin vec4 vary;out vec4 result;void main(){result=vary;}").id,0u);
    NativeGlContext* foreign=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(foreign,nullptr);
    EXPECT_FALSE(native_gl_valid(foreign,program,NATIVE_GL_PROGRAM));EXPECT_FALSE(native_gl_uniform_set(foreign,uniform,color,4));
    native_gl_destroy(foreign);EXPECT_EQ(glfwGetCurrentContext(),previous);
    native_gl_release(graphics,program);EXPECT_FALSE(native_gl_uniform_set(graphics,uniform,color,4));
    EXPECT_EQ(native_gl_buffer(graphics,color,129u*1024u*1024u).id,0u);
    native_gl_destroy(graphics);EXPECT_EQ(native_gl_create(false,diagnostic,sizeof(diagnostic)),nullptr);
}
TEST(Scene3dMath, ViewBoxRejectsNonfiniteNegativeAndTrailingComponents) {
    const char* invalid[]={"0 0 -1 1","0 0 1 -1","nan 0 1 1","0 0 inf 1","0 0 1 1 2","0 0 1 1junk"};
    for (const char* value:invalid) EXPECT_FALSE(svg_parse_viewbox(value).has_viewbox)<<value;
    EXPECT_TRUE(svg_parse_viewbox("0 0 0 1").has_viewbox);EXPECT_TRUE(svg_parse_viewbox("1, 2, 3, 4").has_viewbox);
}

TEST_F(Scene3dTest, LocalTextureUvOrientationAndOpaqueDepth) {
    DomElement* object=mesh("plane","#ffffff");DomElement* texture=element("texture",object->last_child->as_element());
    attr(texture,"src","test/scene3d/quadrants.png");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    pixel(image,51,51,255,0,0);pixel(image,76,51,0,255,0);
    pixel(image,51,76,0,0,255);pixel(image,76,76,255,255,255);
    Scene3dStats stats{};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.textures,1u);
    attr(texture,"src","test/scene3d/missing.png");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"texture"),nullptr);
}
TEST_F(Scene3dTest, MirroredMeshesAndTransparentInstancesUseCorrectWindingAndOrder) {
    DomElement* front=mesh("plane","#ff0000");attr(front,"scale","-1 1 1");
    pixel(snapshot(),64,64,255,0,0);
    attr(front->last_child->as_element(),"opacity","0.5");
    attr(front,"instances","1 0 0 0 0 1 0 0 0 0 1 0 0 0 1 1  1 0 0 0 0 1 0 0 0 0 1 0 0 0 -1 1");
    ImageSurface* image=snapshot();pixel(image,64,64,191,0,0,191,4);
    Scene3dStats stats{};scene3d_stats(root,&stats);EXPECT_EQ(stats.meshes,2u);
}
TEST_F(Scene3dTest, TwoViewportsRemainIsolatedAndRemovedSnapshotsStayLeased) {
    DomElement* wrapper=element("div");ASSERT_TRUE(wrapper->append_child(root));doc.root=lam::up(wrapper);
    mesh();ImageSurface* old=snapshot();ASSERT_NE(old,nullptr);
    DisplayList retained={};dl_init(&retained,nullptr);
    dl_blit_surface_scaled(&retained,old,0,0,128,128,SCALE_MODE_LINEAR,nullptr,nullptr,0,255,old->generation);
    auto handle=old->self;uint64_t generation=old->generation;
    DomElement* other=element("scene3d",wrapper);attr(other,"background","#00ff00");
    DomElement* other_camera=element("camera",other);attr(other_camera,"type","perspective");attr(other_camera,"position","0 0 4");
    ImageSurface* green=scene3d_snapshot(other,&ui,128,128,1);pixel(green,64,64,0,255,0);
    Scene3dStats a{},b{};scene3d_stats(root,&a);scene3d_stats(other,&b);EXPECT_NE(a.graphics.generation,b.graphics.generation);
    attr(root,"background","#0000ff");ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(image_surface_lookup(handle),old);EXPECT_NE(old->generation,generation);pixel(old,64,64,255,0,0);
    scene3d_release_subtree(root);EXPECT_FALSE(scene3d_stats(root,&a));EXPECT_TRUE(scene3d_stats(other,&b));
    pixel(image_surface_lookup(handle),64,64,255,0,0);dl_destroy(&retained);EXPECT_EQ(image_surface_lookup(handle),nullptr);
    ASSERT_TRUE(wrapper->remove_child(other));scene3d_collect(&doc);EXPECT_FALSE(scene3d_stats(other,&b));
}
TEST_F(Scene3dTest, BufferIndexAttributeInstanceAndStaleDependencyRangesAreRejected) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlResource program=native_gl_program(graphics,"#version 330 core\nlayout(location=0) in vec3 p;void main(){gl_Position=vec4(p,1);}",
        "#version 330 core\nout vec4 result;void main(){result=vec4(1,0,0,1);}");
    const float positions[]={-1,-1,0,1,-1,0,0,1,0};const uint32_t indices[]={0,1,3};
    NativeGlResource buffer=native_gl_buffer(graphics,positions,sizeof(positions));
    NativeGlResource index=native_gl_buffer(graphics,indices,sizeof(indices));
    NativeGlAttribute attribute={buffer,0,3,3*sizeof(float),0,0};
    NativeGlResource vertices=native_gl_vertices(graphics,&attribute,1,index);
    NativeGlResource target=native_gl_target(graphics,32,32);const float clear[]={0,0,0,0};
    EXPECT_EQ(native_gl_snapshot(graphics,target),nullptr);ASSERT_TRUE(native_gl_begin(graphics,target,clear));
    NativeGlDraw draw={program,vertices,{},3,1,true,false,0,false};
    EXPECT_FALSE(native_gl_draw(graphics,&draw));draw.indexed=false;draw.count=4;EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.count=3;EXPECT_TRUE(native_gl_draw(graphics,&draw));
    attribute.divisor=1;vertices=native_gl_vertices(graphics,&attribute,1,{});draw.vertices=vertices;draw.instances=4;
    EXPECT_FALSE(native_gl_draw(graphics,&draw));draw.instances=1;native_gl_release(graphics,buffer);EXPECT_FALSE(native_gl_draw(graphics,&draw));
    attribute.offset=sizeof(positions);EXPECT_EQ(native_gl_vertices(graphics,&attribute,1,{}).id,0u);
    native_gl_destroy(graphics);
}
TEST_F(Scene3dTest, MixedPageClippingTransformOpacitySvgStackingAndSceneMutation) {
    ASSERT_NE(load_page("test/scene3d/mixed.html"),nullptr);
    DomElement* canvas=dom_find_element_by_id(page->root->as_element(),"canvas");ASSERT_NE(canvas,nullptr);
    ASSERT_TRUE(radiant_canvas_ensure(canvas));ASSERT_TRUE(radiant_canvas_set_fill_color(canvas,255,0,0,255));
    ASSERT_TRUE(radiant_canvas_fill_rect(canvas,0,0,128,128));
    render_html_doc(&ui,page->view_tree,nullptr);ASSERT_NE(ui.surface,nullptr);
    pixel(ui.surface,10,10,255,255,255);pixel(ui.surface,80,30,128,128,128,255,4);
    pixel(ui.surface,120,84,255,128,128,255,4);pixel(ui.surface,140,70,255,255,255);pixel(ui.surface,132,84,0,0,255);
    pixel(ui.surface,180,30,255,255,255);pixel(ui.surface,200,30,255,128,128,255,4);
    DomElement* viewport=dom_find_element_by_id(page->root->as_element(),"viewport");ASSERT_NE(viewport,nullptr);
    Scene3dStats before{},after{};ASSERT_TRUE(scene3d_stats(viewport,&before));
    DomElement* material=dom_find_element_by_id(page->root->as_element(),"color");ASSERT_NE(material,nullptr);
    ASSERT_TRUE(material->set_attribute("color","#00ff00"));page->mutation_epoch++;
    ASSERT_TRUE(radiant_canvas_set_fill_color(canvas,0,0,255,255));ASSERT_TRUE(radiant_canvas_fill_rect(canvas,0,0,128,128));
    render_html_doc(&ui,page->view_tree,nullptr);pixel(ui.surface,120,84,128,255,128,255,4);
    pixel(ui.surface,200,30,128,128,255,255,4);
    ASSERT_TRUE(scene3d_stats(viewport,&after));EXPECT_GT(after.snapshot_generation,before.snapshot_generation);
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    Scene3dStats cached{};scene3d_stats(viewport,&cached);EXPECT_EQ(cached.graphics.frames,after.graphics.frames);
    ASSERT_TRUE(ui_context_set_device_scale(&ui,2,2));ui.create_surface(640,480);
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    Scene3dStats dense{};ASSERT_TRUE(scene3d_stats(viewport,&dense));EXPECT_EQ(dense.raster_width,256u);
    EXPECT_EQ(dense.raster_height,256u);EXPECT_FLOAT_EQ(dense.camera_aspect,1);EXPECT_GT(dense.snapshot_generation,cached.snapshot_generation);
    pixel(ui.surface,240,168,128,255,128,255,4);
    pixel(ui.surface,400,60,128,128,255,255,4);
}
TEST_F(Scene3dTest, SvgSizingParityCoversRatioAttributesPercentLimitsFlexGridAndFallbacks) {
    ASSERT_NE(load_page("test/scene3d/sizing.html"),nullptr);
    const char* cases[]={"ratio","attributes","percent","limits","absent","invalid","zero","only","auto","flex","grid"};
    for (const char* name:cases) {
        char svg_id[64],scene_id[64];snprintf(svg_id,sizeof(svg_id),"svg-%s",name);snprintf(scene_id,sizeof(scene_id),"scene-%s",name);
        DomElement* svg=dom_find_element_by_id(page->root->as_element(),svg_id);
        DomElement* scene=dom_find_element_by_id(page->root->as_element(),scene_id);ASSERT_NE(svg,nullptr);ASSERT_NE(scene,nullptr);
        EXPECT_FLOAT_EQ(scene->width,svg->width)<<name;EXPECT_FLOAT_EQ(scene->height,svg->height)<<name;
        if (!strcmp(name,"ratio")) { EXPECT_FLOAT_EQ(scene->width,320);EXPECT_FLOAT_EQ(scene->height,180); }
    }
}

class Scene3dGcTest : public Scene3dTest {
protected:
    Runtime runtime={};EvalContext evaluator={};Heap heap={};
    void SetUp() override {
        Scene3dTest::SetUp();heap.gc=gc_heap_create();ASSERT_NE(heap.gc,nullptr);
        evaluator.heap=&heap;runtime.eval_context=&evaluator;doc.lambda_runtime=&runtime;
    }
    void TearDown() override {
        dom_lifecycle_release_backing_roots(&doc);doc.lambda_runtime=nullptr;
        Scene3dTest::TearDown();gc_heap_destroy(heap.gc);
    }
};
TEST_F(Scene3dGcTest, TypedNumericGeometrySurvivesMovingGcAndProjectionRebuild) {
    Input* source_input=InputManager::create_input(nullptr);ASSERT_NE(source_input,nullptr);MarkBuilder builder(source_input);
    ArrayNum* positions=(ArrayNum*)gc_heap_calloc(heap.gc,sizeof(ArrayNum),LMD_TYPE_ARRAY_NUM);ASSERT_NE(positions,nullptr);
    positions->type_id=LMD_TYPE_ARRAY_NUM;positions->set_elem_type(ELEM_FLOAT64);
    positions->length=positions->capacity=9;positions->float_items=(double*)gc_data_alloc(heap.gc,9*sizeof(double));
    const double vertices[]={-1,-1,0,1,-1,0,0,1,0};memcpy(positions->float_items,vertices,sizeof(vertices));
    Item array;array.item=(uint64_t)positions;
    Item description=builder.element("geometry").attr("type",builder.createSymbolItem("buffer")).attr("positions",array).final();
    Element* original=description.element;Element* source=(Element*)gc_heap_calloc(heap.gc,sizeof(Element),LMD_TYPE_ELEMENT);
    ASSERT_NE(source,nullptr);*source=*original;
    source->data=gc_data_alloc(heap.gc,original->data_cap);memcpy(source->data,original->data,original->data_cap);
    DomElement* object=element("mesh",root);DomElement* geometry=DomElement::create(&doc,"geometry",source);
    ASSERT_NE(geometry,nullptr);ASSERT_TRUE(object->append_child(geometry));
    DomElement* material=element("material",object);attr(material,"type","basic");attr(material,"color","#ff0000");
    pixel(snapshot(),64,64,255,0,0);double* previous=positions->float_items;
    gc_collect(heap.gc,nullptr,0);EXPECT_NE(positions->float_items,previous);
    doc.mutation_epoch++;pixel(snapshot(),64,64,255,0,0);
    for (unsigned pass=0;pass<3;pass++) { gc_collect(heap.gc,nullptr,0);pixel(snapshot(128,128,pass%2+1),64*(pass%2+1),64*(pass%2+1),255,0,0); }
}

TEST_F(Scene3dTest, LambdaPackageSceneRendersWithTypedAttributesBesideHtmlAndSvg) {
    ASSERT_NE(load_page("test/scene3d/mixed.ls"),nullptr);
    DomElement* viewport=dom_find_element_by_id(page->root->as_element(),"viewport");ASSERT_NE(viewport,nullptr);
    Element* backing=dom_element_backing(viewport);ASSERT_NE(backing,nullptr);
    EXPECT_TRUE(ElementReader(backing).get_attr("width").isNumber());
    EXPECT_EQ(ElementReader(backing).get_attr("width").asInt(),128);
    SvgIntrinsicSize intrinsic=calculate_svg_intrinsic_size(backing);
    EXPECT_TRUE(intrinsic.has_intrinsic_width);EXPECT_FLOAT_EQ(intrinsic.width,128);
    EXPECT_FLOAT_EQ(viewport->width,128);EXPECT_FLOAT_EQ(viewport->height,128);
    render_html_doc(&ui,page->view_tree,nullptr);pixel(ui.surface,80,80,255,0,0);pixel(ui.surface,210,10,0,255,0);
    Scene3dStats stats{};ASSERT_TRUE(scene3d_stats(viewport,&stats));EXPECT_EQ(stats.meshes,1u);EXPECT_EQ(stats.graphics.frames,1u);
}

TEST_F(Scene3dTest, TransparentSceneCompositesOverSvgAndAntialiasesEdges) {
    DomElement* object=mesh();attr(object,"rotation","0 0 0.3");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    unsigned covered=0;
    for (unsigned y=0;y<(unsigned)image->height;y++) for (unsigned x=0;x<(unsigned)image->width;x++) {
        const uint8_t* p=(const uint8_t*)image->pixels+y*image->pitch+x*4;
        if (p[3]>0 && p[3]<255) { covered++;EXPECT_LE(p[0],p[3]); }
    }
    EXPECT_GT(covered,0u);
    ASSERT_NE(load_page("test/scene3d/alpha.html"),nullptr);
    DomElement* a=dom_find_element_by_id(page->root->as_element(),"alpha-scene");ASSERT_NE(a,nullptr);
    EXPECT_FLOAT_EQ(a->width,128);EXPECT_FLOAT_EQ(a->height,128);
    render_html_doc(&ui,page->view_tree,nullptr);
    Scene3dStats alpha_stats{};ASSERT_TRUE(scene3d_stats(a,&alpha_stats));EXPECT_EQ(alpha_stats.graphics.frames,1u);
    pixel(ui.surface,64,64,128,127,0,255,4);pixel(ui.surface,8,8,0,255,0);
}

TEST_F(Scene3dTest, OpaqueDepthCullingAndBufferVertexColorsDeterminePixels) {
    DomElement* front=mesh("plane","#ff0000");attr(front,"position","0 0 1");
    DomElement* back=mesh("plane","#00ff00");pixel(snapshot(),64,64,255,0,0);
    attr(front->last_child->as_element(),"side","back");pixel(snapshot(),64,64,0,255,0);
    attr(front,"visible","false");attr(back,"visible","false");
    DomElement* object=element("mesh",root);DomElement* geometry=element("geometry",object);
    attr(geometry,"type","buffer");attr(geometry,"positions","-1 -1 0 1 -1 0 0 1 0");
    attr(geometry,"colors","0 0 1 0 0 1 0 0 1");DomElement* material=element("material",object);attr(material,"type","basic");
    pixel(snapshot(),64,64,0,0,255);
    attr(geometry,"indices","0 1 3");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"index"),nullptr);
}

TEST_F(Scene3dTest, TypedTextAttributesCannotSilentlyUseDefaultMaterialColor) {
    Input* source_input=InputManager::create_input(nullptr);ASSERT_NE(source_input,nullptr);MarkBuilder builder(source_input);
    Item description=builder.element("material").attr("type",builder.createSymbolItem("basic")).attr("color",builder.createInt(42)).final();
    DomElement* object=mesh();DomElement* material=object->last_child->as_element();ASSERT_TRUE(object->remove_child(material));
    material=DomElement::create(&doc,"material",description.element);ASSERT_NE(material,nullptr);ASSERT_TRUE(object->append_child(material));
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"text attribute"),nullptr);
}

TEST_F(Scene3dTest, ViewportQuotaRecoversAfterDetachedSceneRetirement) {
    DomElement* wrapper=element("div");doc.root=lam::up(wrapper);ASSERT_TRUE(wrapper->append_child(root));mesh();ASSERT_NE(snapshot(),nullptr);
    DomElement* last=nullptr;
    for (unsigned i=0;i<8;i++) {
        last=element("scene3d",wrapper);DomElement* c=element("camera",last);attr(c,"type","perspective");
        ImageSurface* image=scene3d_snapshot(last,&ui,16,16,1);
        if (i<7) ASSERT_NE(image,nullptr)<<scene3d_diagnostic(last);
        else { EXPECT_EQ(image,nullptr);EXPECT_NE(strstr(scene3d_diagnostic(last),"quota"),nullptr); }
    }
    ASSERT_TRUE(wrapper->remove_child(root));scene3d_collect(&doc);
    EXPECT_NE(scene3d_snapshot(last,&ui,16,16,1),nullptr)<<scene3d_diagnostic(last);
    Scene3dStats stats{};EXPECT_FALSE(scene3d_stats(root,&stats));
}

TEST_F(Scene3dTest, WebGl2ApiAndMultipleCanvasPixels) {
    ASSERT_NE(load_page("test/webgl/api.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    ASSERT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_GE(strtol(result->get_attribute("data-checks"),nullptr,10),30);
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    pixel(ui.surface,32,64,255,0,0);pixel(ui.surface,192,64,0,255,0);
}
TEST_F(Scene3dTest, PinnedUnmodifiedThreeRendererAndAddonProduceRealPixels) {
    ASSERT_NE(load_page("test/demo/scene3d/three-gallery.html",848,650),nullptr);
    DomElement* status=dom_find_element_by_id(page->root->as_element(),"status");ASSERT_NE(status,nullptr);
    // the completed module publishes this marker only after its first actual renderer submission.
    EXPECT_STREQ(status->get_attribute("data-ready"),"true");
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    pixel(ui.surface,40,130,8,15,34);
    unsigned colored=0;
    for(unsigned y=180;y<540;y++) for(unsigned x=160;x<680;x++) {
        const uint8_t* p=(const uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4;
        if(p[1]>80 && p[2]>80) colored++;
    }
    EXPECT_GT(colored,20000u);
    NativeGlStats stats={};ASSERT_TRUE(radiant_webgl_stats(dom_find_element_by_id(page->root->as_element(),"garden"),&stats));
    EXPECT_EQ(stats.draws,4u);EXPECT_EQ(stats.shader_normalize_calls,8u);
    RecordProperty("three_shader_normalize_us",(int64_t)stats.shader_normalize_us);
    RecordProperty("three_gpu_bytes",(int64_t)stats.gpu_bytes);RecordProperty("three_core_cpu_bytes",(int64_t)stats.cpu_bytes);
}
TEST_F(Scene3dTest, WebGlContextLossRestoresNewResourcesAndRejectsStaleWrappers) {
    ASSERT_NE(load_page("test/webgl/loss.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    ASSERT_STREQ(result->get_attribute("data-result"),"passed");
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    pixel(ui.surface,32,32,0,0,255);
}

TEST_F(Scene3dTest, ThreeRendererResizesDensityDisposesAndKeepsCanvasesIndependent) {
    ASSERT_NE(load_page("test/webgl/three-lifecycle.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    ASSERT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_GE(strtol(result->get_attribute("data-checks"),nullptr,10),9);
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    pixel(ui.surface,32,64,255,0,0);pixel(ui.surface,192,64,0,255,0);
}
TEST(WebGlShaderTest, NormalizationPreservesCommentsConditionalsAndDesktopSource) {
    const char* es="// #version 999 in a comment\n#version 300 es\n#ifdef GL_ES\nprecision highp float;\n#endif\n#if __VERSION__ == 300\nout vec4 color;void main(){color=vec4(1); }\n#endif\n";
    char* adapted=native_gl_shader_source(es,true);ASSERT_NE(adapted,nullptr);
    EXPECT_NE(strstr(adapted,"// #version 999 in a comment"),nullptr);
    EXPECT_NE(strstr(adapted,"#version 330 core"),nullptr);EXPECT_NE(strstr(adapted,"#line 3"),nullptr);
    EXPECT_NE(strstr(adapted,"#ifdef RADIANT_WEBGL_ES"),nullptr);EXPECT_NE(strstr(adapted,"#if 300 == 300"),nullptr);
    EXPECT_EQ(strstr(adapted,"precision highp float"),nullptr);mem_free(adapted);
    const char* desktop="#version 330 core\nvoid main(){}";adapted=native_gl_shader_source(desktop,false);
    ASSERT_NE(adapted,nullptr);EXPECT_STREQ(adapted,desktop);mem_free(adapted);
}
TEST_F(Scene3dTest, Es100ShaderAndDefaultFramebufferResolvePreserveApplicationState) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    WebGlOptions options={true,true,true,true,true,false};ASSERT_TRUE(native_gl_webgl_init(graphics,32,32,&options));
    auto call=[&](WebGlOp op,double a=0,double b=0,double c=0,double d=0) {
        WebGlCommand command={};command.op=op;command.n[0]=a;command.n[1]=b;command.n[2]=c;command.n[3]=d;
        WebGlReply reply={};EXPECT_TRUE(native_gl_webgl_call(graphics,&command,&reply));return reply;
    };
    char* vertex=native_gl_shader_source("attribute vec3 p;void main(){gl_Position=vec4(p,1);}",false);
    char* fragment=native_gl_shader_source("precision mediump float;void main(){gl_FragColor=vec4(1);}",true);
    NativeGlResource program=native_gl_program(graphics,vertex,fragment);EXPECT_NE(program.id,0u)<<native_gl_diagnostic(graphics);
    mem_free(vertex);mem_free(fragment);native_gl_release(graphics,program);
    call(WEBGL_clearColor,0,1,0,1);call(WEBGL_clear,0x4000);
    call(WEBGL_stencilMaskSeparate,0x0404,0x12);call(WEBGL_stencilMaskSeparate,0x0405,0x34);
    call(WEBGL_scissor,0,0,1,1);call(WEBGL_enable,0x0C11);call(WEBGL_readBuffer,0);
    ImageSurface* image=native_gl_webgl_snapshot(graphics);ASSERT_NE(image,nullptr);pixel(image,16,16,0,255,0);
    EXPECT_EQ(call(WEBGL_getParameter,0x0C02).n[0],0);EXPECT_EQ(call(WEBGL_getParameter,0x0C11).n[0],1);
    EXPECT_EQ(native_gl_webgl_snapshot(graphics),nullptr);
    call(WEBGL_readBuffer,0x0405);
    uint8_t readback[4]={9,9,9,9};WebGlCommand command={};command.op=WEBGL_readPixels;
    command.n[2]=command.n[3]=1;command.n[4]=0x1908;command.n[5]=0x1401;command.data=readback;command.bytes=4;WebGlReply reply={};
    EXPECT_TRUE(native_gl_webgl_call(graphics,&command,&reply));EXPECT_EQ(readback[0],0);EXPECT_EQ(readback[1],0);EXPECT_EQ(readback[3],0);
    EXPECT_EQ(call(WEBGL_getParameter,0x0B98).n[0],0x12);EXPECT_EQ(call(WEBGL_getParameter,0x8CA5).n[0],0x34);
    pixel(image,16,16,0,255,0);EXPECT_EQ(call(WEBGL_getError).n[0],0);
    image_surface_destroy(image);native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, SelectedPinnedKhronosBufferObjectAndTextureAssertions) {
    ASSERT_NE(load_page("test/webgl/khronos-selected.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    ASSERT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_GE(strtol(result->get_attribute("data-checks"),nullptr,10),99);
}
