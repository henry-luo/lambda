#include <gtest/gtest.h>
#include "../radiant/scene3d.hpp"
#include "../radiant/scene3d_math.hpp"
#include "../radiant/animation_mixer.hpp"
#include "../radiant/scene3d_animation.hpp"
#include "../radiant/svg_animation.hpp"
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../radiant/render_css3d.hpp"
#include "../radiant/event.hpp"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/dom/realm/dom_realm.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/input/input.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/module/radiant/radiant_dom_bridge.hpp"
#include "../lambda/module/radiant/radiant_webgl_bridge.hpp"
#include "../lib/mem.h"
#include "../lib/mem_factory.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_context.h"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/image.h"
#include "../lib/font/font_internal.h"
#include <time.h>

static void scene3d_test_capture(const char* name,ImageSurface* image) {
    const char* directory=getenv("LAMBDA_SCENE3D_CAPTURE_DIR");if(!directory) return;
    StrBuf* path=strbuf_new();strbuf_append_format(path,"%s/%s-native.png",directory,name);
    EXPECT_EQ(image_save_png(path->str,(const unsigned char*)image->pixels,image->width,image->height,4),1);
    strbuf_free(path);
}

TEST(Css3dProjection, RepeatedTextureMatchesExpandedImageAcrossViewerClipping) {
    const uint32_t tile_pixels[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0x80808080};
    uint32_t expanded[16 * 16];
    for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 16; x++)
        expanded[y * 16 + x] = tile_pixels[(y % 2) * 2 + x % 2];
    Rect tile = {-3, -5, 8, 6}, coverage = {-3, -5, 64, 48};
    Rect viewport = {0, 0, 48, 48};
    for (float perspective : {0.012f, -0.04f}) for (ScaleMode mode : {SCALE_MODE_NEAREST, SCALE_MODE_LINEAR_WRAP}) {
        SCOPED_TRACE(perspective);
        SCOPED_TRACE(mode);
        RdtMatrix transform = {1, 0, 12, 0, 1, 12, perspective, -0.007f, 1};
        uint32_t *repeated = nullptr, *reference = nullptr;
        Rect repeated_rect, reference_rect;
        ASSERT_TRUE(render_image_project_pixels(tile_pixels, 2, 2, 2, coverage,
            &transform, viewport, mode, true, &repeated, &repeated_rect, &tile));
        lam::Temp<uint32_t> owned_repeated(repeated);
        ASSERT_TRUE(render_image_project_pixels(expanded, 16, 16, 16, coverage,
            &transform, viewport, mode, true, &reference, &reference_rect));
        lam::Temp<uint32_t> owned_reference(reference);
        ASSERT_NE(repeated, nullptr); ASSERT_NE(reference, nullptr);
        ASSERT_FLOAT_EQ(repeated_rect.x, reference_rect.x); ASSERT_FLOAT_EQ(repeated_rect.y, reference_rect.y);
        ASSERT_FLOAT_EQ(repeated_rect.width, reference_rect.width); ASSERT_FLOAT_EQ(repeated_rect.height, reference_rect.height);
        size_t count = (size_t)repeated_rect.width * (size_t)repeated_rect.height;
        size_t visible = 0;
        for (size_t i = 0; i < count; i++) {
            if (reference[i] >> 24) visible++;
            for (unsigned channel = 0; channel < 4; channel++)
                EXPECT_NEAR((repeated[i] >> (channel * 8)) & 255,
                    (reference[i] >> (channel * 8)) & 255, mode == SCALE_MODE_NEAREST ? 0 : 1);
        }
        EXPECT_GT(visible, 400u);
    }
}

TEST(Css3dProjection, ParallelRowsMatchSerialViewportSampling) {
    const uint32_t pixels[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0x80808080};
    RdtMatrix transform = {1, 0, 110, 0, 1, 130, -.004f, .001f, 1};
    Rect destination = {-200, -150, 800, 700}, tile = {-3, -5, 8, 6};
    for (ScaleMode mode : {SCALE_MODE_NEAREST, SCALE_MODE_LINEAR_WRAP, SCALE_MODE_PIXELATED}) {
        uint32_t *parallel = nullptr, *serial = nullptr;
        Rect full, crop;
        ASSERT_TRUE(render_image_project_pixels(pixels, 2, 2, 2, destination, &transform,
            {0, 0, 440, 400}, mode, true, &parallel, &full, &tile));
        lam::Temp<uint32_t> owned_parallel(parallel);
        ASSERT_TRUE(render_image_project_pixels(pixels, 2, 2, 2, destination, &transform,
            {110, 100, 120, 90}, mode, true, &serial, &crop, &tile));
        lam::Temp<uint32_t> owned_serial(serial);
        ASSERT_NE(parallel, nullptr); ASSERT_NE(serial, nullptr);
        for (unsigned y = 0; y < (unsigned)crop.height; y++)
            for (unsigned x = 0; x < (unsigned)crop.width; x++) {
                size_t offset = (size_t)(crop.y - full.y + y) * (size_t)full.width +
                    (size_t)(crop.x - full.x + x);
                ASSERT_EQ(parallel[offset], serial[y * (size_t)crop.width + x]);
            }
    }
    render_pool_shutdown();
}

TEST(Css3dProjection, PreparedAlphaMatchesPerSampleConversionWithoutChangingSource) {
    uint32_t pixels[100 * 100];
    for (unsigned i = 0; i < 100 * 100; i++) pixels[i] = i * 2654435761u;
    RdtMatrix transform = {1, .01f, 0, 0, 1, 0, .001f, -.001f, 1};
    for (ScaleMode mode : {SCALE_MODE_NEAREST, SCALE_MODE_LINEAR_WRAP, SCALE_MODE_PIXELATED}) {
        uint32_t *prepared = nullptr, *per_sample = nullptr;
        Rect full, crop;
        ASSERT_TRUE(render_image_project_pixels(pixels, 100, 100, 100, {0, 0, 100, 100},
            &transform, {0, 0, 100, 100}, mode, true, &prepared, &full));
        lam::Temp<uint32_t> owned_prepared(prepared);
        ASSERT_TRUE(render_image_project_pixels(pixels, 100, 100, 100, {0, 0, 100, 100},
            &transform, {30, 30, 20, 20}, mode, true, &per_sample, &crop));
        lam::Temp<uint32_t> owned_per_sample(per_sample);
        ASSERT_NE(prepared, nullptr); ASSERT_NE(per_sample, nullptr);
        for (unsigned y = 0; y < (unsigned)crop.height; y++)
            for (unsigned x = 0; x < (unsigned)crop.width; x++)
                EXPECT_EQ(prepared[(size_t)(crop.y - full.y + y) * (size_t)full.width +
                    (size_t)(crop.x - full.x + x)], per_sample[y * (size_t)crop.width + x]);
    }
    for (unsigned i = 0; i < 100 * 100; i++) ASSERT_EQ(pixels[i], i * 2654435761u);
}

struct AnimationOracleTrack {
    const char* name;
    double times[5], values[20], incoming[40], outgoing[40];
    unsigned keys, components, type, interpolation, ending;
    bool tangents;
    double times_to_sample[12], expected[48];
    unsigned samples;
};
struct AnimationOracleStep {
    unsigned op, action;
    double a, b, c, values[5], time;
    const char* events;
};
struct AnimationOracleScenario {
    const char* name;
    bool additive;
    const AnimationOracleStep* steps;
    unsigned count;
};
#include "webgl/animation-oracle.inc"

TEST(AnimationCore, PinnedThreeTypedInterpolationOracle) {
    for (const auto& fixture : animation_oracle_tracks) {
        SCOPED_TRACE(fixture.name);
        AnimationTrackView track = {fixture.times, fixture.values, nullptr,
            fixture.tangents ? fixture.incoming : nullptr, fixture.tangents ? fixture.outgoing : nullptr,
            fixture.keys, fixture.components, (AnimationValueType)fixture.type, (AnimationInterpolation)fixture.interpolation,
            (AnimationEnding)fixture.ending, (AnimationEnding)fixture.ending};
        ASSERT_TRUE(animation_track_validate(track));
        for (unsigned sample = 0; sample < fixture.samples; sample++) {
            double value[4] = {};
            ASSERT_TRUE(animation_track_sample(track, fixture.times_to_sample[sample], value));
            for (unsigned c = 0; c < fixture.components; c++)
                EXPECT_NEAR(value[c], fixture.expected[sample * fixture.components + c], 2e-7)
                    << "at " << fixture.times_to_sample[sample] << " component " << c;
        }
    }
}

struct AnimationTestBinding {
    AnimationValue values[2];
    StrBuf* events;
    unsigned writes;
    static bool read(void* owner, uint64_t property, AnimationValue* value) {
        auto* state = (AnimationTestBinding*)owner;
        if (property >= 2) return false;
        *value = state->values[property]; return true;
    }
    static bool write(void* owner, uint64_t property, const AnimationValue* value) {
        auto* state = (AnimationTestBinding*)owner;
        if (property >= 2) return false;
        state->values[property] = *value; state->writes++; return true;
    }
    static void event(void* owner, uint64_t action, const char* type, double detail) {
        auto* state = (AnimationTestBinding*)owner;
        strbuf_append_format(state->events, "%s:%llu:%.0f;", type, (unsigned long long)(action - 1), detail);
    }
};

TEST(AnimationCore, PinnedThreeActionsMixingAndEventsOracle) {
    const double times[2][3] = {{0,1,2},{0,.5,1}};
    const double numbers[2][3] = {{2,10,4},{-3,5,9}};
    const double half = sqrt(.5);
    const double quaternions[2][12] = {{0,0,0,1,0,half,0,half,0,1,0,0},{0,0,0,1,half,0,0,half,1,0,0,0}};
    for (const auto& fixture : animation_oracle_scenarios) {
        SCOPED_TRACE(fixture.name);
        AnimationTestBinding binding = {};
        binding.events = strbuf_new();
        binding.values[0].type = ANIMATION_NUMBER; binding.values[0].count = 1; binding.values[0].numbers[0] = 7;
        binding.values[1].type = ANIMATION_QUATERNION; binding.values[1].count = 4;
        binding.values[1].numbers[2] = sin(.25); binding.values[1].numbers[3] = cos(.25);
        auto* mixer = animation_mixer_create(&binding, {AnimationTestBinding::read, AnimationTestBinding::write, AnimationTestBinding::event});
        ASSERT_NE(mixer, nullptr);
        AnimationChannelView channels[2][2] = {};
        AnimationActionState* actions[2] = {};
        for (unsigned i = 0; i < 2; i++) {
            channels[i][0] = {{times[i],numbers[i],nullptr,nullptr,nullptr,3,1,ANIMATION_NUMBER,ANIMATION_LINEAR},0};
            channels[i][1] = {{times[i],quaternions[i],nullptr,nullptr,nullptr,3,4,ANIMATION_QUATERNION,ANIMATION_LINEAR},1};
            AnimationClipView clip = {i+1,i ? 1.0 : 2.0,channels[i],2,i && fixture.additive};
            actions[i] = animation_mixer_action(mixer, clip); ASSERT_NE(actions[i], nullptr);
            EXPECT_EQ(animation_mixer_action(mixer, clip), actions[i]);
        }
        for (unsigned index = 0; index < fixture.count; index++) {
            const auto& step = fixture.steps[index]; auto* action = actions[step.action];
            SCOPED_TRACE(index); strbuf_reset(binding.events);
            switch (step.op) {
                case 0: ASSERT_TRUE(animation_mixer_update(mixer,step.a)); break;
                case 1: ASSERT_TRUE(animation_mixer_set_time(mixer,step.a)); break;
                case 2: ASSERT_TRUE(animation_action_play(action)); break;
                case 3: ASSERT_TRUE(animation_action_stop(action)); break;
                case 4: animation_action_reset(action); break;
                case 5: action->weight=step.a; break;
                case 6: action->time_scale=step.a; break;
                case 7: action->loop=(AnimationLoopMode)(unsigned)step.a; action->repetitions=step.b; break;
                case 8: action->clamp=step.a; break;
                case 9: action->paused=step.a; break;
                case 10: action->enabled=step.a; break;
                case 11: ASSERT_TRUE(animation_action_fade(action,step.a,step.b)); break;
                case 12: ASSERT_TRUE(animation_action_crossfade(action,actions[(unsigned)step.a],step.b,step.c)); break;
                case 13: ASSERT_TRUE(animation_action_warp(action,step.a,step.b,step.c)); break;
                case 14: action->scheduled=true; action->scheduled_start=step.a; break;
                case 15: ASSERT_TRUE(animation_mixer_time_scale(mixer,step.a)); break;
                case 16: ASSERT_TRUE(animation_mixer_uncache(mixer,action)); actions[step.action]=nullptr; break;
                default: FAIL();
            }
            EXPECT_NEAR(animation_mixer_time(mixer),step.time,1e-12);
            EXPECT_NEAR(binding.values[0].numbers[0],step.values[0],2e-6);
            for (unsigned c=0;c<4;c++) EXPECT_NEAR(binding.values[1].numbers[c],step.values[c+1],2e-6) << "component " << c;
            EXPECT_STREQ(binding.events->str,step.events);
        }
        animation_mixer_destroy(mixer);
        EXPECT_DOUBLE_EQ(binding.values[0].numbers[0],7);
        strbuf_free(binding.events);
    }
}

TEST(AnimationCore, RejectsMalformedTracksAndInvalidBindingsBeforePlayback) {
    double times[] = {0,1,2}, values[] = {0,1,0};
    AnimationTrackView track = {times,values,nullptr,nullptr,nullptr,3,1,ANIMATION_NUMBER,ANIMATION_LINEAR};
    ASSERT_TRUE(animation_track_validate(track));
    times[1] = -1; EXPECT_FALSE(animation_track_validate(track)); times[1]=1;
    values[1] = NAN; EXPECT_FALSE(animation_track_validate(track)); values[1]=1;
    track.type=ANIMATION_QUATERNION; EXPECT_FALSE(animation_track_validate(track)); track.type=ANIMATION_NUMBER;
    AnimationTestBinding binding = {}; binding.values[0].type=ANIMATION_NUMBER;binding.values[0].count=1;
    auto* mixer = animation_mixer_create(&binding,{AnimationTestBinding::read,AnimationTestBinding::write,nullptr}); ASSERT_NE(mixer,nullptr);
    AnimationChannelView channels[] = {{track,0},{track,9}};
    EXPECT_EQ(animation_mixer_action(mixer,{1,2,channels,2,false}),nullptr);
    auto* action = animation_mixer_action(mixer,{1,2,channels,1,false}); ASSERT_NE(action,nullptr);
    ASSERT_TRUE(animation_action_play(action)); EXPECT_TRUE(animation_mixer_active(mixer));
    action->paused=true; EXPECT_FALSE(animation_mixer_active(mixer));
    ASSERT_TRUE(animation_action_fade(action,1,false)); EXPECT_TRUE(animation_mixer_active(mixer));
    EXPECT_FALSE(animation_mixer_update(mixer,INFINITY));
    ASSERT_TRUE(animation_mixer_uncache(mixer,action)); EXPECT_FALSE(animation_mixer_active(mixer));
    EXPECT_EQ(binding.writes,1u);
    animation_mixer_destroy(mixer);
}

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
    void pump_js() {
        for(unsigned turn=0;turn<8;turn++) {
            if(js_event_loop_virtual_clock_enabled()) js_event_loop_advance_virtual_time(1,0);
            else js_event_loop_pump_wait(1);
        }
    }
    Item run_js(const char* source) {
        Runtime* runtime=dom_document_script_runtime(page);
        if(!runtime||!radiant_eval_context_switch(runtime_get_eval_context(runtime))||
           !runtime_context_bind_retained(runtime,runtime_get_eval_context(runtime))||!radiant_bind_document_script_host(&ui,page)) return ItemError;
        RootFrame roots(1);Rooted<Item> code(roots,make_string_item(source));return js_builtin_eval(code.get(),1);
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
TEST_F(Scene3dTest, NativeClipAnimatesTransformAndMaterialWithoutRebuildingGeometry) {
    DomElement* object=mesh("plane");attr(object,"id","moving");
    DomElement* material=object->last_child->as_element();attr(material,"id","paint");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","travel");attr(clip,"duration","2");
    DomElement* position=element("keyframe-track",clip);attr(position,"path","moving.position");
    attr(position,"times","0 1 2");attr(position,"values","-1.5 0 0  1.5 0 0  -1.5 0 0");
    DomElement* color=element("keyframe-track",clip);attr(color,"path","moving.material.color");
    attr(color,"times","0 1 2");attr(color,"values","1 0 0  0 1 0  1 0 0");
    ASSERT_NE(snapshot(),nullptr);
    auto* animation=scene3d_animations(root);ASSERT_NE(animation,nullptr);
    auto* action=scene3d_animation_action(animation,"travel");ASSERT_NE(action,nullptr);
    ASSERT_TRUE(animation_action_play(action));ASSERT_TRUE(scene3d_animation_seek(animation,0));
    pixel(snapshot(),40,64,255,0,0);pixel(snapshot(),88,64,0,0,0,0);
    Scene3dStats before{},after{};ASSERT_TRUE(scene3d_stats(root,&before));
    ASSERT_TRUE(scene3d_animation_seek(animation,1));pixel(snapshot(),88,64,0,255,0);pixel(snapshot(),40,64,0,0,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.projection_generation,after.projection_generation);
    EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
    EXPECT_GT(after.snapshot_generation,before.snapshot_generation);
    EXPECT_STREQ(object->get_attribute("position"),nullptr);EXPECT_STREQ(material->get_attribute("color"),"#ff0000");
    scene3d_context_lost(root);pixel(snapshot(),88,64,0,255,0);
    EXPECT_EQ(scene3d_animation_action(scene3d_animations(root),"travel"),action);
    ASSERT_TRUE(animation_action_stop(action));pixel(snapshot(),64,64,255,0,0);
    attr(material,"color","#0000ff");pixel(snapshot(),64,64,0,0,255);
    ASSERT_TRUE(animation_action_play(action));ASSERT_TRUE(scene3d_animation_seek(animation,.5));
    ASSERT_TRUE(animation_action_stop(action));pixel(snapshot(),64,64,0,0,255);
}
TEST_F(Scene3dTest, NativeClipUsesDocumentSchedulerAndParksAfterClampedFinish) {
    DomElement* object=mesh("plane");attr(object,"id","moving");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","travel");
    DomElement* track=element("keyframe-track",clip);attr(track,"path","moving.position");
    attr(track,"times","0 1");attr(track,"values","0 0 0  1.5 0 0");
    ASSERT_NE(snapshot(),nullptr);auto* animation=scene3d_animations(root);ASSERT_NE(animation,nullptr);
    auto* action=scene3d_animation_action(animation,"travel");ASSERT_NE(action,nullptr);
    action->loop=ANIMATION_LOOP_ONCE;action->clamp=true;ASSERT_TRUE(animation_action_play(action));
    ASSERT_TRUE(scene3d_animation_automatic(animation,true));
    EXPECT_FALSE(scene3d_animation_update(animation,.5));
    ASSERT_NE(doc.state,nullptr);auto* scheduler=doc.state->animation_scheduler;ASSERT_NE(scheduler,nullptr);
    double now=scheduler->current_time;
    EXPECT_TRUE(animation_scheduler_tick(scheduler,now+.5,nullptr));pixel(snapshot(),76,64,255,0,0);
    EXPECT_FALSE(animation_scheduler_tick(scheduler,now+1,nullptr));pixel(snapshot(),88,64,255,0,0);
    EXPECT_EQ(scheduler->count,0);EXPECT_FALSE(animation_mixer_active(action->mixer));
}
TEST_F(Scene3dTest, NativeBoneAndMorphPoseChangePixelsWithoutReallocatingGeometry) {
    ASSERT_NE(load_page("test/demo/scene3d/shared-animation.html",500,560),nullptr);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"deformation");ASSERT_NE(scene,nullptr);
    ASSERT_NE(scene3d_snapshot(scene,&ui,400,260,1),nullptr)<<scene3d_diagnostic(scene);
    auto* animation=scene3d_animations(scene);ASSERT_NE(animation,nullptr);
    ASSERT_TRUE(scene3d_animation_seek(animation,0));
    ImageSurface* first=scene3d_snapshot(scene,&ui,400,260,1);ASSERT_NE(first,nullptr)<<scene3d_diagnostic(scene);
    // retain the immutable first pose while a second generation is published.
    image_surface_snapshot_retain(first);
    Scene3dStats before{},after{};ASSERT_TRUE(scene3d_stats(scene,&before));
    ASSERT_TRUE(scene3d_animation_seek(animation,1));
    ImageSurface* second=scene3d_snapshot(scene,&ui,400,260,1);ASSERT_NE(second,nullptr)<<scene3d_diagnostic(scene);
    unsigned changed[2]={};
    for(unsigned y=0;y<260;y++) for(unsigned x=0;x<400;x++)
        if(memcmp((uint8_t*)first->pixels+y*first->pitch+x*4,(uint8_t*)second->pixels+y*second->pitch+x*4,3)) changed[x/200]++;
    EXPECT_GT(changed[0],600u);EXPECT_GT(changed[1],600u);ASSERT_TRUE(scene3d_stats(scene,&after));
    EXPECT_EQ(before.projection_generation,after.projection_generation);
    EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);
    image_surface_snapshot_release(first);
    scene3d_context_lost(scene);ASSERT_NE(scene3d_snapshot(scene,&ui,400,260,1),nullptr)<<scene3d_diagnostic(scene);
    EXPECT_EQ(scene3d_animations(scene),animation);
}
TEST_F(Scene3dTest, MixedSvgAndNativeTimelinesPauseAndSeekIndependently) {
    ASSERT_NE(load_page("test/demo/scene3d/shared-animation.html",500,720),nullptr);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"deformation");ASSERT_NE(scene,nullptr);
    DomElement* wave=dom_find_element_by_id(page->root->as_element(),"wave");ASSERT_NE(wave,nullptr);
    ASSERT_NE(scene3d_snapshot(scene,&ui,400,260,1),nullptr);
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('native-pause').click();document.getElementById('svg-pause').click()")));
    ASSERT_TRUE(svg_animation_paused(wave));
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('native-seek').click();document.getElementById('svg-seek').click()")));
    auto* animation=scene3d_animations(scene);ASSERT_NE(animation,nullptr);
    auto* action=scene3d_animation_action(animation,"bend");ASSERT_NE(action,nullptr);
    EXPECT_NEAR(action->time,1,1e-9);EXPECT_NEAR(svg_animation_current_time(wave),.5,1e-9);
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('native-play').click()")));
    auto* scheduler=page->state->animation_scheduler;ASSERT_NE(scheduler,nullptr);
    animation_scheduler_tick(scheduler,scheduler->current_time+.25,nullptr);
    EXPECT_NEAR(action->time,1.25,1e-6);EXPECT_NEAR(svg_animation_current_time(wave),.5,1e-9);
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('native-pause').click();document.getElementById('svg-play').click()")));
    animation_scheduler_tick(scheduler,scheduler->current_time+.25,nullptr);
    EXPECT_NEAR(action->time,1.25,1e-6);EXPECT_NEAR(svg_animation_current_time(wave),.75,1e-6);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        document.getElementById('native-pause').click();
        document.getElementById('deformation').addEventListener('finished',event=>
            document.getElementById('deformation').setAttribute('data-finished',event.clip+':'+event.direction));
    )JS")));
    action->loop=ANIMATION_LOOP_ONCE;action->clamp=true;
    ASSERT_TRUE(scene3d_animation_update(animation,1));pump_js();
    EXPECT_STREQ(scene->get_attribute("data-finished"),"bend:1");
}
TEST_F(Scene3dTest, RingworldAnimatesAndKeepsOrbitPanZoomIndependent) {
    ASSERT_NE(load_page("test/demo/scene3d/ringworld.ls",1120,900),nullptr);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"ringworld");ASSERT_NE(scene,nullptr);
    ASSERT_STREQ(scene->get_attribute("data-ready"),"true");
    ASSERT_EQ(page->js.runtime,page->lambda_runtime);
    gc_collect(runtime_heap(page->lambda_runtime)->gc,nullptr,0);
    ImageSurface* first=scene3d_snapshot(scene,&ui,624,342,1);ASSERT_NE(first,nullptr)<<scene3d_diagnostic(scene);
    image_surface_snapshot_retain(first);
    auto* animation=scene3d_animations(scene);ASSERT_NE(animation,nullptr);
    auto* action=scene3d_animation_action(animation,"orbits");ASSERT_NE(action,nullptr);
    auto* scheduler=page->state->animation_scheduler;ASSERT_NE(scheduler,nullptr);
    animation_scheduler_tick(scheduler,scheduler->current_time+1,nullptr);
    EXPECT_NEAR(action->time,1,1e-6);
    ImageSurface* second=scene3d_snapshot(scene,&ui,624,342,1);ASSERT_NE(second,nullptr);
    unsigned changed=0;
    for(unsigned y=0;y<342;y++) for(unsigned x=0;x<624;x++)
        if(memcmp((uint8_t*)first->pixels+y*first->pitch+x*4,(uint8_t*)second->pixels+y*second->pitch+x*4,3)) changed++;
    EXPECT_GT(changed,1000u);image_surface_snapshot_release(first);
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('play').click()")));
    animation_scheduler_tick(scheduler,scheduler->current_time+1,nullptr);
    EXPECT_NEAR(action->time,1,1e-6);
    auto button=[&](EventType type,float x,float y) {
        RdtEvent event={};event.type=type;event.mouse_button.x=x;event.mouse_button.y=y;
        event.mouse_button.button=0;event.mouse_button.clicks=1;handle_event(&ui,page,&event);
    };
    auto drag=[&]() {
        button(RDT_EVENT_MOUSE_DOWN,500,400);
        RdtEvent move={};move.type=RDT_EVENT_MOUSE_MOVE;move.mouse_position.x=560;move.mouse_position.y=430;handle_event(&ui,page,&move);
        button(RDT_EVENT_MOUSE_UP,560,430);
    };
    ASSERT_FALSE(item_is_error(run_js("globalThis.cameraBefore=ringworld.camera.position.clone()")));
    drag();
    ASSERT_FALSE(item_is_error(run_js("if(ringworld.camera.position.distanceTo(cameraBefore)<.1)throw new Error('native scene drag did not orbit')")));
    ASSERT_FALSE(item_is_error(run_js("if(document.activeElement!==document.getElementById('ringworld'))throw new Error('scene did not receive keyboard focus')")));
    auto key=[&](int code,int mods=0) {
        RdtEvent event={};event.type=RDT_EVENT_KEY_DOWN;event.key.key=code;event.key.mods=mods;handle_event(&ui,page,&event);
        event.type=RDT_EVENT_KEY_UP;handle_event(&ui,page,&event);
    };
    ASSERT_FALSE(item_is_error(run_js("globalThis.keyTargetBefore=ringworld.controls.target.clone();globalThis.tiltBefore=ringworld.controls.getPolarAngle()")));
    key(RDT_KEY_RIGHT);key(RDT_KEY_UP,RDT_MOD_SHIFT);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        if(ringworld.controls.target.distanceTo(keyTargetBefore)<.001)throw new Error('arrow key did not pan');
        if(Math.abs(ringworld.controls.getPolarAngle()-tiltBefore)<.001)throw new Error('shift-arrow did not tilt');
    )JS")));
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('pan-mode').click();globalThis.targetBefore=ringworld.controls.target.clone()")));
    drag();
    ASSERT_FALSE(item_is_error(run_js("if(ringworld.controls.target.distanceTo(targetBefore)<.1)throw new Error('pan mode did not move the target')")));
    ASSERT_NE(scene3d_snapshot(scene,&ui,624,342,1),nullptr)<<scene3d_diagnostic(scene);
    EXPECT_EQ(scene3d_animations(scene),animation);EXPECT_NEAR(action->time,1,1e-6);
    ASSERT_FALSE(item_is_error(run_js("globalThis.zoomBefore=ringworld.camera.position.distanceTo(ringworld.controls.target)")));
    RdtEvent scroll={};scroll.type=RDT_EVENT_SCROLL;scroll.scroll.x=500;scroll.scroll.y=400;scroll.scroll.yoffset=1;handle_event(&ui,page,&scroll);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        if(Math.abs(ringworld.camera.position.distanceTo(ringworld.controls.target)-zoomBefore)<.01)throw new Error('wheel did not zoom');
        document.getElementById('tilt-up').click();document.getElementById('orbit-right').click();
        document.getElementById('zoom-in').click();document.getElementById('zoom-out').click();
        document.getElementById('top').click();
        if(ringworld.controls.getPolarAngle()>.17)throw new Error('top preset');
        document.getElementById('side').click();
        if(Math.abs(ringworld.controls.getPolarAngle()-Math.PI/2)>1e-6)throw new Error('side preset');
        for(let i=0;i<50;i++)ringworld.zoom(.5);
        if(Math.abs(ringworld.controls.getDistance()-7)>1e-6)throw new Error('minimum zoom');
        for(let i=0;i<50;i++)ringworld.zoom(2);
        if(Math.abs(ringworld.controls.getDistance()-38)>1e-6)throw new Error('maximum zoom');
        ringworld.orbit(0,-100);
        if(ringworld.controls.getPolarAngle()<.0799)throw new Error('tilt limit');
        document.getElementById('reset').click();
        if(ringworld.camera.position.distanceTo({x:7.5,y:4.8,z:11})>1e-6||ringworld.controls.target.length()>1e-6)
            throw new Error('reset camera and pan');
        document.getElementById('rewind').click();
    )JS")));
    EXPECT_NEAR(action->time,0,1e-6);
    key(RDT_KEY_SPACE);
    animation_scheduler_tick(scheduler,scheduler->current_time+.25,nullptr);
    EXPECT_NEAR(action->time,.25,1e-6);
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
TEST_F(Scene3dTest, CssPreservedPlanesUseDepthInsteadOfDomOrder) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/depth-order.html", 440, 200), nullptr);
    render_html_doc(&ui, page->view_tree, nullptr);
    pixel(ui.surface, 100, 100, 255, 0, 0);
    pixel(ui.surface, 320, 100, 255, 0, 0);
}

TEST_F(Scene3dTest, TransformedBackgroundsClipToTheirPaintBoxAndSvgZeroAxesStayZero) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/background-clip.html", 220, 100), nullptr);
    for (const char* id : {"repeat-x", "repeat-y"}) {
        DomElement* repeated = dom_find_element_by_id(page->root->as_element(), id);
        ASSERT_NE(repeated, nullptr);
        ASSERT_NE(repeated->boundary()->background, nullptr);
        bool horizontal = strcmp(id, "repeat-x") == 0;
        EXPECT_EQ(repeated->boundary()->background->bg_repeat_x,
            horizontal ? CSS_VALUE_REPEAT : CSS_VALUE_NO_REPEAT);
        EXPECT_EQ(repeated->boundary()->background->bg_repeat_y,
            horizontal ? CSS_VALUE_NO_REPEAT : CSS_VALUE_REPEAT);
    }
    DomElement* svg = dom_find_element_by_id(page->root->as_element(), "zero");
    ASSERT_NE(svg, nullptr);
    EXPECT_FLOAT_EQ(svg->width, 0.0f);
    EXPECT_FLOAT_EQ(svg->height, 0.0f);
    for (unsigned density : {1u, 2u}) {
        ui_context_set_device_scale(&ui, density, density);
        ui.create_surface(220 * density, 100 * density);
        render_html_doc(&ui, page->view_tree, nullptr);
        pixel(ui.surface, 124 * density, 84 * density, 255, 255, 255);
        pixel(ui.surface, 174 * density, 84 * density, 255, 255, 255);
        pixel(ui.surface, 5 * density, 5 * density, 0, 255, 0);
        pixel(ui.surface, 30 * density, 30 * density, 255, 0, 0);
        pixel(ui.surface, 60 * density, 30 * density, 255, 255, 255);
        pixel(ui.surface, 130 * density, 30 * density, 255, 0, 0);
        pixel(ui.surface, 165 * density, 30 * density, 255, 255, 255);
        pixel(ui.surface, 25 * density, 65 * density, 128, 128, 128);
        pixel(ui.surface, 60 * density, 65 * density, 255, 255, 255);
    }
}

TEST_F(Scene3dTest, RecomputedVariablesAndBackgroundUrlsKeepRetainedStorageBounded) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/background-clip.html", 220, 100), nullptr);
    DomElement* element = dom_find_element_by_id(page->root->as_element(), "affine");
    ASSERT_NE(element, nullptr);
    LayoutContext layout = {};
    layout.doc = lam::up(page);
    layout.ui_context = lam::up(&ui);
    layout.pool = lam::up(page->view_tree->prop_pool.get());
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    const char* sources[] = {"--measured:12px", "width:calc(var(--measured) * 2)",
        "background-image:url('uv-grid.png')", "background-size:var(--measured) 24px"};
    CssDeclaration* declarations[4] = {};
    for (size_t i = 0; i < 4; i++) {
        declarations[i] = css_parse_declaration_text(sources[i], strlen(sources[i]), page->document_pool);
        ASSERT_NE(declarations[i], nullptr);
        resolve_css_property(declarations[i]->property_code, declarations[i], &layout);
    }
    const char* image = element->boundary()->background->image;
    ASSERT_NE(image, nullptr);
    size_t bytes_before = 0, count_before = 0;
    pool_get_stats(layout.pool, &bytes_before, &count_before);
    for (size_t repeat = 0; repeat < 32; repeat++) {
        for (size_t i = 1; i < 4; i++)
            resolve_css_property(declarations[i]->property_code, declarations[i], &layout);
        EXPECT_EQ(element->boundary()->background->image, image);
    }
    size_t bytes_after = 0, count_after = 0;
    pool_get_stats(layout.pool, &bytes_after, &count_after);
    // D4.5.1v4: temporary substitution trees cannot become retained view storage.
    EXPECT_EQ(bytes_after, bytes_before);
    EXPECT_EQ(count_after, count_before);
}

TEST_F(Scene3dTest, RepeatedFontResolutionDoesNotRetainLookupKeys) {
    FontContext* ctx = ui.font_ctx;
    ASSERT_NE(ctx, nullptr);
    FontStyleDesc style = {};
    style.family = "monospace, serif";
    style.size_px = 12;
    style.weight = FONT_WEIGHT_NORMAL;
    style.slant = FONT_SLANT_NORMAL;
    FontHandle* first = font_resolve(ctx, &style);
    ASSERT_NE(first, nullptr);
    font_handle_release(first);
    size_t warm = arena_total_used(ctx->arena);
    for (int i = 0; i < 4096; i++) {
        FontHandle* next = font_resolve(ctx, &style);
        EXPECT_EQ(next, first);
        if (next) font_handle_release(next);
    }
    EXPECT_EQ(arena_total_used(ctx->arena), warm);
}

TEST_F(Scene3dTest, ViewerClippedConcavePlanesRemainIndependentOfDomOrder) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/viewer-concave.html", 1280, 336), nullptr);
    for (unsigned density : {1u, 2u}) {
        ui_context_set_device_scale(&ui, density, density);
        ui.create_surface(1280 * density, 336 * density);
        render_html_doc(&ui, page->view_tree, nullptr);
        for (unsigned offset : {0u, 640u}) {
            pixel(ui.surface, (100 + offset) * density, 30 * density, 0, 128, 0);
            pixel(ui.surface, (120 + offset) * density, 140 * density, 0, 0, 255);
            pixel(ui.surface, (320 + offset) * density, 200 * density, 26, 26, 58);
        }
    }
}

TEST_F(Scene3dTest, ConcaveFloorRemainsVisibleBesideNonoverlappingWallInEitherDomOrder) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/concave-floor-order.html", 1280, 336), nullptr);
    for (unsigned density : {1u, 2u}) {
        ui_context_set_device_scale(&ui, density, density);
        ui.create_surface(1280 * density, 336 * density);
        render_html_doc(&ui, page->view_tree, nullptr);
        pixel(ui.surface, 320 * density, 104 * density, 0, 128, 0, 255, 2);
        pixel(ui.surface, 960 * density, 104 * density, 0, 128, 0, 255, 2);
    }
}

TEST_F(Scene3dTest, AnimatedCustomFiltersUpdateWithoutLayoutAndKeepOverridesAndGeometryDependencies) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/custom-lighting.html", 320, 100), nullptr);
    DomElement* paint = dom_find_element_by_id(page->root->as_element(), "paint");
    DomElement* geometry = dom_find_element_by_id(page->root->as_element(), "geometry");
    DomElement* scroll = dom_find_element_by_id(page->root->as_element(), "scroll");
    DomElement* tile = dom_find_element_by_id(page->root->as_element(), "tile");
    ASSERT_NE(paint, nullptr); ASSERT_NE(geometry, nullptr);
    ASSERT_NE(scroll, nullptr); ASSERT_NE(tile, nullptr);
    ASSERT_NE(state_store_create(page), nullptr);
    LayoutContext context = {};
    context.doc = lam::up(page); context.pool = lam::up(page->view_tree->prop_pool);
    context.ui_context = lam::up(&ui);
    for (DomElement* element : {paint, geometry, scroll}) {
        context.view = lam::up(static_cast<View*>(element)); context.elmt = lam::up(element);
        css_animation_resolve(element, &context);
    }
    AnimationScheduler* scheduler = page->state->animation_scheduler;
    ASSERT_NE(scheduler, nullptr);
    AnimationInstance *paint_animation = nullptr, *geometry_animation = nullptr, *scroll_animation = nullptr;
    for (AnimationInstance* instance = scheduler->first; instance; instance = instance->next) {
        if (instance->target == paint) paint_animation = instance;
        if (instance->target == geometry) geometry_animation = instance;
        if (instance->target == scroll) scroll_animation = instance;
    }
    ASSERT_NE(paint_animation, nullptr); ASSERT_NE(geometry_animation, nullptr);
    ASSERT_NE(scroll_animation, nullptr);
    for (float time : {.5f, .75f}) {
        paint_animation->layout_changed = false;
        geometry_animation->layout_changed = false;
        scroll_animation->layout_changed = false;
        css_animation_tick(paint_animation, time);
        css_animation_tick(geometry_animation, time);
        css_animation_tick(scroll_animation, time);
        EXPECT_FALSE(paint_animation->layout_changed);
        EXPECT_TRUE(geometry_animation->layout_changed);
        EXPECT_FALSE(scroll_animation->layout_changed);
        EXPECT_FALSE(tile->boundary()->background->bg_position_x_is_percent);
        EXPECT_NEAR(tile->boundary()->background->bg_position_x, time * 32, .001f);
        render_html_doc(&ui, page->view_tree, nullptr);
        unsigned intensity = time == .5f ? 127u : 191u;
        pixel(ui.surface, 40, 40, intensity, intensity, intensity, 255, 2);
        pixel(ui.surface, 140, 40, 64, 64, 64, 255, 2);
        pixel(ui.surface, 244, 20, 0, 255, 0);
        pixel(ui.surface, 280, 20, 255, 0, 0);
    }
}

TEST_F(Scene3dTest, CssCompositionAllocationFailurePreservesOwnershipAndRejectsPaint) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/depth-order.html", 440, 200), nullptr);
    RenderProfiler profiler = {};
    RasterRenderContext context;
    RenderFrameScope frame(&context, &ui, page->view_tree, &profiler);
    Css3dPaintContext composition(&context, lam::view_require_block(page->view_tree->root));
    dl_fill_rect(frame.list(), 0.0f, 0.0f, 20.0f, 20.0f, Color{0xffffffff});
    composition.flush();
    int original_count = dl_item_count(frame.list());
    memtrack_fault_inject(0);
    bool success = composition.compose();
    memtrack_fault_clear();
    EXPECT_FALSE(success);
    EXPECT_EQ(dl_item_count(frame.list()), original_count);
    EXPECT_TRUE(dl_validate_or_log(frame.list(), "css3d rejected allocation"));
}

TEST_F(Scene3dTest, ProjectedClipBoundsLimitSamplingAndRestoreNestedScopes) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/depth-order.html", 440, 200), nullptr);
    RenderProfiler profiler = {};
    RasterRenderContext context;
    RenderFrameScope frame(&context, &ui, page->view_tree, &profiler);
    Rect original = render_painter_projection_viewport(&context);
    RdtPath* rectangle = rdt_path_new();
    rdt_path_add_rect(rectangle, 20, 40, 100, 50, 0, 0);
    rc_push_clip(&context, rectangle, nullptr);
    Rect outer = render_painter_projection_viewport(&context);
    EXPECT_FLOAT_EQ(outer.x, 19); EXPECT_FLOAT_EQ(outer.y, 39);
    EXPECT_FLOAT_EQ(outer.width, 102); EXPECT_FLOAT_EQ(outer.height, 52);
    rc_push_clip(&context, rectangle, nullptr);
    rc_pop_clip(&context);
    EXPECT_FLOAT_EQ(render_painter_projection_viewport(&context).width, outer.width);
    rc_pop_clip(&context);
    rdt_path_free(rectangle);

    RdtPath* crossing = rdt_path_new();
    rdt_path_move_to(crossing, -2, 10); rdt_path_line_to(crossing, 2, 10);
    rdt_path_line_to(crossing, 2, 20); rdt_path_close(crossing);
    RdtMatrix transform = {1, 0, 0, 0, 1, 0, 1, 0, 1};
    rc_push_clip(&context, crossing, &transform);
    Rect clipped = render_painter_projection_viewport(&context);
    EXPECT_GT(clipped.width, 0); EXPECT_LT(clipped.width, 4);
    EXPECT_GT(clipped.height, 0); EXPECT_LT(clipped.height, 32);
    rc_pop_clip(&context);
    rdt_path_free(crossing);
    Rect restored = render_painter_projection_viewport(&context);
    EXPECT_FLOAT_EQ(restored.x, original.x); EXPECT_FLOAT_EQ(restored.y, original.y);
    EXPECT_FLOAT_EQ(restored.width, original.width); EXPECT_FLOAT_EQ(restored.height, original.height);
    EXPECT_TRUE(dl_validate_or_log(frame.list(), "projected clip bounds"));
}

TEST_F(Scene3dTest, CssIntersectingPlanesAlphaFlatteningBackfacesAndViewerClipping) {
    ASSERT_NE(load_page("test/demo/doom/tests/render/context-planes.html", 880, 660), nullptr);
    const struct { unsigned x, y, r, g, b; } probes[] = {
        {80, 110, 255, 0, 0}, {140, 110, 0, 0, 255},
        {300, 110, 255, 0, 0}, {360, 110, 0, 0, 255},
        {550, 110, 128, 0, 127}, {770, 110, 255, 0, 0},
        {110, 330, 0, 0, 255}, {330, 330, 255, 0, 0},
        {550, 330, 255, 0, 0}, {770, 330, 0, 0, 255}, {705, 330, 255, 0, 0},
        {80, 500, 255, 0, 0}, {80, 600, 0, 0, 255}, {140, 500, 255, 0, 0},
        {300, 500, 255, 0, 0}, {300, 600, 0, 0, 255}, {360, 500, 255, 0, 0},
        {530, 520, 255, 0, 0}, {570, 520, 0, 255, 0}, {530, 580, 0, 0, 255},
        {570, 580, 255, 255, 0}, {705, 500, 255, 0, 0}, {830, 500, 0, 255, 0},
        {705, 600, 0, 0, 255}, {830, 600, 255, 255, 0}, {770, 550, 0, 0, 255}
    };
    for (unsigned density : {1u, 2u}) {
        ui_context_set_device_scale(&ui, density, density);
        ASSERT_FLOAT_EQ(ui_context_raster_scale(&ui), density);
        ui.create_surface(880 * density, 660 * density);
        render_html_doc(&ui, page->view_tree, nullptr);
        for (const auto& probe : probes)
            pixel(ui.surface, probe.x * density, probe.y * density, probe.r, probe.g, probe.b);
    }
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
TEST_F(Scene3dTest, ResponsiveSvgConstrainsAuthoredWidthBeforeDerivingAutoHeight) {
    ASSERT_NE(load_page("test/html/svg_responsive_limits.html", 386, 360), nullptr);
    const struct { const char* id; float width; float height; } cases[] = {
        {"responsive", 326.0f, 326.0f * 270.0f / 350.0f},
        {"css-width", 326.0f, 326.0f * 270.0f / 350.0f},
        {"percent-width", 326.0f, 326.0f * 270.0f / 350.0f},
        {"both-auto", 326.0f, 326.0f * 270.0f / 350.0f},
        {"fixed-height", 326.0f, 270.0f},
        {"min-wins", 360.0f, 360.0f * 270.0f / 350.0f},
        {"unconstrained", 350.0f, 270.0f},
        {"content-box", 224.0f, 200.0f * 270.0f / 350.0f + 24.0f},
        {"border-box", 200.0f, 176.0f * 270.0f / 350.0f + 24.0f},
        {"zero", 0.0f, 0.0f},
        {"intrinsic-min-wins", 100.0f, 100.0f},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.id);
        DomElement* svg = dom_find_element_by_id(page->root->as_element(), entry.id);
        ASSERT_NE(svg, nullptr);
        EXPECT_NEAR(svg->width, entry.width, 0.02f);
        EXPECT_NEAR(svg->height, entry.height, 0.02f);
    }
    render_html_doc(&ui, page->view_tree, nullptr);
    ASSERT_NE(ui.surface, nullptr);
    // the final stripe stays visible while the card's right padding stays white.
    pixel(ui.surface, 350, 60, 0, 0, 255);
    pixel(ui.surface, 365, 60, 255, 255, 255);
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
TEST_F(Scene3dTest, FloatTargetsDepthTexturesAndFloatLinearFiltering) {
    ASSERT_NE(load_page("test/webgl/formats.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    EXPECT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_STREQ(result->get_attribute("data-checks"),"11");
}
TEST_F(Scene3dTest, ThreeTextureLoaderAndCanvasImageOverloads) {
    ASSERT_NE(load_page("test/webgl/images.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    EXPECT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_GE(strtol(result->get_attribute("data-checks"),nullptr,10),35);
}
TEST_F(Scene3dTest, ThreePbrEnvironmentShadowsAndMultipassProduceRealPixels) {
    ASSERT_NE(load_page("test/demo/scene3d/observatory.html",848,650),nullptr);
    DomElement* status=dom_find_element_by_id(page->root->as_element(),"status");ASSERT_NE(status,nullptr);
    ASSERT_STREQ(status->get_attribute("data-ready"),"true")<<status->get_attribute("data-error");
    RenderOutputTarget target={};target.kind=RENDER_OUTPUT_SCREEN;
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    unsigned colored=0;
    for(unsigned y=160;y<570;y++) for(unsigned x=160;x<680;x++) {
        const uint8_t* pixel=(const uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4;
        if(pixel[0]>65&&pixel[1]>65&&pixel[2]>35) colored++;
    }
    EXPECT_GT(colored,15000u);
    NativeGlStats stats={};ASSERT_TRUE(radiant_webgl_stats(dom_find_element_by_id(page->root->as_element(),"observatory"),&stats));
    EXPECT_GT(stats.draws,30u);EXPECT_GT(stats.shader_normalize_calls,12u);
    RecordProperty("pbr_draws",(int64_t)stats.draws);
    RecordProperty("pbr_gpu_bytes",(int64_t)stats.gpu_bytes);
    RecordProperty("pbr_cpu_bytes",(int64_t)stats.cpu_bytes);
    scene3d_test_capture("observatory",ui.surface);
    for(const char* sample:{"observatory.sample(1.25)","observatory.sample(3.5)"}) {
        ASSERT_FALSE(item_is_error(run_js(sample)));
        ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
        scene3d_test_capture(strstr(sample,"1.25")?"observatory-1.25":"observatory-3.5",ui.surface);
    }
    size_t bytes=ui.surface->pitch*ui.surface->height;
    auto* baseline=(uint8_t*)mem_alloc(bytes,MEM_CAT_RENDER);ASSERT_NE(baseline,nullptr);memcpy(baseline,ui.surface->pixels,bytes);
    ASSERT_FALSE(item_is_error(run_js("observatory.setShadows(false)")));
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    unsigned shadow_changes=0;
    for(unsigned y=400;y<580;y++) for(unsigned x=120;x<720;x++)
        if(memcmp(baseline+y*ui.surface->pitch+x*4,(uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4,3)) shadow_changes++;
    EXPECT_GT(shadow_changes,100u);scene3d_test_capture("observatory-no-shadow",ui.surface);
    memcpy(baseline,ui.surface->pixels,bytes);
    ASSERT_FALSE(item_is_error(run_js("observatory.composer.passes[1].enabled=false;observatory.render()")));
    ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
    unsigned pass_changes=0;
    for(size_t offset=0;offset<bytes;offset+=4)
        if(memcmp(baseline+offset,(uint8_t*)ui.surface->pixels+offset,3)) pass_changes++;
    EXPECT_GT(pass_changes,100u);scene3d_test_capture("observatory-no-fxaa",ui.surface);mem_free(baseline);
    if(getenv("LAMBDA_SCENE3D_MEASURE_FRAMES")) {
#ifndef NDEBUG
        FAIL()<<"Frame-pacing measurements require the release runner";
#else
        ASSERT_FALSE(item_is_error(run_js("observatory.setShadows(true);observatory.composer.passes[1].enabled=true")));
        auto cache_counts=[](uint64_t* entries,uint64_t* bytes) {
            MemSnapshot* snapshot=mem_snapshot_capture(nullptr);
            if(snapshot) for(uint32_t i=0;i<snapshot->count;i++) if(!strcmp(snapshot->samples[i].label,"rdt.vector.caches")) {
                *entries=snapshot->samples[i].alloc_count;*bytes=snapshot->samples[i].bytes_in_use;
            }
            mem_snapshot_free(snapshot);
        };
        uint64_t cache_entries_before=0,cache_bytes_before=0,cache_entries_after=0,cache_bytes_after=0;
        cache_counts(&cache_entries_before,&cache_bytes_before);
        ASSERT_FALSE(item_is_error(run_js("globalThis.measureFrame=()=>{const begin=performance.now();observatory.mixer.update(1/60);globalThis.sampleMs=performance.now()-begin;observatory.render()}")));
        RootFrame roots(3);Rooted<Item> global(roots,dom_realm_global());
        Rooted<Item> callback(roots,dom_realm_get_name(global.get(),"measureFrame")),sample(roots,ItemNull);
        // cross the 128-entry image cache capacity to measure its steady-state bound.
        constexpr unsigned frame_count=144;
        double frames[frame_count],sampling[frame_count];
        for(unsigned i=0;i<frame_count;i++) {
            struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
            ASSERT_FALSE(item_is_error(dom_realm_call(callback.get(),ItemNull,nullptr,0)));
            sample.set(dom_realm_get_name(global.get(),"sampleMs"));ASSERT_TRUE(item_try_to_double(sample.get(),&sampling[i]));
            ASSERT_EQ(render_output_render_view_tree_to_target(&ui,page->view_tree,&target),0);
            clock_gettime(CLOCK_MONOTONIC,&end);frames[i]=(end.tv_sec-start.tv_sec)*1000.+(end.tv_nsec-start.tv_nsec)/1000000.;
        }
        auto compare=[](const void* a,const void* b)->int {return (*(const double*)a>*(const double*)b)-(*(const double*)a<*(const double*)b);};
        qsort(frames,frame_count,sizeof(double),compare);qsort(sampling,frame_count,sizeof(double),compare);
        // GTest's numeric overload is integral; retain sub-millisecond sampling measurements as text.
        auto record_ms=[](const char* key,double milliseconds) {
            char value[64];str_fmt(value,sizeof(value),"%.6f",milliseconds);RecordProperty(key,value);
        };
        RecordProperty("pbr_measured_frames",frame_count);
        record_ms("pbr_frame_p50_ms",frames[frame_count/2]);record_ms("pbr_frame_p95_ms",frames[frame_count*95/100]);
        record_ms("pbr_sampling_p50_ms",sampling[frame_count/2]);record_ms("pbr_sampling_p95_ms",sampling[frame_count*95/100]);
        cache_counts(&cache_entries_after,&cache_bytes_after);
        RecordProperty("vector_cache_entries_before",(int64_t)cache_entries_before);
        RecordProperty("vector_cache_entries_after",(int64_t)cache_entries_after);
        RecordProperty("vector_cache_metadata_bytes_before",(int64_t)cache_bytes_before);
        RecordProperty("vector_cache_metadata_bytes_after",(int64_t)cache_bytes_after);
        EXPECT_LE(cache_entries_after,cache_entries_before+128);
        NativeGlStats final_stats{};ASSERT_TRUE(radiant_webgl_stats(dom_find_element_by_id(page->root->as_element(),"observatory"),&final_stats));
        RecordProperty("pbr_final_cpu_bytes",(int64_t)final_stats.cpu_bytes);
        RecordProperty("pbr_final_gpu_bytes",(int64_t)final_stats.gpu_bytes);
        EXPECT_LE(final_stats.cpu_bytes,stats.cpu_bytes+1024*1024);
        EXPECT_LE(final_stats.gpu_bytes,stats.gpu_bytes+1024*1024);
#endif
    }
}
TEST_F(Scene3dTest, OrbitControlsRealInputCaptureCancellationAndRaycastAtCssDensity) {
    ASSERT_NE(load_page("test/demo/scene3d/observatory.html",848,650),nullptr);
    DomElement* canvas=dom_find_element_by_id(page->root->as_element(),"observatory");ASSERT_NE(canvas,nullptr);
    DomElement* status=dom_find_element_by_id(page->root->as_element(),"status");ASSERT_NE(status,nullptr);
    ASSERT_STREQ(status->get_attribute("data-ready"),"true");
    ASSERT_FALSE(item_is_error(run_js("globalThis.cameraBefore=observatory.camera.position.clone()")));
    auto button=[&](EventType type,float x,float y) {
        RdtEvent event={};event.type=type;event.mouse_button.x=x;event.mouse_button.y=y;
        event.mouse_button.button=0;event.mouse_button.clicks=1;handle_event(&ui,page,&event);
    };
    button(RDT_EVENT_MOUSE_DOWN,350,300);
    RdtEvent move={};move.type=RDT_EVENT_MOUSE_MOVE;move.mouse_position.x=410;move.mouse_position.y=320;handle_event(&ui,page,&move);
    button(RDT_EVENT_MOUSE_UP,410,320);
    ASSERT_FALSE(item_is_error(run_js("if(observatory.camera.position.distanceTo(cameraBefore)<.1)throw new Error('drag did not orbit')")));
    EXPECT_STREQ(status->get_attribute("data-gotpointercapture"),"true");
    EXPECT_STREQ(status->get_attribute("data-lostpointercapture"),"true");
    ASSERT_FALSE(item_is_error(run_js("globalThis.zoomBefore=observatory.camera.position.distanceTo(observatory.controls.target)")));
    RdtEvent scroll={};scroll.type=RDT_EVENT_SCROLL;scroll.scroll.x=350;scroll.scroll.y=300;scroll.scroll.yoffset=1;handle_event(&ui,page,&scroll);
    ASSERT_FALSE(item_is_error(run_js("if(Math.abs(observatory.camera.position.distanceTo(observatory.controls.target)-zoomBefore)<.01)throw new Error('wheel did not zoom')")));
    radiant_dispatch_event_sim_pointer(&ui,(View*)canvas,"pointerdown",350,300,0,1,0,"mouse");
    radiant_dispatch_event_sim_pointer(&ui,(View*)canvas,"pointercancel",350,300,0,0,0,"mouse");
    EXPECT_STREQ(status->get_attribute("data-pointercancel"),"true");
    for(unsigned density=1;density<=2;density++) {
        if(density==2) {
            ASSERT_FALSE(item_is_error(run_js(R"JS(
                observatory.resize(400,250,2);
                observatory.renderer.domElement.style.width='400px';observatory.renderer.domElement.style.height='250px';
                observatory.renderer.domElement.style.transformOrigin='0 0';
                observatory.renderer.domElement.style.transform='translate(20px,10px) scale(0.8)';
            )JS")));
            layout_html_doc(&ui,page,false);
        }
        ASSERT_FALSE(item_is_error(run_js(R"JS(
            observatory.sample(.75);
            const satellite=observatory.satellites[1];
            const point=satellite.position.clone().applyMatrix4(satellite.parent.matrixWorld).project(observatory.camera);
            const rect=observatory.renderer.domElement.getBoundingClientRect();
            document.getElementById('status').setAttribute('data-pick-x',rect.left+(point.x+1)*rect.width/2);
            document.getElementById('status').setAttribute('data-pick-y',rect.top+(1-point.y)*rect.height/2);
        )JS")));
        float x=strtof(status->get_attribute("data-pick-x"),nullptr),y=strtof(status->get_attribute("data-pick-y"),nullptr);
        button(RDT_EVENT_MOUSE_DOWN,x,y);button(RDT_EVENT_MOUSE_UP,x,y);
        EXPECT_STREQ(status->get_attribute("data-selection"),"Satellite 2")<<"density "<<density;
    }
}
TEST_F(Scene3dTest, RichThreeSceneRecoversDuringCrossfadeWithBoundedResources) {
    ASSERT_NE(load_page("test/demo/scene3d/observatory.html",848,650),nullptr);
    DomElement* status=dom_find_element_by_id(page->root->as_element(),"status");ASSERT_NE(status,nullptr);
    ASSERT_STREQ(status->get_attribute("data-ready"),"true");
    DomElement* canvas=dom_find_element_by_id(page->root->as_element(),"observatory");ASSERT_NE(canvas,nullptr);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        const audit=observatory;
        const alternate=audit.clip.clone();alternate.name='crossfade';alternate.duration=4;
        audit.sample(1.25);
        audit.mixer.clipAction(alternate).play().crossFadeFrom(audit.mixer.clipAction(audit.clip),.6,true);
        audit.mixer.update(.3);audit.render();
        globalThis.recoveryTime=audit.mixer.time;
        globalThis.recoveryPose=audit.scene.getObjectByName('outer').quaternion.toArray().join();
        const extension=audit.renderer.getContext().getExtension('WEBGL_lose_context');
        audit.renderer.domElement.addEventListener('webglcontextlost',event=> {
            event.preventDefault();setTimeout(()=>extension.restoreContext(),0);
        });
        globalThis.beginRecovery=()=>extension.loseContext();
        globalThis.checkRecovery=()=> {
            if(audit.mixer.time!==recoveryTime||audit.scene.getObjectByName('outer').quaternion.toArray().join()!==recoveryPose)
                throw new Error('restoration advanced or reset CPU playback');
            audit.render();if(audit.renderer.getContext().getError())throw new Error('restored PBR GL error');
            const previous=audit.composer.readBuffer.clone();audit.renderer.setRenderTarget(previous);
            audit.composer.render(0);
            if(audit.renderer.getRenderTarget()!==previous)throw new Error('composer did not restore application target');
            audit.renderer.setRenderTarget(null);previous.dispose();
        };
    )JS")));
    NativeGlStats first{},current{};ASSERT_TRUE(radiant_webgl_stats(canvas,&first));
    RecordProperty("initial_cpu_bytes",(int64_t)first.cpu_bytes);
    RecordProperty("initial_gpu_bytes",(int64_t)first.gpu_bytes);
    RecordProperty("initial_resources",(int64_t)first.resources);
    for(unsigned i=0;i<3;i++) {
        ASSERT_FALSE(item_is_error(run_js("beginRecovery()")));
        pump_js();
        ASSERT_STREQ(status->get_attribute("data-restored"),i==0?"1":i==1?"2":"3");
        ASSERT_FALSE(item_is_error(run_js("checkRecovery()")));
        ASSERT_TRUE(radiant_webgl_stats(canvas,&current));
        EXPECT_LE(current.resources,first.resources+12);EXPECT_LE(current.gpu_bytes,first.gpu_bytes+1024*1024);
        EXPECT_LE(current.cpu_bytes,first.cpu_bytes+1024*1024);
    }
    RecordProperty("restored_cpu_bytes",(int64_t)current.cpu_bytes);
    RecordProperty("restored_gpu_bytes",(int64_t)current.gpu_bytes);
    RecordProperty("restored_resources",(int64_t)current.resources);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        const second=document.createElement('canvas');document.body.appendChild(second);
        const secondStatus=document.createElement('span');document.body.appendChild(secondStatus);
        globalThis.secondObservatory=createObservatory(second,secondStatus,document.createElement('button'));
        globalThis.secondCanvas=second;globalThis.secondStatus=secondStatus;
    )JS")));
    pump_js();
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        if(secondStatus.getAttribute('data-ready')!=='true')throw new Error('second PBR scene did not load');
        if(secondObservatory.renderer.getContext()===observatory.renderer.getContext())throw new Error('shared contexts');
        for(let i=0;i<4;i++) {secondObservatory.resize(400,250,i%2+1);secondObservatory.sample(i*.2);}
        secondObservatory.dispose();observatory.dispose();
    )JS")));
    ASSERT_TRUE(radiant_webgl_stats(canvas,&current));
    EXPECT_LE(current.resources,8u);EXPECT_LE(current.gpu_bytes,1024u);
    RecordProperty("disposed_cpu_bytes",(int64_t)current.cpu_bytes);
    RecordProperty("disposed_gpu_bytes",(int64_t)current.gpu_bytes);
    RecordProperty("disposed_resources",(int64_t)current.resources);
}
TEST_F(Scene3dTest, DeclaredNativeAnimationBridgeMatchesPinnedMixerAndReleasesActions) {
    ASSERT_NE(load_page("test/webgl/native-animation.html"),nullptr);
    DomElement* result=dom_find_element_by_id(page->root->as_element(),"result");ASSERT_NE(result,nullptr);
    ASSERT_STREQ(result->get_attribute("data-result"),"ready-for-gc");
    ASSERT_NE(page->state,nullptr);ASSERT_NE(page->state->animation_scheduler,nullptr);
    ASSERT_FALSE(item_is_error(run_js("animationFrameProbe()")));
    for(double time:{1000.,1250.,1500.}) ASSERT_EQ(js_animation_frame_flush(time),1);
    ASSERT_FALSE(item_is_error(run_js(R"JS(
        if(autoSamples.length!==3||Math.abs(autoSamples[1][1]-.25)>1e-9||Math.abs(autoSamples[2][1]-.5)>1e-9||
           Math.abs(autoSamples[2][2])>2e-6)throw new Error('native sampling must precede rAF exactly once');
    )JS")));
    Runtime* runtime=dom_document_script_runtime(page);ASSERT_NE(runtime,nullptr);
    for(unsigned i=0;i<3;i++) gc_collect(runtime_heap(runtime)->gc,nullptr,0);
    EXPECT_FALSE(item_is_error(run_js("animationLifetimeCheck()")));
    EXPECT_STREQ(result->get_attribute("data-result"),"passed");
    EXPECT_GE(strtol(result->get_attribute("data-checks"),nullptr,10),43);
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

TEST(AnimationCore, GltfHermiteUsesSegmentDurationAndNormalizesCubicQuaternions) {
    const double times[]={2,5},values[]={0,0},incoming[]={0,-2},outgoing[]={2,0};
    AnimationTrackView track={times,values,nullptr,incoming,outgoing,2,1,ANIMATION_NUMBER,ANIMATION_HERMITE};
    ASSERT_TRUE(animation_track_validate(track));double result[4]={};
    ASSERT_TRUE(animation_track_sample(track,3.5,result));EXPECT_DOUBLE_EQ(result[0],1.5);
    ASSERT_TRUE(animation_track_sample(track,2,result));EXPECT_DOUBLE_EQ(result[0],0);
    ASSERT_TRUE(animation_track_sample(track,5,result));EXPECT_DOUBLE_EQ(result[0],0);
    track.in_tangents=nullptr;EXPECT_FALSE(animation_track_validate(track));
    const double q[]={0,0,0,1,0,0,1,0},zero[8]={};
    track={times,q,nullptr,zero,zero,2,4,ANIMATION_QUATERNION,ANIMATION_HERMITE};
    ASSERT_TRUE(animation_track_validate(track));ASSERT_TRUE(animation_track_sample(track,3.5,result));
    EXPECT_NEAR(result[2],sqrt(.5),1e-12);EXPECT_NEAR(result[3],sqrt(.5),1e-12);
}

static unsigned asset_changed_pixels(ImageSurface* a,ImageSurface* b) {
    unsigned count=0;
    for(unsigned y=0;y<(unsigned)a->height;y++) for(unsigned x=0;x<(unsigned)a->width;x++)
        if(memcmp((uint8_t*)a->pixels+y*a->pitch+x*4,(uint8_t*)b->pixels+y*b->pitch+x*4,4)) count++;
    return count;
}

TEST_F(Scene3dTest, ImportedAssetGalleryRendersTexturedObjAndPlaysGltfAndA3dClips) {
    ASSERT_NE(load_page("test/demo/scene3d/asset-gallery.ls",1200,480),nullptr);
    const char* ids[]={"obj-view","gltf-view","a3d-view"};
    const char* clips[]={nullptr,"gltf-asset-clip-0","a3d-asset-clip-0"};
    for(unsigned i=0;i<3;i++) {
        SCOPED_TRACE(ids[i]);auto* scene=dom_find_element_by_id(page->root->as_element(),ids[i]);ASSERT_NE(scene,nullptr);
        ImageSurface* initial=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(initial,nullptr)<<scene3d_diagnostic(scene);
        Scene3dStats before{},after{};ASSERT_TRUE(scene3d_stats(scene,&before));EXPECT_EQ(before.meshes,i==1?3u:1u);
        EXPECT_EQ(before.textures,i<2?1u:0u);
        // count foreground pixels against the authored background, and require texture/color variation.
        unsigned foreground=0,variation=0;const uint8_t* background=(uint8_t*)initial->pixels;
        for(unsigned y=0;y<320;y++) for(unsigned x=0;x<360;x++) {
            auto* p=(uint8_t*)initial->pixels+y*initial->pitch+x*4;
            if(memcmp(p,background,3)) {foreground++;if(p[0]>p[2]) variation++;}
        }
        EXPECT_GT(foreground,4000u);EXPECT_GT(variation,500u);
        if(i==1) {pixel(initial,249,110,63,231,149);pixel(initial,268,219,63,231,149);}
        scene3d_test_capture(ids[i],initial);
        if(!clips[i]) continue;
        auto* animation=scene3d_animations(scene);ASSERT_NE(animation,nullptr);
        auto* action=scene3d_animation_action(animation,clips[i]);ASSERT_NE(action,nullptr);
        ASSERT_TRUE(scene3d_animation_seek(animation,0));
        ImageSurface* first=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(first,nullptr);image_surface_snapshot_retain(first);
        ASSERT_TRUE(scene3d_animation_seek(animation,1));
        ImageSurface* second=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(second,nullptr)<<scene3d_diagnostic(scene);image_surface_snapshot_retain(second);
        EXPECT_GT(asset_changed_pixels(first,second),4000u);
        scene3d_test_capture(i==1?"gltf-pose-0":"a3d-pose-0",first);
        scene3d_test_capture(i==1?"gltf-pose-1":"a3d-pose-1",second);
        ASSERT_TRUE(scene3d_stats(scene,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);
        EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
        scene3d_context_lost(scene);ImageSurface* restored=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(restored,nullptr);
        EXPECT_EQ(scene3d_animations(scene),animation);EXPECT_EQ(scene3d_animation_action(animation,clips[i]),action);
        EXPECT_EQ(asset_changed_pixels(second,restored),0u);
        ASSERT_TRUE(animation_action_stop(action));ASSERT_TRUE(scene3d_animation_seek(animation,0));
        ImageSurface* stopped=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(stopped,nullptr);EXPECT_EQ(asset_changed_pixels(first,stopped),0u);
        if(i==1) {
            // the embedded PNG has alpha, but default glTF materials must render it opaque.
            auto* material=dom_find_element_by_id(page->root->as_element(),"gltf-asset-material-0");ASSERT_NE(material,nullptr);
            ASSERT_TRUE(material->set_attribute("alpha-mode","blend"));page->mutation_epoch++;
            ImageSurface* blended=scene3d_snapshot(scene,&ui,360,320,1);ASSERT_NE(blended,nullptr);
            EXPECT_GT(asset_changed_pixels(first,blended),4000u);
        }
        image_surface_snapshot_release(first);image_surface_snapshot_release(second);
    }
}
