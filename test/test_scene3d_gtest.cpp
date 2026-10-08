#include <gtest/gtest.h>
#include "../radiant/scene3d.hpp"
#include "../radiant/scene3d_math.hpp"
#include "../radiant/radiant.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"

class Scene3dTest : public ::testing::Test {
protected:
    UiContext ui{};
    Input input{};
    DomDocument doc{};
    DomElement* root=nullptr;
    DomElement* camera=nullptr;
    void SetUp() override {
        // rendered tests explicitly opt into native graphics; absence is a failure.
        setenv("LAMBDA_HEADLESS_GLFW_WINDOW","1",1);
        ASSERT_EQ(ui_context_init(&ui,true,1),0);
        ASSERT_TRUE(doc.init(&input));
        root=element("scene3d"); ASSERT_NE(root,nullptr);doc.root=lam::up(root);
        ui.document=lam::up(&doc);doc.js.host_ui_context=&ui;
        camera=element("camera",root);ASSERT_NE(camera,nullptr);
        attr(camera,"type","perspective");attr(camera,"id","main");attr(camera,"position","0 0 4");attr(camera,"target","0 0 0");attr(camera,"fov","90");
        attr(root,"camera","main");
    }
    void TearDown() override {
        ui.document=nullptr;doc.destroy();ui_context_cleanup(&ui);
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
    ImageSurface* snapshot(float width=128,float height=128,float scale=1) {
        ImageSurface* image=scene3d_snapshot(root,&ui,width,height,scale);
        EXPECT_NE(image,nullptr)<<scene3d_diagnostic(root);return image;
    }
    void pixel(ImageSurface* image,unsigned x,unsigned y,unsigned r,unsigned g,unsigned b,unsigned a=255,unsigned tolerance=3) {
        ASSERT_NE(image,nullptr);ASSERT_LT(x,(unsigned)image->width);ASSERT_LT(y,(unsigned)image->height);
        const uint8_t* p=(const uint8_t*)image->pixels+y*image->pitch+x*4;
        const unsigned expected[]={r,g,b,a};
        for (unsigned c=0;c<4;c++) EXPECT_LE(abs((int)p[c]-(int)expected[c]),tolerance)<<"channel "<<c<<" at "<<x<<","<<y;
    }
};

TEST_F(Scene3dTest, IndexedCubeRendersRealPixelsAndCachesUnchangedFrame) {
    mesh("box");attr(root,"background","#203040");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    pixel(image,64,64,255,0,0);pixel(image,4,4,32,48,64);
    Scene3dStats first{},second{};ASSERT_TRUE(scene3d_stats(root,&first));
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),image);ASSERT_TRUE(scene3d_stats(root,&second));
    EXPECT_EQ(first.snapshot_generation,second.snapshot_generation);EXPECT_EQ(second.graphics.frames,1u);
    EXPECT_GT(second.graphics.draws,0u);EXPECT_GT(second.graphics.resources,0u);EXPECT_NE(second.graphics.driver[0],0);
}
TEST_F(Scene3dTest, ParentTransformAndVisibilityUpdatePixels) {
    DomElement* group=element("group",root);DomElement* object=mesh("plane","#00ff00","basic",group);
    attr(group,"position","1 0 0");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    pixel(image,80,64,0,255,0);pixel(image,40,64,0,0,0,0);
    attr(object,"visible","false");image=snapshot();pixel(image,80,64,0,0,0,0);
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
    attr(root,"preserveAspectRatio","none");image=snapshot(200,100);pixel(image,60,50,255,0,0);
    attr(root,"preserveAspectRatio","xMidYMid slice");image=snapshot(200,100);pixel(image,100,15,255,0,0);
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
    attr(object,"scale","1 1 1");DomElement* duplicate=element("camera",root);attr(duplicate,"id","main");
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
    NativeGlResource program=native_gl_program(graphics,
        "#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}",
        "#version 330 core\nuniform vec4 color;out vec4 result;void main(){result=color;}");
    ASSERT_NE(program.id,0u)<<native_gl_diagnostic(graphics);EXPECT_EQ(native_gl_uniform_count(graphics,program),1u);
    char name[128];unsigned type;int size;EXPECT_TRUE(native_gl_uniform_info(graphics,program,0,name,sizeof(name),&type,&size));EXPECT_STREQ(name,"color");
    NativeGlUniform uniform=native_gl_uniform(graphics,program,"color");float color[]={1,0,0,1};EXPECT_TRUE(native_gl_uniform_set(graphics,uniform,color,4));
    EXPECT_EQ(native_gl_program(graphics,"invalid vertex source","invalid fragment source").id,0u);EXPECT_NE(native_gl_diagnostic(graphics)[0],0);
    native_gl_release(graphics,program);EXPECT_FALSE(native_gl_uniform_set(graphics,uniform,color,4));
    EXPECT_EQ(native_gl_buffer(graphics,color,129u*1024u*1024u).id,0u);
    native_gl_destroy(graphics);EXPECT_EQ(native_gl_create(false,diagnostic,sizeof(diagnostic)),nullptr);
}
TEST(Scene3dMath, ViewBoxRejectsNonfiniteNegativeAndTrailingComponents) {
    const char* invalid[]={"0 0 -1 1","0 0 1 -1","nan 0 1 1","0 0 inf 1","0 0 1 1 2","0 0 1 1junk"};
    for (const char* value:invalid) EXPECT_FALSE(svg_parse_viewbox(value).has_viewbox)<<value;
    EXPECT_TRUE(svg_parse_viewbox("0 0 0 1").has_viewbox);EXPECT_TRUE(svg_parse_viewbox("1, 2, 3, 4").has_viewbox);
}
