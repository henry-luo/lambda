#include <gtest/gtest.h>
// the native-graphics cases observe gl_core's GLFW context directly
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../radiant/scene3d/scene3d.hpp"
#include "../radiant/scene3d/scene3d_math.hpp"
#include "../lib/image_hdr.h"
#include "../radiant/animation_mixer.hpp"
#include "../radiant/scene3d/scene3d_animation.hpp"
#include "../radiant/scene3d/scene3d_source.hpp"
#include "../radiant/scene3d/scene3d_physics.hpp"
#include "../lib/physics/physics_math.h"
#include "../lib/math_utils.h"
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
#include "../lambda/runtime/root_vector.h"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/dom/dom.h"
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/io/mark_editor.hpp"
#include "../lambda/module/radiant/radiant_dom_bridge.hpp"
#include "../lambda/module/radiant/radiant_history.hpp"
#include "../lambda/module/radiant/radiant_input_value.hpp"
#include "../lambda/module/radiant/radiant_webgl_bridge.hpp"
#include "../lib/mem.h"
#include "../lib/mem_factory.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_context.h"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/image.h"
#include "../lib/time_util.h"
#include "../lib/hash.h"
#include "../lib/font/font_internal.h"
#include <time.h>


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

TEST(Scene3dRotation, MatrixConversionPreservesHalfTurnsAndComposedRotations) {
    for(unsigned sample=0;sample<96;sample++) {
        PhysicsVec angles=sample<3?pmul(pbase(sample),acos(-1.0)):pv(.17*sample,-.31*sample,.53*sample);
        PhysicsQuat expected=peuler(angles);double rotation[9],q[4]={};
        for(unsigned column=0;column<3;column++){PhysicsVec axis=prot(expected,pbase(column));for(unsigned row=0;row<3;row++)rotation[row*3+column]=paxis(axis,row);}
        ASSERT_TRUE(math_rotation_quaternion(rotation,q));PhysicsQuat actual={q[0],q[1],q[2],q[3]};
        EXPECT_NEAR(fabs(actual.x*expected.x+actual.y*expected.y+actual.z*expected.z+actual.w*expected.w),1,1e-12);
        EXPECT_LT(plen(psub(prot(actual,{1,2,3}),prot(expected,{1,2,3}))),1e-11);
    }
    double invalid[9]={NAN},retained[4]={1,2,3,4};EXPECT_FALSE(math_rotation_quaternion(invalid,retained));
    EXPECT_DOUBLE_EQ(retained[0],1);EXPECT_DOUBLE_EQ(retained[3],4);
}

struct Scene3dNavigationOwner {
    Scene3dRuntime* runtime=nullptr;Scene3dNavigation* cache=nullptr;
    ~Scene3dNavigationOwner(){scene3d_navigation_destroy(cache);scene3d_runtime_destroy(runtime);}
    bool create(DomElement* root) {
        runtime=scene3d_runtime_create(root);if(!runtime)return false;
        cache=scene3d_navigation_create(root,runtime);return cache!=nullptr;
    }
};

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
    DomElement* navigation_asset(const char* id="routes",const char* data="{\"type\":\"grid\",\"size\":[3,3]}",DomElement* resources=nullptr,const char* tag="navigation-grid") {
        if(!resources)resources=element("resources",root);
        auto* asset=element(tag,resources);attr(asset,"id",id);attr(asset,"data",data);return asset;
    }
    bool wait_navigation_bake() {
        uint64_t deadline=time_now_us()+5000000;
        do {
            if(item_is_error(run_js("jobRequest('poll',jobToken)")))return false;pump_js();
            if(ItemReader(run_js("jobState.finished===true").to_const()).asBool())return true;
        }while(time_now_us()<deadline);
        return false;
    }
    bool start_navigation_bake(bool publish=true) {
        if(item_is_error(run_js("jobRequest('start')")))return false;pump_js();
        if(item_is_error(run_js("globalThis.jobToken=jobState.token"))||!wait_navigation_bake())return false;
        if(publish){if(item_is_error(run_js("jobRequest('publish',jobToken)")))return false;pump_js();}return true;
    }
    DomElement* mesh(const char* geometry="plane",const char* color="#ff0000",const char* material="basic",DomElement* parent=nullptr,const char* tag="mesh") {
        DomElement* result=element(tag,parent?parent:root);
        DomElement* g=element("geometry",result);attr(g,"type",geometry);attr(g,"size","2 2 2");
        DomElement* m=element("material",result);attr(m,"type",material);attr(m,"color",color);
        return result;
    }
    DomElement* runtime_stage(bool playing,const char* substeps="8",bool animate=true,DomElement** configuration=nullptr) {
        auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
        attr(config,"state",playing?"playing":"paused");attr(config,"max-substeps",substeps);
        if(configuration) *configuration=config;
        auto* body=element("rigid-body",root);attr(body,"id","moving");attr(body,"type","dynamic");attr(body,"linear-velocity","1 0 0");
        auto* collider=element("collider",body);attr(collider,"id","shape");attr(collider,"shape","box");mesh("box","#ff0000","basic",body);
        if(animate) {
            auto* animated=mesh("plane","#00ff00");attr(animated,"id","animated");
            auto* clip=element("animation-clip",root);attr(clip,"id","move");attr(clip,"autoplay","true");
            auto* track=element("keyframe-track",clip);attr(track,"path","animated.position");attr(track,"type","vector");
            attr(track,"times","0 1");attr(track,"values","0 0 0 1 0 0");
        }
        return body;
    }
    struct RagdollStage {DomElement *control,*base,*tip,*config,*clip,*track;};
    RagdollStage ragdoll_stage(DomElement* parent=nullptr) {
        auto* config=element("physics",root);attr(config,"gravity","0 -10 0");attr(config,"timestep",".1");attr(config,"state","paused");
        auto* skeleton=element("skeleton",parent?parent:root);attr(skeleton,"id","rig");
        auto* base=element("bone",skeleton);attr(base,"id","base");attr(base,"position","0 2 0");attr(base,"physics-body","base-body");attr(base,"physics-offset","0 .25 0");
        auto* tip=element("bone",base);attr(tip,"id","tip");attr(tip,"position","0 1 0");attr(tip,"physics-body","tip-body");attr(tip,"physics-offset","0 .25 0");
        for(unsigned i=0;i<2;i++) {
            auto* body=element("rigid-body",root);attr(body,"id",i?"tip-body":"base-body");attr(body,"type","dynamic");attr(body,"position",i?"0 9 0":"0 8 0");
            auto* shape=element("collider",body);attr(shape,"id",i?"tip-shape":"base-shape");attr(shape,"shape","sphere");attr(shape,"radius",".1");
        }
        auto* joint=element("joint",root);attr(joint,"id","link");attr(joint,"type","distance");attr(joint,"body-a","base-body");attr(joint,"body-b","tip-body");attr(joint,"length","1");
        auto* clip=element("animation-clip",root);attr(clip,"id","walk");attr(clip,"autoplay","true");attr(clip,"duration","10");
        auto* track=element("keyframe-track",clip);attr(track,"path","base.position");attr(track,"type","vector");attr(track,"times","0 10");attr(track,"values","0 2 0 10 2 0");
        auto* control=element("ragdoll",root);attr(control,"id","person");attr(control,"bones","base tip");attr(control,"mode","animated");attr(control,"recovery-duration",".2");
        return {control,base,tip,config,clip,track};
    }
    DomElement* buffer_quad(const char* material="basic") {
        DomElement* result=mesh("buffer","#ffffff",material),*geometry=result->first_child->as_element();
        attr(geometry,"positions","-1 -1 0 1 -1 0 1 1 0 -1 1 0");attr(geometry,"indices","0 1 2 0 2 3");
        attr(geometry,"normals","0 0 1 0 0 1 0 0 1 0 0 1");return result;
    }
    DomElement* drawable(const char* tag,const char* positions,const char* color="#ff0000") {
        DomElement* result=mesh("buffer",color,"basic",nullptr,tag);attr(result->first_child->as_element(),"positions",positions);return result;
    }
    unsigned color_span(ImageSurface* image,unsigned channel,unsigned scan,bool vertical=false) {
        unsigned count=vertical?(unsigned)image->height:(unsigned)image->width,lo=count,hi=0;
        for(unsigned i=0;i<count;i++) {
            const uint8_t* pixel=(const uint8_t*)image->pixels+(vertical?i:scan)*image->pitch+(vertical?scan:i)*4;
            if(pixel[channel]) {lo=lo<i?lo:i;hi=i;}
        }
        return lo<count?hi-lo+1:0;
    }
    DomElement* map_texture(const char* id,const char* file) {
        DomElement* texture=element("texture",root);attr(texture,"id",id);attr(texture,"src",file);
        attr(texture,"min-filter","9728");attr(texture,"mag-filter","9728");return texture;
    }
    DomElement* glass_stage(DomElement** backdrop=nullptr) {
        attr(camera,"type","orthographic");attr(camera,"view_height","3");attr(root,"background","#000000");
        DomElement* background=buffer_quad(),*g=background->first_child->as_element(),*m=background->last_child->as_element();
        attr(background,"position","0 0 -1");attr(background,"scale","3 3 1");attr(g,"uvs","0 0 1 0 1 1 0 1");
        map_texture("checker","test/scene3d/assets/fidelity/colors.png");attr(m,"texture","checker");attr(m,"alpha-mode","opaque");
        DomElement* glass=buffer_quad("physical");g=glass->first_child->as_element();m=glass->last_child->as_element();
        attr(g,"uvs","0 0 1 0 1 1 0 1");attr(g,"uvs1","0 1 1 1 1 0 0 0");
        attr(glass,"position","0 0 .5");attr(glass,"rotation","0 .4 0");attr(m,"roughness",".05");attr(m,"thickness","1");
        if(backdrop) *backdrop=background;return m;
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
        RootFrame roots(1);Rooted<Item> code(roots,make_string_item(source));return js_builtin_eval(code.get());
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


TEST_F(Scene3dTest, OrthographicSizeIsDistanceIndependentAndDepthClipped) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* plane=mesh();
    pixel(snapshot(),40,64,255,0,0);
    attr(plane,"position","0 0 -10");
    pixel(snapshot(),40,64,255,0,0);
    attr(camera,"far","8");
    pixel(snapshot(),40,64,0,0,0,0);
    attr(camera,"view_height","0");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
}

TEST_F(Scene3dTest, CameraOnlyEditsRetainDrawsButMixedAndStructuralEditsReproject) {
    DomElement* plane=mesh();pixel(snapshot(),64,64,255,0,0);
    Scene3dStats before={},after={};ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"position","0 0 3");attr(camera,"fov","60");
    pixel(snapshot(),30,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.projection_generation,before.projection_generation);
    EXPECT_GT(after.snapshot_generation,before.snapshot_generation);
    EXPECT_EQ(after.graphics.resources,before.graphics.resources);
    EXPECT_EQ(after.graphics.allocated_bytes,before.graphics.allocated_bytes);
    // a camera error clears the old image, and repair keeps the valid retained geometry.
    attr(camera,"near","-1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"camera"),nullptr);
    attr(camera,"near","0.1");pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.projection_generation,before.projection_generation);
    attr(camera,"position","0 0 4");attr(plane->first_child->next_sibling->as_element(),"color","#00ff00");
    pixel(snapshot(),64,64,0,255,0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.projection_generation,before.projection_generation+1);
    // camera IDs and children are structural: they must still pass the full validator.
    attr(camera,"id","missing");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    attr(camera,"id","main");pixel(snapshot(),64,64,0,255,0);
    DomElement* child=element("group",camera);doc.mutation_epoch++;
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    camera->remove_child(child);doc.mutation_epoch++;pixel(snapshot(),64,64,0,255,0);
    ASSERT_TRUE(scene3d_stats(root,&before));
    // parent transforms move scene objects too, even when a camera changes in the same turn.
    attr(root,"position","0.5 0 0");attr(camera,"fov","75");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.projection_generation,before.projection_generation+1);
}

TEST_F(Scene3dTest, TypedScalarEditsInvalidateWithoutReplacingTheBackingElement) {
    ASSERT_NE(load_page("test/scene3d/mixed.html"),nullptr);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"viewport");ASSERT_NE(scene,nullptr);
    ASSERT_TRUE(scene->set_attribute("tone-mapping","neutral"));
    MarkEditor editor(page,EDIT_MODE_INLINE);Item backing={.element=dom_element_backing(scene)};
    ASSERT_EQ(editor.elmt_update_attr(backing,"exposure",editor.builder()->createFloat(.25)).element,backing.element);
    page->mutation_epoch++;
    pixel(scene3d_snapshot(scene,&ui,128,128,1),64,64,137,0,0);
    Scene3dStats before={},after={};ASSERT_TRUE(scene3d_stats(scene,&before));
    ASSERT_EQ(editor.elmt_update_attr(backing,"exposure",editor.builder()->createFloat(.5)).element,backing.element);
    page->mutation_epoch++;
    pixel(scene3d_snapshot(scene,&ui,128,128,1),64,64,188,0,0);
    ASSERT_TRUE(scene3d_stats(scene,&after));EXPECT_EQ(after.projection_generation,before.projection_generation+1);
}

TEST_F(Scene3dTest, EnvironmentAndHemisphereLightAndCaptureOnlyWhenTheEnvironmentChanges) {
    // a hemisphere light seen side-on lights a Lambert plane halfway between ground and sky
    DomElement* plane=mesh("plane","#ff0000","lambert");
    DomElement* hemisphere=element("light",root);attr(hemisphere,"type","hemisphere");attr(hemisphere,"ground-color","#000000");
    pixel(snapshot(),64,64,188,0,0);
    DomElement* second=element("light",root);attr(second,"type","hemisphere");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"hemisphere light quota"),nullptr);
    root->remove_child(second);root->remove_child(hemisphere);doc.mutation_epoch++;
    pixel(snapshot(),64,64,0,0,0);
    // a uniformly glowing box around the capture point: every prefiltered level holds its radiance,
    // which lights the Lambert plane as an ambient light of that color would; the box itself is never drawn
    DomElement* environment=element("environment",root);attr(environment,"id","glow");
    DomElement* walls=mesh("box","#000000","basic",environment);DomElement* glow=walls->first_child->next_sibling->as_element();
    attr(glow,"side","back");attr(glow,"emissive","#ffffff");attr(glow,"emissive-intensity","0.5");
    pixel(snapshot(),64,64,0,0,0);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,0u);
    attr(root,"environment","glow");
    ImageSurface* image=snapshot();pixel(image,64,64,188,0,0);pixel(image,2,2,0,0,0,0);
    ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,1u);
    attr(root,"environment-intensity","0.25");pixel(snapshot(),64,64,99,0,0);
    // camera and view changes re-project without capturing again; the environment's own content does capture
    attr(camera,"fov","60");attr(plane,"position","0 0 -0.5");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,1u);
    attr(glow,"emissive-intensity","1");attr(root,"environment-intensity","1");pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,2u);
    scene3d_context_lost(root);pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,3u);
    // an environment is a scene of its own: no cameras, no nesting, and the scene must name one that exists
    attr(root,"environment","missing");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"names no environment"),nullptr);
    attr(root,"environment","glow");DomElement* inner=element("camera",environment);attr(inner,"type","perspective");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"cameras"),nullptr);
    environment->remove_child(inner);element("environment",environment);doc.mutation_epoch++;
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"cannot nest"),nullptr);
}

TEST_F(Scene3dTest, PbrTracksChangePixelsAndRestoreAuthoredValuesWithoutReprojection) {
    attr(camera,"type","orthographic");attr(camera,"view_height","3");
    DomElement* plane=mesh("plane","#b97836","standard");attr(plane,"id","surface");
    DomElement* material=plane->last_child->as_element();attr(material,"roughness","0.35");
    DomElement* caster=mesh("box","#ffffff","lambert");attr(caster,"position","0.4 0 1");attr(caster,"scale","0.2 0.3 0.2");
    DomElement* sun=element("light",root);attr(sun,"id","sun");attr(sun,"type","directional");
    attr(sun,"position","2 1 4");attr(sun,"cast-shadow","true");attr(sun,"shadow-map-size","512");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","finish");
    DomElement* track=element("keyframe-track",clip);attr(track,"times","0 1");
    struct Case { const char* path;const char* type;const char* values;const char* property;double sample; };
    const Case cases[]={
        {"surface.material.metalness","number","0 1","metalness",.75},
        {"surface.material.roughness","number","0.1 0.9","roughness",.7},
        {"surface.material.emissive","color","0 0 0 0 1 0","emissive",.75},
        {"sun.shadow-softness","number","0 8","shadow-softness",6}
    };
    for(const auto& item:cases) {
        SCOPED_TRACE(item.path);attr(track,"path",item.path);attr(track,"type",item.type);attr(track,"values",item.values);
        ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
        uint64_t authored=hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height);
        Scene3dAnimationState* state=scene3d_animations(root);ASSERT_NE(state,nullptr);
        AnimationActionState* action=scene3d_animation_action(state,"finish");ASSERT_NE(action,nullptr);
        ASSERT_TRUE(animation_action_play(action));ASSERT_TRUE(scene3d_animation_seek(state,.75));
        bool softness=!strcmp(item.property,"shadow-softness");
        AnimationValue value={};ASSERT_TRUE(scene3d_animation_value(state,softness?sun:material,item.property,&value));
        EXPECT_NEAR(value.numbers[!strcmp(item.type,"color")?1:0],item.sample,1e-6);
        Scene3dStats before={};ASSERT_TRUE(scene3d_stats(root,&before));
        image=snapshot();ASSERT_NE(image,nullptr);
        EXPECT_NE(hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height),authored);
        Scene3dStats after={};ASSERT_TRUE(scene3d_stats(root,&after));
        EXPECT_EQ(after.projection_generation,before.projection_generation);
        EXPECT_EQ(after.environment_builds,before.environment_builds);
        EXPECT_EQ(after.graphics.gpu_bytes,before.graphics.gpu_bytes);
        // softness changes receiving PCF, so the cached caster depth stays valid.
        if(softness) EXPECT_EQ(after.shadow_passes,before.shadow_passes);
        ASSERT_TRUE(animation_action_stop(action));ASSERT_TRUE(scene3d_animation_update(state,0));
        image=snapshot();ASSERT_NE(image,nullptr);
        EXPECT_EQ(hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height),authored);
    }
}

TEST_F(Scene3dTest, PbrTrackRangesRejectInvalidSamplesAndRecoverAfterSeek) {
    DomElement* object=mesh("plane","#ffffff","standard");attr(object,"id","surface");
    DomElement* sun=element("light",root);attr(sun,"id","sun");attr(sun,"type","directional");attr(sun,"position","0 0 4");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","finish");
    DomElement* track=element("keyframe-track",clip);attr(track,"type","number");attr(track,"times","0 1");
    const char* paths[]={"surface.material.metalness","surface.material.roughness","sun.shadow-softness"};
    for(const char* path:paths) {
        SCOPED_TRACE(path);attr(track,"path",path);attr(track,"values",!strcmp(path,"sun.shadow-softness")?"0 32":"0 2");
        ASSERT_NE(snapshot(),nullptr);Scene3dAnimationState* state=scene3d_animations(root);ASSERT_NE(state,nullptr);
        ASSERT_TRUE(animation_action_play(scene3d_animation_action(state,"finish")));ASSERT_TRUE(scene3d_animation_seek(state,.75));
        EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
        EXPECT_NE(strstr(scene3d_diagnostic(root),"invalid"),nullptr);
        ASSERT_TRUE(scene3d_animation_seek(state,0));ASSERT_NE(snapshot(),nullptr);
    }
    attr(object->last_child->as_element(),"type","lambert");attr(track,"path",paths[0]);
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"binding"),nullptr);
}

TEST_F(Scene3dTest, ShadowMapsDarkenReceiversAndRenderOnlyWhenTheirCastersOrFrameChange) {
    // a light at 45 degrees; a small box on its axis shadows the middle of a Lambert plane facing the camera
    DomElement* plane=mesh("plane","#ff0000","lambert");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","3 0 3");attr(sun,"cast-shadow","true");
    DomElement* caster=mesh("box","#00ff00","lambert");attr(caster->first_child->as_element(),"size","0.3 0.3 0.3");attr(caster,"position","0.75 0 0.75");
    ImageSurface* image=snapshot();
    // in shadow nothing lights the plane; beside it the light falls at 45 degrees (cos 45 = 0.707 linear)
    pixel(image,64,64,0,0,0);pixel(image,52,64,218,0,0);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.shadow_passes,1u);
    // camera moves never redraw the map; moving a caster does
    attr(camera,"fov","80");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.shadow_passes,1u);
    attr(caster,"position","0.75 0.4 0.75");pixel(snapshot(),64,64,218,0,0);
    ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.shadow_passes,2u);
    // a mesh that opts out casts no shadow; a receiver that opts out is lit
    attr(caster,"position","0.75 0 0.75");attr(caster,"cast-shadow","false");pixel(snapshot(),64,64,218,0,0);
    attr(caster,"cast-shadow","true");attr(plane,"receive-shadow","false");pixel(snapshot(),64,64,218,0,0);
    attr(plane,"receive-shadow","true");attr(sun,"shadow-intensity","0.5");pixel(snapshot(),64,64,160,0,0);
    // ambient and hemisphere lights cast nothing; at most four lights cast
    DomElement* ambient=element("light",root);attr(ambient,"type","ambient");attr(ambient,"cast-shadow","true");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"directional, point or spot"),nullptr);
    root->remove_child(ambient);doc.mutation_epoch++;
    for (unsigned i=0;i<4;i++) {
        DomElement* extra=element("light",root);attr(extra,"type","directional");attr(extra,"position","0 1 1");attr(extra,"cast-shadow","true");
    }
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"shadow light quota"),nullptr);
}

TEST_F(Scene3dTest, PointLightCubeShadowsDarkenReceiversInEveryDirection) {
    // a point light between the camera and a Lambert plane; a small box on the line to the plane's centre shadows it
    DomElement* plane=mesh("plane","#ff0000","lambert");(void)plane;
    DomElement* lamp=element("light",root);attr(lamp,"type","point");attr(lamp,"position","1 0 2");attr(lamp,"cast-shadow","true");
    DomElement* caster=mesh("box","#00ff00","lambert");attr(caster->first_child->as_element(),"size","0.2 0.2 0.2");attr(caster,"position","0.5 0 1");
    ImageSurface* image=snapshot();
    // beside the shadow the lamp lights the plane by inverse-square falloff (0.107 linear at x = -0.75)
    pixel(image,64,64,0,0,0);pixel(image,52,64,92,0,0);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.shadow_passes,1u);
    attr(camera,"fov","80");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.shadow_passes,1u);
    // the cube covers every direction: the same lamp behind the plane's centre, shadowing it from the other side
    attr(lamp,"position","-1 0 2");attr(caster,"position","-0.5 0 1");image=snapshot();
    pixel(image,64,64,0,0,0);pixel(image,76,64,92,0,0);
    // a directional and a point light share the scene's shadow slots
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","0 0 3");attr(sun,"intensity","0");attr(sun,"cast-shadow","true");
    pixel(snapshot(),64,64,0,0,0);
    attr(lamp,"shadow-map-size","2048");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"at most 1024"),nullptr);
}

TEST(Scene3dHdr, RadianceImagesDecodeFlatAndRunLengthScanlinesAsThreeJs) {
    HdrImage image;const char* error=nullptr;
    // two flat texels: RGBE (128, 64, 32, 129) is mantissa / 255 * 2^1; exponent 0 is black
    const char flat_header[]="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    uint8_t flat[sizeof(flat_header)-1+8];memcpy(flat,flat_header,sizeof(flat_header)-1);
    const uint8_t texels[]={128,64,32,129, 200,200,200,0};memcpy(flat+sizeof(flat_header)-1,texels,8);
    ASSERT_TRUE(hdr_read(flat,sizeof(flat),4096,&image,&error))<<error;
    EXPECT_EQ(image.width,2u);EXPECT_EQ(image.height,1u);
    EXPECT_FLOAT_EQ(image.pixels[0],256.f/255);EXPECT_FLOAT_EQ(image.pixels[1],128.f/255);EXPECT_FLOAT_EQ(image.pixels[2],64.f/255);
    EXPECT_EQ(image.pixels[3],0.f);hdr_image_release(&image);
    // an 8-texel scanline, each channel one run or literals; rows stay top to bottom
    const char rle_header[]="#?RGBE\n\n-Y 2 +X 8\n";
    const uint8_t rows[]={2,2,0,8, 136,10, 136,20, 4,1,2,3,4,132,5, 136,128,
                          2,2,0,8, 136,0, 136,0, 136,0, 136,0};
    uint8_t rle[sizeof(rle_header)-1+sizeof(rows)];memcpy(rle,rle_header,sizeof(rle_header)-1);memcpy(rle+sizeof(rle_header)-1,rows,sizeof(rows));
    ASSERT_TRUE(hdr_read(rle,sizeof(rle),4096,&image,&error))<<error;
    EXPECT_FLOAT_EQ(image.pixels[0],10.f/255);EXPECT_FLOAT_EQ(image.pixels[1],20.f/255);EXPECT_FLOAT_EQ(image.pixels[2],1.f/255);
    EXPECT_FLOAT_EQ(image.pixels[7*3+2],5.f/255);EXPECT_EQ(image.pixels[8*3],0.f);hdr_image_release(&image);
    // what the decoder refuses, and why
    // each sample counts its own bytes: texels may hold zeros
#define HDR_SAMPLE(text,reason) {text,sizeof(text)-1,reason}
    struct { const char* bytes; size_t length; const char* reason; } bad[]={
        HDR_SAMPLE("P6\n","not a Radiance"),HDR_SAMPLE("#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 1 +X 1\n\0\0\0\0","pixel format"),
        HDR_SAMPLE("#?RADIANCE\n\n+Y 1 +X 1\n\0\0\0\0","orientation"),HDR_SAMPLE("#?RADIANCE\n\n-Y 2 +X 1\n\1\1\1\1","truncated"),
        HDR_SAMPLE("#?RADIANCE\n\n-Y 1 +X 5000\n","dimensions"),HDR_SAMPLE("#?RADIANCE\n","header")};
#undef HDR_SAMPLE
    for (auto& b:bad) {
        EXPECT_FALSE(hdr_read((const uint8_t*)b.bytes,b.length,4096,&image,&error))<<b.reason;
        EXPECT_NE(strstr(error,b.reason),nullptr)<<error;EXPECT_EQ(image.pixels,nullptr);
    }
}

TEST_F(Scene3dTest, ImageEnvironmentsLoadOnceAndRejectContentOrMissingFiles) {
    DomElement* plane=mesh("plane","#ffffff","lambert");(void)plane;
    DomElement* studio=element("environment",root);attr(studio,"id","studio");attr(studio,"src","test/scene3d/assets/hdr/studio.hdr");
    attr(root,"environment","studio");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    // the studio lights the white plane from every side
    const uint8_t* p=(const uint8_t*)image->pixels+64*image->pitch+64*4;EXPECT_GT(p[0],40);EXPECT_EQ(p[3],255);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,1u);
    attr(camera,"fov","70");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.environment_builds,1u);
    DomElement* extra=mesh("box","#ffffff","basic",studio);
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"holds no content"),nullptr);
    studio->remove_child(extra);attr(studio,"src","test/scene3d/assets/hdr/missing.hdr");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"cannot be read"),nullptr);
    attr(studio,"src","test/scene3d/assets/pbr/checker.png");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"not a Radiance"),nullptr);
}

TEST_F(Scene3dTest, PhysicsPosesSurviveCameraResizeAndGraphicsRecovery) {
    DomElement* config=element("physics",root);attr(config,"gravity","0 -1 0");attr(config,"state","paused");
    DomElement* body=element("rigid-body",root);attr(body,"id","falling");attr(body,"type","dynamic");attr(body,"position","0 1 0");
    DomElement* collider=element("collider",body);attr(collider,"id","falling-shape");attr(collider,"shape","box");attr(collider,"size","1 1 1");
    mesh("box","#ff0000","basic",body);ASSERT_NE(snapshot(),nullptr);
    Scene3dPhysics* physics=scene3d_physics(root);ASSERT_NE(physics,nullptr);PhysicsState result={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,120,&result,&error));ASSERT_EQ(result.body_count,1u);EXPECT_EQ(result.tick,120u);
    EXPECT_NEAR(result.bodies[0].pose.position.y,1.0-121.0/240.0,1e-10);
    Scene3dStats before={};ASSERT_TRUE(scene3d_stats(root,&before));ASSERT_NE(snapshot(192,160),nullptr);
    Scene3dStats after={};ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);
    attr(camera,"fov","70");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_EQ(result.tick,120u);
}

TEST_F(Scene3dTest, CapsuleDescriptorEditsRetainNativeMotionAndRuntime) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    auto* body=element("rigid-body",root);attr(body,"id","capsule-body");attr(body,"type","dynamic");attr(body,"linear-velocity","1 0 0");
    auto* collider=element("collider",body);attr(collider,"id","capsule-shape");attr(collider,"shape","capsule");
    attr(collider,"radius",".5");attr(collider,"height","2");mesh("box","#ff0000","basic",body);ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState result={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&result,&error));ASSERT_EQ(result.body_count,1u);double mass=result.bodies[0].mass;
    EXPECT_NEAR(result.bodies[0].pose.position.x,.1,1e-12);
    attr(collider,"height","4");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_EQ(result.tick,10u);EXPECT_GT(result.bodies[0].mass,mass);
    EXPECT_NEAR(result.bodies[0].pose.position.x,.1,1e-12);EXPECT_DOUBLE_EQ(result.bodies[0].velocity.x,1);
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(physics,body,&pose));EXPECT_NEAR(pose.v[12],.1,1e-6);
}

TEST_F(Scene3dTest, ConvexEditsOwnGeometryRetainSleepingStateAndRejectInvalidClouds) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    auto* body=element("rigid-body",root);attr(body,"id","convex-body");attr(body,"type","dynamic");
    auto* collider=element("collider",body);attr(collider,"id","convex-shape");attr(collider,"shape","convex");
    const char* cube="-.5 -.5 -.5 .5 -.5 -.5 -.5 .5 -.5 .5 .5 -.5 -.5 -.5 .5 .5 -.5 .5 -.5 .5 .5 .5 .5 .5";
    attr(collider,"vertices",cube);mesh("box","#ff0000","basic",body);ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState result={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,100,&result,&error));ASSERT_TRUE(result.bodies[0].sleeping);
    for(unsigned i=0;i<3;i++) {
        attr(camera,"fov",i%2?"60":"70");ASSERT_NE(snapshot(),nullptr);
        ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_TRUE(result.bodies[0].sleeping);EXPECT_EQ(result.tick,100u);
    }
    attr(collider,"vertices","0 0 0 1 0 0 0 1 0 1 1 0");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_EQ(scene3d_physics(root),physics);ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));
    EXPECT_EQ(result.tick,100u);EXPECT_NEAR(result.bodies[0].mass,1,1e-12);
    attr(collider,"vertices","-1 -1 -1 1 -1 -1 -1 1 -1 1 1 -1 -1 -1 1 1 -1 1 -1 1 1 1 1 1");
    attr(body,"linear-velocity","1 0 0");ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&result,&error));EXPECT_EQ(result.tick,110u);
    EXPECT_NEAR(result.bodies[0].mass,8,1e-12);EXPECT_NEAR(result.bodies[0].pose.position.x,.1,1e-12);
}

TEST_F(Scene3dTest, PhysicsRejectsUnimplementedCcdAndNonRigidBodyTransforms) {
    element("physics",root);DomElement* body=element("rigid-body",root);attr(body,"id","body");attr(body,"type","dynamic");
    DomElement* collider=element("collider",body);attr(collider,"id","shape");attr(collider,"shape","box");mesh("box","#ff0000","basic",body);
    attr(body,"ccd","true");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"CCD"),nullptr);
    attr(body,"ccd","false");attr(body,"scale","2 1 1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"rigid"),nullptr);
    attr(body,"scale","1 1 1");ASSERT_NE(snapshot(),nullptr);
    DomElement* track=element("keyframe-track",root);attr(track,"path","body.position");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"tick sampling"),nullptr);
}

TEST_F(Scene3dTest, RuntimeCoordinatesAnimationAndPhysicsOncePerHostSample) {
    auto* body=runtime_stage(true);ASSERT_NE(snapshot(),nullptr);
    auto* runtime=scene3d_runtime(root);ASSERT_NE(runtime,nullptr);
    auto* scheduler=doc.state->animation_scheduler;ASSERT_NE(scheduler,nullptr);EXPECT_EQ(scheduler->count,1);
    animation_scheduler_tick(scheduler,.025,nullptr);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,2u);EXPECT_NEAR(stats.debt,.005,1e-12);
    auto* action=scene3d_animation_action(scene3d_animations(root),"move");ASSERT_NE(action,nullptr);EXPECT_NEAR(action->time,.02,1e-12);
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_NEAR(pose.v[12],.02,1e-6);
    animation_scheduler_tick(scheduler,.025,nullptr);ASSERT_NE(snapshot(),nullptr);ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,2u);
    ASSERT_TRUE(scene3d_runtime_pause(runtime,true));EXPECT_EQ(scheduler->count,0);
    ASSERT_TRUE(scene3d_runtime_step(runtime,3));EXPECT_NEAR(action->time,.05,1e-12);
    ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_NEAR(pose.v[12],.05,1e-6);
    ASSERT_TRUE(scene3d_runtime_time_scale(runtime,.5));ASSERT_TRUE(scene3d_runtime_pause(runtime,false));EXPECT_EQ(scheduler->count,1);
    animation_scheduler_tick(scheduler,.065,nullptr);ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,7u);
    EXPECT_NEAR(action->time,.07,1e-12);ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_NEAR(pose.v[12],.07,1e-6);
    attr(camera,"fov","80");ASSERT_NE(snapshot(160,144),nullptr);scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(scene3d_runtime(root),runtime);ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,7u);
    scene3d_release_subtree(root);EXPECT_EQ(scene3d_runtime(root),nullptr);EXPECT_EQ(scheduler->count,0);
}

TEST_F(Scene3dTest, NativeCharacterAdapterCopiesRequestsAndCommitsOnlyAnUpcomingTarget) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    auto* body=element("rigid-body",root);attr(body,"id","walker");attr(body,"type","kinematic-position");attr(body,"position","0 2 0");
    auto* collider=element("collider",body);attr(collider,"id","capsule");attr(collider,"shape","capsule");attr(collider,"radius",".25");attr(collider,"height","1");mesh("box","#ff0000","basic",body);
    ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);PhysicsCharacterMove movement={};PhysicsCharacterProfile profile={};DomElement* owner=nullptr;PhysicsError error={};
    {
        struct Source {Pool* pool=pool_create();~Source(){pool_destroy(pool);}} source;ASSERT_NE(source.pool,nullptr);
        Input* input=Input::create(source.pool);ASSERT_NE(input,nullptr);MarkBuilder builder(input);
        Item request=builder.map().put("body","walker").put("skin",.01).put("max_slides",2).final();
        ASSERT_TRUE(scene3d_physics_character_configure(physics,request,&movement,&profile,&owner,&error))<<(error.message?error.message:"");
    }
    EXPECT_EQ(owner,body);EXPECT_EQ(movement.max_slides,2u);EXPECT_DOUBLE_EQ(movement.skin,.01);EXPECT_DOUBLE_EQ(profile.radius,.25);EXPECT_DOUBLE_EQ(profile.height,1);
    movement.displacement={1,0,0};PhysicsCharacterState moved={};ASSERT_TRUE(scene3d_physics_character_move(physics,&movement,&moved,&error));EXPECT_NEAR(moved.pose.position.x,1,1e-12);
    ASSERT_TRUE(scene3d_physics_character_profile(physics,movement.body,&profile,&error));EXPECT_DOUBLE_EQ(profile.pose.position.x,0);
    ASSERT_TRUE(scene3d_runtime_step(scene3d_runtime(root),1));ASSERT_TRUE(scene3d_physics_character_profile(physics,movement.body,&profile,&error));EXPECT_NEAR(profile.pose.position.x,1,1e-12);
}

TEST_F(Scene3dTest, PhysicsWorldIdentitySurvivesTicksAndCameraEditsButInvalidatesDiscontinuousChanges) {
    auto* body=runtime_stage(false);ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);
    uint64_t initial=scene3d_physics_world_identity(physics);ASSERT_GT(initial,0u);ASSERT_TRUE(scene3d_runtime_step(runtime,2));EXPECT_EQ(scene3d_physics_world_identity(physics),initial);
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));ASSERT_EQ(state.body_count,1u);
    PhysicsCommand command={};command.kind=PHYSICS_VELOCITY;command.body=state.bodies[0].id;command.value={2,0,0};ASSERT_TRUE(scene3d_physics_apply(physics,&command,1,&error));EXPECT_EQ(scene3d_physics_world_identity(physics),initial);
    command={};command.kind=PHYSICS_TELEPORT;command.body=state.bodies[0].id;command.pose=state.bodies[0].pose;command.pose.position.x=3;ASSERT_TRUE(scene3d_physics_apply(physics,&command,1,&error));
    uint64_t teleported=scene3d_physics_world_identity(physics);EXPECT_GT(teleported,initial);ASSERT_TRUE(scene3d_seek(root,0));EXPECT_GT(scene3d_physics_world_identity(physics),teleported);
    uint64_t reset=scene3d_physics_world_identity(physics);attr(body,"linear-damping",".2");ASSERT_TRUE(scene3d_runtime_step(runtime,1));uint64_t edited=scene3d_physics_world_identity(physics);EXPECT_GT(edited,reset);
    attr(camera,"fov","80");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics_world_identity(physics),edited);
    auto* collider=body->first_child->as_element();attr(collider,"size","-1 1 1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics_world_identity(physics),edited);
    attr(collider,"size","1 1 1");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics_world_identity(physics),edited);
    scene3d_release_subtree(root);ASSERT_NE(snapshot(),nullptr);EXPECT_GT(scene3d_physics_world_identity(scene3d_physics(root)),edited);
}
TEST_F(Scene3dTest, RagdollReplayPublishesANewPhysicsWorldIdentityWithTheAdoptedState) {
    ragdoll_stage();ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);uint64_t first=scene3d_physics_world_identity(physics);
    ASSERT_TRUE(scene3d_runtime_step(scene3d_runtime(root),1));EXPECT_EQ(scene3d_physics_world_identity(physics),first);
    ASSERT_TRUE(scene3d_seek(root,4));uint64_t replayed=scene3d_physics_world_identity(physics);EXPECT_GT(replayed,first);
    ASSERT_TRUE(scene3d_seek(root,4));EXPECT_GT(scene3d_physics_world_identity(physics),replayed);
}

TEST_F(Scene3dTest, NavigationStageReadsThePreviousWorldBeforeAnimationAndPhysics) {
    auto* body=runtime_stage(true);ASSERT_NE(snapshot(),nullptr);auto* runtime=scene3d_runtime(root);
    auto* action=scene3d_animation_action(scene3d_animations(root),"move");ASSERT_NE(action,nullptr);
    struct Probe {Scene3dPhysics* physics;DomElement* body;AnimationActionState* action;unsigned count;double positions[4],times[4];bool valid;};
    Probe probe={scene3d_physics(root),body,action,0,{},{},true};
    ASSERT_TRUE(scene3d_runtime_bind(runtime,SCENE3D_TICK_NAVIGATION,&probe,[](void* owner,double delta){
        auto& p=*(Probe*)owner;Scene3dMatrix pose;
        if(p.count>=4||fabs(delta-.01)>1e-12||!scene3d_physics_pose(p.physics,p.body,&pose)){p.valid=false;return SCENE3D_TICK_FAILED;}
        p.positions[p.count]=pose.v[12];p.times[p.count++]=p.action->time;return SCENE3D_TICK_ACTIVE;
    }));
    ASSERT_TRUE(scene3d_runtime_enable(runtime,SCENE3D_TICK_NAVIGATION,&probe,true));ASSERT_TRUE(scene3d_runtime_step(runtime,3));
    EXPECT_TRUE(probe.valid);ASSERT_EQ(probe.count,3u);
    for(unsigned i=0;i<3;i++){EXPECT_NEAR(probe.positions[i],i*.01,1e-6);EXPECT_NEAR(probe.times[i],i*.01,1e-12);}
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(probe.physics,body,&pose));EXPECT_NEAR(pose.v[12],.03,1e-6);EXPECT_NEAR(action->time,.03,1e-12);
    scene3d_runtime_unbind(runtime,SCENE3D_TICK_NAVIGATION,&probe);
}
TEST_F(Scene3dTest, ActiveNavigationUsesFixedTicksWithoutAnEnabledPhysicsStage) {
    runtime_stage(false);ASSERT_NE(snapshot(),nullptr);auto* runtime=scene3d_runtime(root);
    struct Probe {unsigned count;double elapsed;};Probe probe={};
    ASSERT_TRUE(scene3d_runtime_bind(runtime,SCENE3D_TICK_NAVIGATION,&probe,[](void* owner,double delta){
        auto& p=*(Probe*)owner;p.count++;p.elapsed+=delta;return SCENE3D_TICK_ACTIVE;
    }));
    ASSERT_TRUE(scene3d_runtime_enable(runtime,SCENE3D_TICK_NAVIGATION,&probe,true));ASSERT_TRUE(scene3d_runtime_advance(runtime,.025));
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(probe.count,2u);EXPECT_NEAR(probe.elapsed,.02,1e-12);EXPECT_EQ(stats.ticks,2u);EXPECT_NEAR(stats.debt,.005,1e-12);
    auto* action=scene3d_animation_action(scene3d_animations(root),"move");ASSERT_NE(action,nullptr);EXPECT_NEAR(action->time,.02,1e-12);
    scene3d_runtime_unbind(runtime,SCENE3D_TICK_NAVIGATION,&probe);
}

TEST_F(Scene3dTest, FixedTickAdmitsAuthoredPhysicsEditsBeforeAnyServiceSamples) {
    auto* body=runtime_stage(false);ASSERT_NE(snapshot(),nullptr);auto* runtime=scene3d_runtime(root);
    attr(body,"linear-velocity","2 0 0");ASSERT_TRUE(scene3d_runtime_step(runtime,1));
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_NEAR(pose.v[12],.02,1e-6);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,1u);
}
TEST_F(Scene3dTest, FailedTickAdmissionKeepsClockAndWorldAvailableForRepair) {
    auto* body=runtime_stage(false);ASSERT_NE(snapshot(),nullptr);auto* runtime=scene3d_runtime(root);auto* physics=scene3d_physics(root);
    auto* collider=body->first_child->as_element();attr(collider,"size","-1 1 1");EXPECT_FALSE(scene3d_runtime_step(runtime,1));
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(physics,body,&pose));EXPECT_FLOAT_EQ(pose.v[12],0);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,0u);EXPECT_FALSE(stats.faulted);
    attr(collider,"size","1 1 1");ASSERT_TRUE(scene3d_runtime_step(runtime,1));EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_pose(physics,body,&pose));EXPECT_NEAR(pose.v[12],.01,1e-6);ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,1u);
}

TEST_F(Scene3dTest, CoordinatedPhysicsBatchDefersOwnerReplacementToMainAdmission) {
    ragdoll_stage();ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);
    auto* extra=element("rigid-body",root);attr(extra,"id","added-body");attr(extra,"type","fixed");
    PhysicsState state={};PhysicsError error={};EXPECT_FALSE(scene3d_physics_advance(physics,1,&state,&error));
    EXPECT_NE(strstr(error.message?error.message:"","owned batch"),nullptr);EXPECT_EQ(state.tick,0u);EXPECT_EQ(state.body_count,2u);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_NE(snapshot(),nullptr);physics=scene3d_physics(root);ASSERT_NE(physics,nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_EQ(state.tick,1u);EXPECT_EQ(state.body_count,3u);
}

TEST_F(Scene3dTest, RuntimePreservesDebtBoundsWorkAndRejectsInvalidTime) {
    DomElement* config=nullptr;runtime_stage(true,"4",false,&config);
    ASSERT_NE(snapshot(),nullptr);auto* runtime=scene3d_runtime(root);ASSERT_NE(runtime,nullptr);
    ASSERT_TRUE(scene3d_runtime_advance(runtime,.1));Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));
    EXPECT_EQ(stats.ticks,4u);EXPECT_NEAR(stats.debt,.06,1e-12);
    attr(config,"max-substeps","2");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_runtime_advance(runtime,0));ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,4u);
    ASSERT_TRUE(scene3d_runtime_advance(runtime,.001));ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,6u);EXPECT_NEAR(stats.debt,.041,1e-12);
    EXPECT_FALSE(scene3d_runtime_advance(runtime,NAN));EXPECT_FALSE(scene3d_runtime_advance(runtime,-1));
    EXPECT_FALSE(scene3d_runtime_time_scale(runtime,INFINITY));EXPECT_FALSE(scene3d_runtime_configure(runtime,0,4));
    ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,6u);EXPECT_FALSE(stats.faulted);
    EXPECT_FALSE(scene3d_runtime_advance(runtime,3));ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_TRUE(stats.faulted);EXPECT_EQ(stats.ticks,6u);
    EXPECT_NE(strstr(scene3d_runtime_diagnostic(runtime),"debt"),nullptr);EXPECT_FALSE(scene3d_runtime_step(runtime,1));
}

TEST_F(Scene3dTest, RuntimeJoinsVisualSamplingToPhysicsAtTheNextFixedBoundary) {
    auto* body=runtime_stage(false);ASSERT_NE(snapshot(),nullptr);
    auto* runtime=scene3d_runtime(root);auto* scheduler=doc.state->animation_scheduler;
    auto* action=scene3d_animation_action(scene3d_animations(root),"move");ASSERT_NE(action,nullptr);
    animation_scheduler_tick(scheduler,.005,nullptr);EXPECT_NEAR(action->time,.005,1e-12);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,0u);EXPECT_NEAR(stats.debt,.005,1e-12);
    ASSERT_TRUE(scene3d_physics_play(scene3d_physics(root),true));EXPECT_EQ(scheduler->count,1);
    animation_scheduler_tick(scheduler,.01,nullptr);EXPECT_NEAR(action->time,.01,1e-12);
    Scene3dMatrix pose;ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_NEAR(pose.v[12],.01,1e-6);
    ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,1u);EXPECT_NEAR(stats.debt,0,1e-12);
    ASSERT_TRUE(scene3d_runtime_step(runtime,1));EXPECT_NEAR(action->time,.02,1e-12);
    ASSERT_TRUE(scene3d_physics_seek(scene3d_physics(root),0));ASSERT_TRUE(scene3d_animation_seek(scene3d_animations(root),0));
    ASSERT_TRUE(scene3d_runtime_reset_clock(runtime,0));ASSERT_TRUE(scene3d_physics_pose(scene3d_physics(root),body,&pose));EXPECT_FLOAT_EQ(pose.v[12],0);
    EXPECT_DOUBLE_EQ(action->time,0);ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,0u);EXPECT_TRUE(stats.paused);
}

TEST_F(Scene3dTest, PhysicsBatchFaultPublishesOnlyCompletedTicks) {
    DomElement* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep","0.75");
    DomElement* body=element("rigid-body",root);attr(body,"id","fast");attr(body,"type","dynamic");attr(body,"linear-velocity","1e308 0 0");
    DomElement* collider=element("collider",body);attr(collider,"id","shape");attr(collider,"shape","sphere");
    mesh("box","#ff0000","basic",body);ASSERT_NE(snapshot(),nullptr);
    PhysicsState result={};PhysicsError error={};
    EXPECT_FALSE(scene3d_physics_advance(scene3d_physics(root),3,&result,&error));
    ASSERT_EQ(result.tick,2u);ASSERT_EQ(result.body_count,1u);
    EXPECT_DOUBLE_EQ(result.bodies[0].pose.position.x,1.5e308);
}

TEST_F(Scene3dTest, PhysicsRenderRotationMatchesColliderAxes) {
    DomElement* config=element("physics",root);attr(config,"gravity","0 0 0");
    DomElement* body=element("rigid-body",root);attr(body,"id","rotating");attr(body,"type","dynamic");
    attr(body,"rotation","0 0 -0.12");attr(body,"position","0 1 0");attr(body,"angular-velocity","0 1 0");
    DomElement* collider=element("collider",body);attr(collider,"id","shape");attr(collider,"shape","box");
    mesh("box","#ff0000","basic",body);ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);PhysicsState state={};PhysicsError error={};
    for(unsigned sample=0;sample<2;sample++) {
        ASSERT_TRUE(scene3d_physics_advance(physics,sample?60:0,&state,&error));Scene3dMatrix rendered;
        ASSERT_TRUE(scene3d_physics_pose(physics,body,&rendered));
        // compare rendered axes to the independent vector rotation used by native collision detection.
        for(unsigned axis=0;axis<3;axis++) {
            PhysicsVec point=pbase(axis),expected=padd(state.bodies[0].pose.position,prot(state.bodies[0].pose.rotation,point));
            Scene3dVec actual=scene3d_point(rendered,{(float)point.x,(float)point.y,(float)point.z});
            EXPECT_NEAR(actual.x,expected.x,1e-6);EXPECT_NEAR(actual.y,expected.y,1e-6);EXPECT_NEAR(actual.z,expected.z,1e-6);
        }
    }
}

TEST_F(Scene3dTest, DepthOnlyOccluderPreservesBackgroundAndHidesRearGeometry) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    attr(root,"background","#ffffff");
    DomElement* occluder=mesh();
    DomElement* material=occluder->last_child->as_element();
    attr(material,"depth_only","true");
    DomElement* rear=mesh("plane","#0000ff");attr(rear,"position","0 0 -1");
    pixel(snapshot(),64,64,255,255,255);
    attr(rear,"position","0 0 1");
    pixel(snapshot(),64,64,0,0,255);
    attr(material,"transparent","true");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
}

TEST_F(Scene3dTest, SvgExportEmbedsScenePixelsAndRejectsInvalidScene) {
    ASSERT_NE(load_page("test/scene3d/mixed.html"),nullptr);
    lam::Temp<char> svg(render_view_tree_to_svg(&ui,page->view_tree->root,320,240,nullptr,1));
    ASSERT_NE(svg.get(),nullptr);
    EXPECT_NE(strstr(svg.get(),"data:image/png;base64,"),nullptr);
    ASSERT_FALSE(ui.render_failed);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"viewport");
    ASSERT_NE(scene,nullptr);
    ASSERT_TRUE(scene->set_attribute("camera","absent-camera"));page->mutation_epoch++;
    lam::Temp<char> invalid(render_view_tree_to_svg(&ui,page->view_tree->root,320,240,nullptr,1));
    EXPECT_EQ(invalid.get(),nullptr);EXPECT_TRUE(ui.render_failed);
}

struct ScriptResourceRetirementProbe : DomDocumentResourceData {
    RootVector roots;
    bool* released;
    uint64_t* released_generation;
};

static void script_resource_retirement_probe_destroy(DomDocumentResourceData* data) {
    auto* probe = (ScriptResourceRetirementProbe*)data;
    *probe->released_generation = heap_generation_for(probe->roots.owner);
    *probe->released = true;
    root_vector_destroy(&probe->roots);
    mem_free(probe);
}

TEST_F(Scene3dTest, NativeDocumentRootsDoNotBorrowAnotherDocumentsEvaluator) {
    ASSERT_NE(load_page("test/demo/scene3d/shared-animation.html",500,720),nullptr);
    Runtime* runtime = page->js.runtime;
    ASSERT_NE(runtime,nullptr);
    Context* owner = (Context*)runtime_get_eval_context(runtime);
    ASSERT_EQ(radiant_dom_resource_owner(page),owner);
    EXPECT_EQ(radiant_dom_resource_owner(nullptr),nullptr);
    EXPECT_EQ(radiant_dom_resource_owner(&doc),nullptr);
    ASSERT_TRUE(radiant_history_initialize(&doc));
    EXPECT_EQ(radiant_history_length(&doc),1);
    for (auto* resource = doc.resources.get(); resource; resource = resource->next)
        EXPECT_EQ(resource->runtime_owner,nullptr);

    // script bootstrap binds its own runtime before publishing the JS alias.
    page->js.runtime = nullptr;
    Context* bootstrap_owner = radiant_dom_resource_owner(page);
    page->js.runtime = runtime;
    EXPECT_EQ(bootstrap_owner,owner);
    script_runner_cleanup_js_state(page);
    EXPECT_EQ(radiant_history_length(&doc),1);
    EXPECT_EQ(radiant_history_state(&doc).item,ItemNull.item);
}

TEST_F(Scene3dTest, NativeAnimationRootsRetireBeforeStandaloneJsRuntime) {
    ASSERT_NE(load_page("test/demo/scene3d/shared-animation.html",500,720),nullptr);
    Runtime* runtime = page->js.runtime;
    ASSERT_NE(runtime,nullptr);
    ASSERT_NE(runtime,page->lambda_runtime);
    Context* owner = (Context*)runtime_get_eval_context(runtime);
    uint64_t generation = heap_generation_for(owner);
    ASSERT_NE(generation,0u);
    bool animation_resources = false;
    for (auto* resource = page->resources.get(); resource; resource = resource->next)
        animation_resources |= resource->runtime_owner == owner;
    ASSERT_TRUE(animation_resources);
    ASSERT_FALSE(item_is_error(run_js("history.pushState({tag:'retained'},'', '#retained')")));
    int history_length = radiant_history_length(page);
    ASSERT_GT(history_length,1);
    DomElement* input_element = DomElement::create(page,"input",nullptr);
    ASSERT_NE(input_element,nullptr);
    ASSERT_TRUE(page->root->as_element()->append_child(input_element));
    radiant_input_set_live_value(input_element,"native text survives");
    radiant_input_set_files(input_element,js_new_object());
    ASSERT_NE(radiant_input_files(input_element).item,ItemNull.item);
    state_set(page->state,input_element,"retained-script-value",js_new_object());
    ASSERT_NE(state_get(page->state,input_element,"retained-script-value").item,ItemNull.item);

    bool released = false;
    uint64_t released_generation = 0;
    auto* probe = (ScriptResourceRetirementProbe*)mem_calloc(1,sizeof(ScriptResourceRetirementProbe),MEM_CAT_LAYOUT);
    ASSERT_NE(probe,nullptr);
    probe->released = &released;
    probe->released_generation = &released_generation;
    root_vector_init(&probe->roots,owner,"scene3d teardown probe");
    ASSERT_TRUE(root_vector_push(&probe->roots,make_string_item("retained until owner teardown")));
    ASSERT_TRUE(dom_document_add_resource(page,probe,script_resource_retirement_probe_destroy,owner));

    script_runner_cleanup_js_state(page);
    EXPECT_TRUE(released);
    EXPECT_EQ(released_generation,generation);
    EXPECT_EQ(page->js.runtime,nullptr);
    EXPECT_EQ(context,nullptr);
    if (page->state_store) EXPECT_EQ(page->state_store->semantic_context,nullptr);
    EXPECT_EQ(state_get(page->state,input_element,"retained-script-value").item,ItemNull.item);
    EXPECT_EQ(radiant_history_length(page),history_length);
    EXPECT_EQ(radiant_history_state(page).item,ItemNull.item);
    EXPECT_STREQ(url_get_hash(page->url),"#retained");
    EXPECT_STREQ(radiant_input_live_value(input_element),"native text survives");
    EXPECT_EQ(radiant_input_files(input_element).item,ItemNull.item);
    for (auto* resource = page->resources.get(); resource; resource = resource->next)
        EXPECT_NE(resource->runtime_owner,owner);
    // a surviving document can still tick native timelines after its JS drivers retire.
    auto* scheduler = page->state->animation_scheduler;
    EXPECT_NE(scheduler,nullptr);
    if (scheduler) animation_scheduler_tick(scheduler,scheduler->current_time+.25,nullptr);
    script_runner_cleanup_js_state(page);
    free_document(page);
    page = nullptr;
    EXPECT_EQ(context,nullptr);
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
    // just over the 512 MiB context budget: refused before the 16-byte source is read.
    EXPECT_EQ(native_gl_buffer(graphics,color,513u*1024u*1024u).id,0u);
    native_gl_destroy(graphics);EXPECT_EQ(native_gl_create(false,diagnostic,sizeof(diagnostic)),nullptr);
}
TEST_F(Scene3dTest, TextureResolutionCapBoundsMipBytesAndPreservesSourceAndOrientation) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    uint32_t pixels[16];for(unsigned i=0;i<16;i++) pixels[i]=i<8?0xff0000ff:0xffff0000;
    ImageSurface image={};image.width=4;image.height=4;image.pitch=16;image.pixels=pixels;image.alpha_mode=IMAGE_ALPHA_STRAIGHT;
    NativeGlSampler sampler;sampler.max_size=2;sampler.min_filter=0x2703;
    NativeGlResource texture=native_gl_texture(graphics,&image,sampler);ASSERT_NE(texture.id,0u)<<native_gl_diagnostic(graphics);
    NativeGlStats stats;ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(stats.allocated_bytes,20u);
    EXPECT_EQ(image.width,4);EXPECT_EQ(image.height,4);EXPECT_EQ(pixels[0],0xff0000ffu);EXPECT_EQ(pixels[15],0xffff0000u);
    NativeGlResource program=native_gl_program(graphics,
        "#version 330 core\nlayout(location=0) in vec2 p;out vec2 uv;void main(){uv=(p+1)*.5;gl_Position=vec4(p,0,1);}",
        "#version 330 core\nin vec2 uv;uniform sampler2D tex;out vec4 result;void main(){result=texture(tex,uv);}");
    float positions[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
    NativeGlResource buffer=native_gl_buffer(graphics,positions,sizeof(positions));
    NativeGlAttribute attribute={buffer,0,2,2*sizeof(float),0,0};
    NativeGlResource vertices=native_gl_vertices(graphics,&attribute,1,{}),target=native_gl_target(graphics,32,32);
    const float clear[]={0,0,0,0};ASSERT_TRUE(native_gl_begin(graphics,target,clear));
    NativeGlDraw draw={program,vertices,6,1,false,false,0,false};draw.bindings[draw.binding_count++]={"tex",texture,NATIVE_GL_BIND_TEXTURE};
    ASSERT_TRUE(native_gl_draw(graphics,&draw));
    ImageSurface* capture=native_gl_snapshot(graphics,target);ASSERT_NE(capture,nullptr);
    pixel(capture,16,4,255,0,0);pixel(capture,16,27,0,0,255);image_surface_destroy(capture);
    native_gl_release(graphics,texture);sampler.max_size=0;EXPECT_EQ(native_gl_texture(graphics,&image,sampler).id,0u);
    native_gl_destroy(graphics);
}

// a full-target quad whose fragment shader sees uv in 0..1
static const char* quad_vertex="#version 330 core\nlayout(location=0) in vec2 p;out vec2 uv;void main(){uv=(p+1)*.5;gl_Position=vec4(p,0,1);}";
static NativeGlResource quad_vertices(NativeGlContext* graphics) {
    static const float positions[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
    NativeGlResource buffer=native_gl_buffer(graphics,positions,sizeof(positions));
    NativeGlAttribute attribute={buffer,0,2,2*sizeof(float),0,0};
    return native_gl_vertices(graphics,&attribute,1,{});
}
static unsigned srgb_byte(float linear) {
    float v=linear<=.0031308f?linear*12.92f:1.055f*powf(linear,1/2.4f)-.055f;return (unsigned)lroundf(fminf(1,v)*255);
}

TEST_F(Scene3dTest, LinearDataTexturesSkipSrgbDecoding) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    uint32_t grey=0xff808080;ImageSurface image={};image.width=image.height=1;image.pitch=4;image.pixels=&grey;image.alpha_mode=IMAGE_ALPHA_STRAIGHT;
    NativeGlSampler data;data.srgb=false;
    NativeGlResource color=native_gl_texture(graphics,&image),linear=native_gl_texture(graphics,&image,data);
    ASSERT_NE(color.id,0u);ASSERT_NE(linear.id,0u);
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nin vec2 uv;uniform sampler2D tex;out vec4 result;void main(){result=texture(tex,uv);}");
    NativeGlResource vertices=quad_vertices(graphics),target=native_gl_target(graphics,8,8);const float clear[]={0,0,0,0};
    for (NativeGlResource texture:{color,linear}) {
        ASSERT_TRUE(native_gl_begin(graphics,target,clear));
        NativeGlDraw draw={program,vertices,6,1,false,false,0,false};draw.bindings[draw.binding_count++]={"tex",texture,NATIVE_GL_BIND_TEXTURE};
        ASSERT_TRUE(native_gl_draw(graphics,&draw));
        ImageSurface* capture=native_gl_snapshot(graphics,target);
        // a color map round-trips its sRGB byte; a data map's byte is a linear value, so it is encoded once on output
        unsigned expected=texture.id==color.id?128:srgb_byte(128/255.0f);
        pixel(capture,4,4,expected,expected,expected,255,2);image_surface_destroy(capture);
    }
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, SnapshotsPreserveEveryOpaqueSrgbByteAndPremultiplyTranslucentRows) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nout vec4 result;void main(){float x=floor(gl_FragCoord.x);"
        "vec3 s=vec3(x,255.-x,mod(x*17.,256.))/255.;"
        "vec3 linear=mix(s/12.92,pow((s+.055)/1.055,vec3(2.4)),step(vec3(.04045),s));"
        "float y=floor(gl_FragCoord.y);float a=y==0.?0.:y==1.?64./255.:y==2.?128./255.:1.;result=vec4(linear*a,a);}");
    ASSERT_NE(program.id,0u)<<native_gl_diagnostic(graphics);
    NativeGlResource vertices=quad_vertices(graphics),target=native_gl_target(graphics,256,4);const float clear[]={0,0,0,0};
    ASSERT_TRUE(native_gl_begin(graphics,target,clear));NativeGlDraw draw={program,vertices,6,1,false,false,0,false};
    ASSERT_TRUE(native_gl_draw(graphics,&draw));ImageSurface* capture=native_gl_snapshot(graphics,target);ASSERT_NE(capture,nullptr);
    const unsigned alpha[]={255,128,64,0};
    for(unsigned y=0;y<4;y++) for(unsigned x=0;x<256;x++)
        pixel(capture,x,y,(x*alpha[y]+127)/255,((255-x)*alpha[y]+127)/255,
            (((x*17)%256)*alpha[y]+127)/255,alpha[y],alpha[y]==255?0:2);
    image_surface_destroy(capture);native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, DrawBindingsAssignUnitsByNameAndRejectMismatches) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    uint32_t red=0xff0000ff,blue=0xffff0000;ImageSurface image={};image.width=image.height=1;image.pitch=4;image.alpha_mode=IMAGE_ALPHA_STRAIGHT;
    image.pixels=&red;NativeGlResource left=native_gl_texture(graphics,&image);
    image.pixels=&blue;NativeGlResource right=native_gl_texture(graphics,&image);
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nin vec2 uv;uniform sampler2D left,right;out vec4 result;"
        "void main(){result=uv.x<.5?texture(left,uv):texture(right,uv);}");
    NativeGlResource vertices=quad_vertices(graphics),target=native_gl_target(graphics,16,16);const float clear[]={0,0,0,0};
    // either order gives the same picture: each sampler gets the unit of its own binding
    for (bool swapped:{false,true}) {
        ASSERT_TRUE(native_gl_begin(graphics,target,clear));
        NativeGlDraw draw={program,vertices,6,1,false,false,0,false};
        draw.bindings[draw.binding_count++]={swapped?"right":"left",swapped?right:left,NATIVE_GL_BIND_TEXTURE};
        draw.bindings[draw.binding_count++]={swapped?"left":"right",swapped?left:right,NATIVE_GL_BIND_TEXTURE};
        ASSERT_TRUE(native_gl_draw(graphics,&draw))<<native_gl_diagnostic(graphics);
        ImageSurface* capture=native_gl_snapshot(graphics,target);
        pixel(capture,3,8,255,0,0);pixel(capture,12,8,0,0,255);image_surface_destroy(capture);
    }
    NativeGlDraw draw={program,vertices,6,1,false,false,0,false};
    draw.bindings[draw.binding_count++]={nullptr,left,NATIVE_GL_BIND_TEXTURE};EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.bindings[0]={"left",left,NATIVE_GL_BIND_CUBE};EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.bindings[0]={"left",target,NATIVE_GL_BIND_SHADOW};EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.bindings[0]={"left",left,NATIVE_GL_BIND_TEXTURE};draw.binding_count=NATIVE_GL_BINDING_LIMIT+1;EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.binding_count=1;native_gl_release(graphics,left);EXPECT_FALSE(native_gl_draw(graphics,&draw));
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, DepthTargetsCompareReadLitBeyondTheirEdgesAndStayOutOfSnapshots) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlStats before,stats;ASSERT_TRUE(native_gl_stats(graphics,&before));
    NativeGlResource depth=native_gl_depth_target(graphics,64,false);ASSERT_NE(depth.id,0u)<<native_gl_diagnostic(graphics);
    ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(stats.allocated_bytes-before.allocated_bytes,64u*64u*4u);
    // the caster covers the left half of the map at depth 0.5
    NativeGlResource caster=native_gl_program(graphics,
        "#version 330 core\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p.x*.5-.5,p.y,0,1);}",
        "#version 330 core\nout vec4 result;void main(){result=vec4(1);}");
    NativeGlResource vertices=quad_vertices(graphics);const float clear[]={0,0,0,0};
    ASSERT_TRUE(native_gl_begin_layer(graphics,depth,0,0,nullptr))<<native_gl_diagnostic(graphics);
    NativeGlDraw cast={caster,vertices,6,1,false,false,2,false};cast.depth_only=true;ASSERT_TRUE(native_gl_draw(graphics,&cast));
    // a pass may not sample the map it is drawing into
    NativeGlResource receiver=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nin vec2 uv;uniform sampler2DShadow shadow;out vec4 result;"
        "void main(){result=vec4(vec3(texture(shadow,vec3(uv.x*1.5-.25,uv.y,.75))),1);}");
    NativeGlDraw receive={receiver,vertices,6,1,false,false,0,false};receive.bindings[receive.binding_count++]={"shadow",depth,NATIVE_GL_BIND_SHADOW};
    EXPECT_FALSE(native_gl_draw(graphics,&receive));
    NativeGlResource target=native_gl_target(graphics,64,8);ASSERT_TRUE(native_gl_begin(graphics,target,clear));
    ASSERT_TRUE(native_gl_draw(graphics,&receive))<<native_gl_diagnostic(graphics);
    ImageSurface* capture=native_gl_snapshot(graphics,target);
    // left of the map (border) lit, behind the caster dark, the cleared half lit, right of the map lit
    pixel(capture,2,4,255,255,255);pixel(capture,24,4,0,0,0);pixel(capture,40,4,255,255,255);pixel(capture,62,4,255,255,255);
    image_surface_destroy(capture);
    EXPECT_FALSE(native_gl_begin(graphics,depth,clear));EXPECT_TRUE(native_gl_begin_layer(graphics,depth,0,0,nullptr));
    EXPECT_EQ(native_gl_snapshot(graphics,depth),nullptr);EXPECT_FALSE(native_gl_begin_layer(graphics,depth,1,0,nullptr));
    EXPECT_FALSE(native_gl_begin_layer(graphics,depth,0,1,nullptr));
    NativeGlResource cube=native_gl_depth_target(graphics,32,true);ASSERT_NE(cube.id,0u)<<native_gl_diagnostic(graphics);
    for (unsigned face=0;face<6;face++) EXPECT_TRUE(native_gl_begin_layer(graphics,cube,face,0,nullptr))<<face;
    EXPECT_FALSE(native_gl_begin_layer(graphics,cube,6,0,nullptr));
    EXPECT_EQ(native_gl_depth_target(graphics,0,false).id,0u);EXPECT_EQ(native_gl_depth_target(graphics,4097,false).id,0u);
    EXPECT_EQ(native_gl_depth_target(graphics,2049,true).id,0u);
    ASSERT_TRUE(native_gl_stats(graphics,&before));native_gl_release(graphics,cube);native_gl_release(graphics,depth);
    ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(before.allocated_bytes-stats.allocated_bytes,32u*32u*4u*6u+64u*64u*4u);
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, CubeTargetsKeepLinearHdrValuesPerFaceAndLevel) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlStats before,stats;ASSERT_TRUE(native_gl_stats(graphics,&before));
    NativeGlResource cube=native_gl_cube_target(graphics,16,3);ASSERT_NE(cube.id,0u)<<native_gl_diagnostic(graphics);
    ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(stats.allocated_bytes-before.allocated_bytes,16u*16u*4u+(256u+64u+16u)*6u*8u);
    // red names the face and may exceed 1; green names the level
    for (unsigned level=0;level<3;level++) for (unsigned face=0;face<6;face++) {
        const float color[]={(face+1)*.5f,level*.25f,0,1};ASSERT_TRUE(native_gl_begin_layer(graphics,cube,face,level,color))<<face<<","<<level;
    }
    const float negative[]={-1,0,0,1};EXPECT_FALSE(native_gl_begin_layer(graphics,cube,0,0,negative));
    const float clear[]={0,0,0,0};EXPECT_FALSE(native_gl_begin_layer(graphics,cube,0,3,clear));EXPECT_FALSE(native_gl_begin_layer(graphics,cube,6,0,clear));
    EXPECT_FALSE(native_gl_begin(graphics,cube,clear));EXPECT_EQ(native_gl_snapshot(graphics,cube),nullptr);
    // pixel (face, level) of a 6x3 target looks along that face's axis at that level
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nuniform samplerCube environment;out vec4 result;"
        "const vec3 axes[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));"
        "void main(){int face=int(gl_FragCoord.x),level=int(gl_FragCoord.y);"
        "vec4 c=textureLod(environment,axes[face],float(level));result=vec4(c.r/4.,c.g,0,1);}");
    NativeGlResource vertices=quad_vertices(graphics),target=native_gl_target(graphics,6,3);ASSERT_TRUE(native_gl_begin(graphics,target,clear));
    NativeGlDraw draw={program,vertices,6,1,false,false,0,false};draw.bindings[draw.binding_count++]={"environment",cube,NATIVE_GL_BIND_CUBE};
    ASSERT_TRUE(native_gl_draw(graphics,&draw))<<native_gl_diagnostic(graphics);
    ImageSurface* capture=native_gl_snapshot(graphics,target);
    for (unsigned level=0;level<3;level++) for (unsigned face=0;face<6;face++)
        pixel(capture,face,2-level,srgb_byte((face+1)*.5f/4),srgb_byte(level*.25f),0,255,2);
    image_surface_destroy(capture);
    draw.bindings[0]={"environment",cube,NATIVE_GL_BIND_TEXTURE};EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.bindings[0]={"environment",cube,NATIVE_GL_BIND_SHADOW};EXPECT_FALSE(native_gl_draw(graphics,&draw));
    EXPECT_EQ(native_gl_cube_target(graphics,16,6).id,0u);EXPECT_EQ(native_gl_cube_target(graphics,2049,1).id,0u);
    EXPECT_EQ(native_gl_cube_target(graphics,16,0).id,0u);
    ASSERT_TRUE(native_gl_stats(graphics,&before));native_gl_release(graphics,cube);ASSERT_TRUE(native_gl_stats(graphics,&stats));
    EXPECT_EQ(before.allocated_bytes-stats.allocated_bytes,16u*16u*4u+(256u+64u+16u)*6u*8u);EXPECT_EQ(before.resources-stats.resources,1u);
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, FloatTargetsDrawRegionByRegionAndKeepHdrValues) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlStats before,stats;ASSERT_TRUE(native_gl_stats(graphics,&before));
    NativeGlResource atlas=native_gl_float_target(graphics,8,4,true);ASSERT_NE(atlas.id,0u)<<native_gl_diagnostic(graphics);
    ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(stats.allocated_bytes-before.allocated_bytes,8u*4u*12u);
    // the left half clears to an HDR red; a draw into the right half stays inside its region
    const float black[]={0,0,0,1},hdr[]={2,0,0,1};
    ASSERT_TRUE(native_gl_begin_region(graphics,atlas,0,0,8,4,black));ASSERT_TRUE(native_gl_begin_region(graphics,atlas,0,0,4,4,hdr));
    NativeGlResource fill=native_gl_program(graphics,quad_vertex,"#version 330 core\nout vec4 result;void main(){result=vec4(0,.25,0,1);}");
    NativeGlResource vertices=quad_vertices(graphics);
    ASSERT_TRUE(native_gl_begin_region(graphics,atlas,4,0,4,4,nullptr));
    NativeGlDraw draw={fill,vertices,6,1,false,false,2,false};ASSERT_TRUE(native_gl_draw(graphics,&draw))<<native_gl_diagnostic(graphics);
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nin vec2 uv;uniform sampler2D atlas;out vec4 result;void main(){vec4 c=texture(atlas,uv);result=vec4(c.r/4.,c.g,0,1);}");
    NativeGlDraw sample={program,vertices,6,1,false,false,0,false};sample.bindings[sample.binding_count++]={"atlas",atlas,NATIVE_GL_BIND_FLOAT};
    // a pass may not sample the atlas it is drawing into
    EXPECT_FALSE(native_gl_draw(graphics,&sample));
    NativeGlResource target=native_gl_target(graphics,8,4);const float clear[]={0,0,0,0};ASSERT_TRUE(native_gl_begin(graphics,target,clear));
    ASSERT_TRUE(native_gl_draw(graphics,&sample))<<native_gl_diagnostic(graphics);
    ImageSurface* capture=native_gl_snapshot(graphics,target);
    pixel(capture,1,2,srgb_byte(.5f),0,0,255,2);pixel(capture,6,2,0,srgb_byte(.25f),0,255,2);image_surface_destroy(capture);
    sample.bindings[0]={"atlas",atlas,NATIVE_GL_BIND_CUBE};EXPECT_FALSE(native_gl_draw(graphics,&sample));
    sample.bindings[0]={"atlas",target,NATIVE_GL_BIND_FLOAT};EXPECT_FALSE(native_gl_draw(graphics,&sample));
    const float negative[]={-1,0,0,1};
    EXPECT_FALSE(native_gl_begin_region(graphics,atlas,4,0,5,4,nullptr));EXPECT_FALSE(native_gl_begin_region(graphics,atlas,0,0,0,4,nullptr));
    EXPECT_FALSE(native_gl_begin_region(graphics,atlas,0,0,8,4,negative));EXPECT_FALSE(native_gl_begin_region(graphics,target,0,0,8,4,nullptr));
    EXPECT_FALSE(native_gl_begin(graphics,atlas,clear));EXPECT_FALSE(native_gl_begin_layer(graphics,atlas,0,0,clear));
    EXPECT_EQ(native_gl_snapshot(graphics,atlas),nullptr);
    EXPECT_EQ(native_gl_float_target(graphics,0,4,false).id,0u);EXPECT_EQ(native_gl_float_target(graphics,4097,4,false).id,0u);
    ASSERT_TRUE(native_gl_stats(graphics,&before));native_gl_release(graphics,atlas);ASSERT_TRUE(native_gl_stats(graphics,&stats));
    EXPECT_EQ(before.allocated_bytes-stats.allocated_bytes,8u*4u*12u);
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, MultisampledHdrTargetsResolveUnclippedRadianceAndRejectFeedback) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));
    ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlResource hdr=native_gl_target(graphics,8,4,4,true),output=native_gl_target(graphics,8,4,1);
    ASSERT_NE(hdr.id,0u);ASSERT_NE(output.id,0u);
    NativeGlResource program=native_gl_program(graphics,quad_vertex,
        "#version 330 core\nin vec2 uv;uniform sampler2D source;out vec4 result;"
        "void main(){result=vec4(texture(source,uv).rgb/4.,1.);}");
    NativeGlDraw sample={program,quad_vertices(graphics),6,1,false,false,2,false};
    sample.bindings[sample.binding_count++]={"source",hdr,NATIVE_GL_BIND_TARGET};
    const float clear[]={0,0,0,0},radiance[]={4,1,.25f,1};
    ASSERT_TRUE(native_gl_begin(graphics,hdr,radiance));
    EXPECT_EQ(native_gl_snapshot(graphics,hdr),nullptr);
    EXPECT_FALSE(native_gl_draw(graphics,&sample));
    ASSERT_TRUE(native_gl_begin(graphics,output,clear));ASSERT_TRUE(native_gl_draw(graphics,&sample));
    ImageSurface* image=native_gl_snapshot(graphics,output);
    pixel(image,4,2,255,srgb_byte(.25f),srgb_byte(.0625f));image_surface_destroy(image);
    // another producer frame invalidates the resolve, rather than sampling the prior frame.
    const float next[]={1,0,0,1};ASSERT_TRUE(native_gl_begin(graphics,hdr,next));
    ASSERT_TRUE(native_gl_begin(graphics,output,clear));ASSERT_TRUE(native_gl_draw(graphics,&sample));
    image=native_gl_snapshot(graphics,output);pixel(image,4,2,srgb_byte(.25f),0,0);image_surface_destroy(image);
    native_gl_release(graphics,hdr);EXPECT_FALSE(native_gl_draw(graphics,&sample));
    native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, HdrOutputPreservesAlphaBackgroundAndSteadyResources) {
    DomElement* plane=mesh("plane","#ff0000");DomElement* material=plane->last_child->as_element();
    attr(material,"opacity","0.5");pixel(snapshot(),64,64,128,0,0,128);
    attr(root,"background","#00ff00");pixel(snapshot(),64,64,188,188,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    for (unsigned i=0;i<100;i++) {
        attr(camera,"fov",i%2?"90":"91");ASSERT_NE(snapshot(),nullptr);
    }
    ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.projection_generation,after.projection_generation);
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);
    EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
    EXPECT_EQ(after.snapshot_generation-before.snapshot_generation,100u);
    attr(material,"opacity","1");attr(material,"color","#000000");
    attr(material,"emissive","#ffffff");attr(material,"emissive-intensity","4");attr(root,"tone-mapping","neutral");
    pixel(snapshot(),64,64,253,253,253,255,2);
    // the final tone-map stage leaves the authored background in its display-color domain.
    pixel(snapshot(),0,0,0,255,0);
    scene3d_context_lost(root);pixel(snapshot(),64,64,253,253,253,255,2);
}

TEST_F(Scene3dTest, HdrOutputToneMapsEachCoveredSampleBeforeDisplayResolve) {
    DomElement* plane=mesh("plane","#fdfdfd");attr(plane,"rotation","0 0 0.3");
    attr(root,"background","#202428");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    DomElement* material=plane->last_child->as_element();attr(material,"color","#000000");
    attr(material,"emissive","#ffffff");attr(material,"emissive-intensity","4");attr(root,"tone-mapping","neutral");
    image=snapshot();ASSERT_NE(image,nullptr);
    // resolving HDR coverage before nonlinear output conversion brightens silhouette pixels.
    for (unsigned i=0;i<sizeof(expected);i++)
        EXPECT_NEAR(((const uint8_t*)image->pixels)[i],expected[i],2)<<"byte "<<i;
}

TEST_F(Scene3dTest, BloomUsesUnclippedRadianceKeepsZeroIdentityAndRetainsTargets) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");attr(root,"background","#000000");
    DomElement* plane=mesh("plane","#000000");attr(plane->first_child->as_element(),"size","0.25 0.25 1");
    DomElement* material=plane->last_child->as_element();attr(material,"emissive","#ff0000");attr(material,"emissive-intensity","8");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,64,64,255,0,0);pixel(image,71,64,0,0,0);
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    DomElement* chain=element("postprocessing",root),*bloom=element("effect",chain);
    attr(bloom,"type","bloom");attr(bloom,"strength","0");attr(bloom,"threshold","1");attr(bloom,"radius","4");
    image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(image->pixels,expected,sizeof(expected)),0);
    attr(bloom,"strength","1");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_GT(((const uint8_t*)image->pixels)[64*image->pitch+71*4],0u);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.postprocess_passes,4u);
    attr(bloom,"strength","2");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
    EXPECT_EQ(after.postprocess_passes-before.postprocess_passes,4u);
    image=snapshot(128,128,2);ASSERT_NE(image,nullptr);EXPECT_EQ(image->width,256);
    EXPECT_GT(((const uint8_t*)image->pixels)[128*image->pitch+142*4],0u);
    for (unsigned i=0;i<100;i++) ASSERT_NE(snapshot(i%2?128:64,i%2?128:64),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(before.graphics.allocated_bytes,after.graphics.allocated_bytes);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
    attr(root,"background","transparent");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"opaque viewport"),nullptr);
}

TEST_F(Scene3dTest, FxaaSmoothsShaderEdgesWithoutChangingFlatRegions) {
    attr(camera,"type","orthographic");attr(camera,"view_height","2");attr(root,"background","#000000");
    DomElement* plane=mesh();attr(plane->first_child->as_element(),"size","4 4 1");
    DomElement* material=plane->last_child->as_element();attr(material,"type","shader");
    attr(material,"vertex","void main(){gl_Position=projection_matrix*view_matrix*model_matrix*vec4(position,1.0);}");
    attr(material,"fragment","void main(){float q=step(gl_FragCoord.x,gl_FragCoord.y);fragment_color=vec4(vec3(q),1.0);}");
    ASSERT_NE(snapshot(),nullptr);
    DomElement* chain=element("postprocessing",root),*fxaa=element("effect",chain);attr(fxaa,"type","fxaa");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,20,20,255,255,255);pixel(image,100,100,0,0,0);
    unsigned softened=0;for (unsigned i=0;i<128*128;i++) {
        unsigned red=((const uint8_t*)image->pixels)[i*4];if (red>0 && red<255) softened++;
    }
    EXPECT_GT(softened,100u);
    Scene3dStats stats;ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.postprocess_passes,1u);
}

TEST_F(Scene3dTest, CustomColorPassesRespectDomainsCacheProgramsAndReleaseTheirResources) {
    attr(root,"background","#000000");mesh();
    DomElement* chain=element("postprocessing",root),*linear=element("effect",chain),*display=element("effect",chain);
    attr(linear,"type","shader");attr(linear,"fragment","void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution).bgra;}");
    attr(display,"type","shader");attr(display,"domain","display");
    attr(display,"fragment","void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution).gbra;}");
    pixel(snapshot(),64,64,0,255,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.shader_links,2u);
    attr(camera,"fov","91");pixel(snapshot(),64,64,0,255,0);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.shader_links,after.shader_links);EXPECT_EQ(before.graphics.resources,after.graphics.resources);
    attr(display,"fragment","uniform float missing;void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution)*missing;}");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"has no value"),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&before));
    for (unsigned i=0;i<40;i++) {
        StrBuf* body=strbuf_new();strbuf_append_format(body,
            "uniform float missing%u;void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution)*missing%u;}",i,i);
        attr(display,"fragment",body->str);strbuf_free(body);
        EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    }
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.graphics.resources,after.graphics.resources);
    attr(display,"fragment","void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution).gbra;}");
    pixel(snapshot(),64,64,0,255,0);
    attr(linear,"domain","display");attr(display,"domain","linear");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"linear effects before"),nullptr);
    root->remove_child(chain);doc.mutation_epoch++;pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.shader_programs,0u);EXPECT_LT(after.graphics.resources,before.graphics.resources);
}

TEST(Scene3dMath, DfgTableMatchesThreeJsSplitSumIntegration) {
    // {row (n·v), column (roughness), scale, bias} from the DATA table of Three.js r186 (three.module.js)
    const struct { unsigned row, col; float scale, bias; } three[]={
        {0,0,0.14709f,0.85205f},{4,8,0.76465f,0.06198f},{8,4,0.95947f,0.02449f},
        {12,12,0.59326f,0.00114f},{15,15,0.34521f,0.00007f},{2,14,0.70947f,0.02383f}};
    const float* table=scene3d_dfg_table();ASSERT_NE(table,nullptr);
    for (const auto& t:three) {
        const float* texel=table+(t.row*16+t.col)*4;
        EXPECT_NEAR(texel[0],t.scale,0.015f)<<t.row<<","<<t.col;EXPECT_NEAR(texel[1],t.bias,0.015f)<<t.row<<","<<t.col;
    }
    // a smooth surface seen head-on reflects all it can: scale + bias reaches 1
    EXPECT_NEAR(table[(15*16)*4]+table[(15*16)*4+1],1.0f,0.01f);
}

TEST_F(Scene3dTest, VertexOnlyBuffersDoNotRetainIndexShadows) {
    char diagnostic[256];NativeGlContext* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    float positions[]={0,0,0,1,0,0,0,1,0};
    NativeGlResource buffer=native_gl_buffer(graphics,positions,sizeof(positions),false);ASSERT_NE(buffer.id,0u);
    NativeGlStats stats;ASSERT_TRUE(native_gl_stats(graphics,&stats));EXPECT_EQ(stats.allocated_bytes,sizeof(positions));
    NativeGlAttribute attribute={buffer,0,3,3*sizeof(float),0,0};
    EXPECT_NE(native_gl_vertices(graphics,&attribute,1,{}).id,0u);
    EXPECT_EQ(native_gl_vertices(graphics,&attribute,1,buffer).id,0u);
    EXPECT_NE(strstr(native_gl_diagnostic(graphics),"validation data"),nullptr);
    native_gl_destroy(graphics);
}

TEST(Scene3dMath, ViewBoxRejectsNonfiniteNegativeAndTrailingComponents) {
    const char* invalid[]={"0 0 -1 1","0 0 1 -1","nan 0 1 1","0 0 inf 1","0 0 1 1 2","0 0 1 1junk"};
    for (const char* value:invalid) EXPECT_FALSE(svg_parse_viewbox(value).has_viewbox)<<value;
    EXPECT_TRUE(svg_parse_viewbox("0 0 0 1").has_viewbox);EXPECT_TRUE(svg_parse_viewbox("1, 2, 3, 4").has_viewbox);
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
    NativeGlDraw draw={program,vertices,3,1,true,false,0,false};
    EXPECT_FALSE(native_gl_draw(graphics,&draw));draw.indexed=false;draw.count=4;EXPECT_FALSE(native_gl_draw(graphics,&draw));
    draw.count=3;EXPECT_TRUE(native_gl_draw(graphics,&draw));
    attribute.divisor=1;vertices=native_gl_vertices(graphics,&attribute,1,{});draw.vertices=vertices;draw.instances=4;
    EXPECT_FALSE(native_gl_draw(graphics,&draw));draw.instances=1;native_gl_release(graphics,buffer);EXPECT_FALSE(native_gl_draw(graphics,&draw));
    attribute.offset=sizeof(positions);EXPECT_EQ(native_gl_vertices(graphics,&attribute,1,{}).id,0u);
    native_gl_destroy(graphics);
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

TEST_F(Scene3dTest, IndependentUvSetsAndAffineTransformsSelectExpectedTexels) {
    attr(camera,"type","orthographic");attr(camera,"view_height","2");
    DomElement* plane=buffer_quad(),*g=plane->first_child->as_element(),*m=plane->last_child->as_element();
    attr(g,"uvs",".25 .75 .25 .75 .25 .75 .25 .75");attr(g,"uvs1",".75 .75 .75 .75 .75 .75 .75 .75");
    DomElement* texture=map_texture("map","test/scene3d/assets/fidelity/colors.png");attr(m,"texture","map");attr(texture,"uv-set","0");
    pixel(snapshot(),64,64,255,0,0);attr(texture,"uv-set","1");pixel(snapshot(),64,64,0,255,0);
    attr(texture,"uv-set","0");attr(texture,"uv-transform","-1 0 1 0 1 0");pixel(snapshot(),64,64,0,255,0);
    attr(texture,"uv-transform","0 -1 1 1 0 0");pixel(snapshot(),64,64,0,0,255);
    attr(texture,"uv-set","1");attr(g,"uvs1","");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"absent texture coordinate"),nullptr);
    attr(g,"uvs1",".75 .75 .75 .75 .75 .75 .75 .75");ASSERT_NE(snapshot(),nullptr);
    attr(texture,"uv-set","2");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"uv-set 0 or 1"),nullptr);
}

TEST_F(Scene3dTest, OcclusionAndNormalMapsUseTheirOwnCoordinatesAndMirroredFrame) {
    attr(camera,"type","orthographic");attr(camera,"view_height","2");
    DomElement* plane=buffer_quad("standard"),*g=plane->first_child->as_element(),*m=plane->last_child->as_element();
    attr(g,"uvs",".75 .25 .75 .25 .75 .25 .75 .25");attr(g,"uvs1",".75 .75 .75 .75 .75 .75 .75 .75");
    DomElement* base=map_texture("base","test/scene3d/assets/fidelity/colors.png");attr(m,"texture","base");
    DomElement* ao=map_texture("ao","test/scene3d/assets/fidelity/colors.png");attr(m,"occlusion-texture","ao");
    DomElement* ambient=element("light",root);attr(ambient,"type","ambient");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);EXPECT_GT(((const uint8_t*)image->pixels)[64*image->pitch+64*4],200u);
    attr(ao,"uv-set","1");pixel(snapshot(),64,64,0,0,0);
    // the normal-map frame follows a mirrored map, even with an authored tangent for UV0.
    root->remove_child(ambient);attr(m,"occlusion-strength","0");attr(base,"uv-transform","0 0 .75 0 0 .25");
    attr(g,"uvs","0 0 1 0 1 1 0 1");attr(g,"tangents","1 0 0 1 1 0 0 1 1 0 0 1 1 0 0 1");
    DomElement* normal=map_texture("normal-map","test/scene3d/assets/fidelity/normal.png");attr(m,"normal-texture","normal-map");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","1 0 1");
    image=snapshot();ASSERT_NE(image,nullptr);unsigned before=((const uint8_t*)image->pixels)[64*image->pitch+64*4];
    attr(normal,"uv-transform","-1 0 1 0 1 0");image=snapshot();ASSERT_NE(image,nullptr);
    unsigned after=((const uint8_t*)image->pixels)[64*image->pitch+64*4];EXPECT_GT(before,after+80);
    // equivalent transformed coordinates without authored tangents give the same visible frame.
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    attr(normal,"uv-transform","1 0 0 0 1 0");attr(g,"uvs","1 0 0 0 0 1 1 1");attr(g,"tangents","");
    image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
}

TEST_F(Scene3dTest, VertexAlphaCombinesWithFactorsBeforeMaskingAndKeepsOpaqueMode) {
    attr(camera,"type","orthographic");attr(camera,"view_height","2");
    DomElement* plane=buffer_quad(),*g=plane->first_child->as_element(),*m=plane->last_child->as_element();
    attr(g,"colors","1 0 0 .5 1 0 0 .5 1 0 0 .5 1 0 0 .5");pixel(snapshot(),64,64,128,0,0,128);
    attr(m,"opacity",".5");pixel(snapshot(),64,64,64,0,0,64);
    attr(m,"alpha-mode","mask");attr(m,"alpha-cutoff",".4");pixel(snapshot(),64,64,0,0,0,0);
    attr(m,"alpha-cutoff",".2");pixel(snapshot(),64,64,255,0,0);
    attr(m,"alpha-mode","opaque");attr(m,"opacity","0");pixel(snapshot(),64,64,255,0,0);
    // a custom RGBA geometry sees vec4 color; the existing RGB profile still sees vec3.
    attr(m,"type","shader");attr(m,"transparent","true");
    attr(m,"vertex","out vec4 tint;void main(){tint=color;gl_Position=projection_matrix*view_matrix*model_matrix*vec4(position,1);}");
    attr(m,"fragment","in vec4 tint;void main(){fragment_color=vec4(tint.rgb*tint.a,tint.a);}");pixel(snapshot(),64,64,128,0,0,128);
}

TEST_F(Scene3dTest, ImportedCamerasFrameTheSceneAndExplicitSelectionWins) {
    ASSERT_NE(load_page("test/scene3d/model-fidelity.ls"),nullptr);
    DomElement* scene=dom_find_element_by_id(page->root->as_element(),"fidelity-scene");ASSERT_NE(scene,nullptr);
    ImageSurface* image=scene3d_snapshot(scene,&ui,128,128,1);ASSERT_NE(image,nullptr)<<scene3d_diagnostic(scene);
    uint8_t finite[128*128*4];memcpy(finite,image->pixels,sizeof(finite));pixel(image,40,64,0,0,0);
    ASSERT_TRUE(scene->set_attribute("camera","fidelity-asset-camera-2"));page->mutation_epoch++;image=scene3d_snapshot(scene,&ui,128,128,1);ASSERT_NE(image,nullptr);
    EXPECT_NE(memcmp(finite,image->pixels,sizeof(finite)),0);unsigned at40=64*image->pitch+40*4;
    EXPECT_GT(((const uint8_t*)image->pixels)[at40]+((const uint8_t*)image->pixels)[at40+1]+((const uint8_t*)image->pixels)[at40+2],100u);
    ASSERT_TRUE(scene->set_attribute("camera","override"));page->mutation_epoch++;image=scene3d_snapshot(scene,&ui,128,128,1);ASSERT_NE(image,nullptr);
    unsigned at10=64*image->pitch+10*4;EXPECT_GT(((const uint8_t*)image->pixels)[at10]+((const uint8_t*)image->pixels)[at10+1]+((const uint8_t*)image->pixels)[at10+2],100u);
    DomElement* model=dom_find_element_by_id(page->root->as_element(),"fidelity-asset-node-0-primitive-0");ASSERT_NE(model,nullptr);
    ASSERT_TRUE(model->set_attribute("scale","10000 10000 1"));ASSERT_TRUE(scene->set_attribute("camera","fidelity-asset-camera-3"));page->mutation_epoch++;
    image=scene3d_snapshot(scene,&ui,128,128,1);ASSERT_NE(image,nullptr)<<scene3d_diagnostic(scene);
    unsigned center=64*image->pitch+64*4;EXPECT_GT(((const uint8_t*)image->pixels)[center]+((const uint8_t*)image->pixels)[center+1]+((const uint8_t*)image->pixels)[center+2],100u);
    Scene3dMatrix lens=scene3d_perspective(90,1,.1f,INFINITY);
    for(float v:lens.v) EXPECT_TRUE(isfinite(v));EXPECT_EQ(lens.v[10],-1);EXPECT_FLOAT_EQ(lens.v[14],-.2f);
}

TEST_F(Scene3dTest, GltfAndGlbRoundTripsKeepIndependentMapPixels) {
    Input* owner=InputManager::create_input(nullptr);MarkBuilder builder(owner);
    DomElement* ambient=element("light",root);attr(ambient,"type","ambient");DomElement* model=nullptr;
    for(const char* path:{"test/scene3d/assets/fidelity/model.gltf","test/scene3d/assets/fidelity/physical.gltf","test/scene3d/assets/fidelity/glass.gltf"}) {
    SCOPED_TRACE(path);if(model) root->remove_child(model);
    Input* source=input_from_url(builder.createString(path),builder.createString("scene3d-asset"),nullptr,doc.url);
    ASSERT_NE(source,nullptr);ASSERT_FALSE(source->parse_failed)<<source->parse_error_message;
    model=build_dom_tree_from_element(source->root.element,&doc,root);ASSERT_NE(model,nullptr);doc.mutation_epoch++;
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    unsigned center=64*image->pitch+64*4;
    EXPECT_GT(((const uint8_t*)image->pixels)[center]+((const uint8_t*)image->pixels)[center+1]+((const uint8_t*)image->pixels)[center+2],100u);
    for(bool binary:{false,true}) {
        StrBuf* bytes=strbuf_new();const char* error=nullptr;ASSERT_TRUE(format_gltf_bytes(source->root,binary,bytes,&error))<<error;
        Url* uri=url_parse_with_base(binary?"roundtrip.glb":"roundtrip.gltf",doc.url);
        Input* again=input_from_source_n(bytes->str,bytes->length,uri,builder.createString("scene3d-asset"),nullptr);strbuf_free(bytes);
        ASSERT_NE(again,nullptr);ASSERT_FALSE(again->parse_failed)<<again->parse_error_message;
        root->remove_child(model);model=build_dom_tree_from_element(again->root.element,&doc,root);ASSERT_NE(model,nullptr);doc.mutation_epoch++;
        image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    }
    }
}

TEST_F(Scene3dTest, PhysicalZeroFactorsRecoverStandardAndFactorTracksKeepTheirProgram) {
    attr(camera,"type","orthographic");attr(camera,"view_height","3");
    DomElement* object=mesh("box","#204060","standard"),*material=object->last_child->as_element();
    attr(object,"id","surface");attr(object,"rotation",".2 .35 0");attr(material,"roughness",".65");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position",".3 .2 2");attr(sun,"intensity",".3");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t standard[128*128*4];memcpy(standard,image->pixels,sizeof(standard));
    attr(material,"type","physical");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_EQ(memcmp(standard,image->pixels,sizeof(standard)),0);
    for (const char* mode:{"mask","blend"}) {
        attr(material,"alpha-mode",mode);attr(material,"opacity",".6");attr(material,"type","standard");
        image=snapshot();ASSERT_NE(image,nullptr);memcpy(standard,image->pixels,sizeof(standard));
        attr(material,"type","physical");image=snapshot();ASSERT_NE(image,nullptr);
        EXPECT_EQ(memcmp(standard,image->pixels,sizeof(standard)),0)<<mode;
    }
    attr(material,"alpha-mode","opaque");attr(material,"opacity","1");image=snapshot();ASSERT_NE(image,nullptr);memcpy(standard,image->pixels,sizeof(standard));
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    attr(material,"clearcoat","1");attr(material,"clearcoat-roughness",".15");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_NE(memcmp(standard,image->pixels,sizeof(standard)),0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.material_links,after.material_links);
    DomElement* clip=element("animation-clip",root);attr(clip,"id","coat");
    DomElement* track=element("keyframe-track",clip);attr(track,"path","surface.material.clearcoat");attr(track,"type","number");attr(track,"times","0 1");attr(track,"values","0 1");
    ASSERT_NE(snapshot(),nullptr);Scene3dAnimationState* state=scene3d_animations(root);ASSERT_NE(state,nullptr);
    AnimationActionState* action=scene3d_animation_action(state,"coat");ASSERT_NE(action,nullptr);ASSERT_TRUE(animation_action_play(action));
    ASSERT_TRUE(scene3d_animation_seek(state,.5));ASSERT_TRUE(scene3d_stats(root,&before));image=snapshot();ASSERT_NE(image,nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.material_links,after.material_links);EXPECT_EQ(before.projection_generation,after.projection_generation);
    AnimationValue value={};ASSERT_TRUE(scene3d_animation_value(state,material,"clearcoat",&value));EXPECT_NEAR(value.numbers[0],.5,1e-6);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
}

TEST_F(Scene3dTest, PhysicalMapsFourShadowsAndSkinPaletteFitTheCheckedBindingPlan) {
    attr(camera,"type","orthographic");attr(camera,"view_height","3");
    DomElement* object=buffer_quad("physical"),*g=object->first_child->as_element(),*m=object->last_child->as_element();
    attr(g,"uvs","0 0 1 0 1 1 0 1");attr(g,"uvs1","0 1 1 1 1 0 0 0");
    attr(g,"skin-indices","0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");attr(g,"skin-weights","1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0");
    DomElement* rig=element("skeleton",root);attr(rig,"id","rig");DomElement* bone=element("bone",rig);attr(bone,"id","bone");attr(object,"skeleton","rig");
    const char* keys[]={"texture","metal-rough-texture","normal-texture","occlusion-texture","emissive-texture",
        "clearcoat-texture","clearcoat-roughness-texture","clearcoat-normal-texture","sheen-color-texture","sheen-roughness-texture"};
    for(unsigned i=0;i<10;i++) {
        char id[24];snprintf(id,sizeof(id),"layer-%u",i);
        DomElement* texture=map_texture(id,(i==2||i==7)?"test/scene3d/assets/fidelity/normal.png":"test/scene3d/assets/fidelity/colors.png");
        attr(texture,"uv-set",i%2?"1":"0");attr(m,keys[i],id);
    }
    attr(m,"clearcoat",".7");attr(m,"sheen",".4");attr(m,"sheen-color","#cc3366");attr(m,"emissive","#333333");
    DomElement* env=element("environment",root);attr(env,"id","studio");attr(env,"src","test/scene3d/assets/hdr/studio.hdr");attr(root,"environment","studio");
    for(unsigned i=0;i<4;i++) {
        DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position",i%2?"1 1 4":"-1 1 4");
        attr(sun,"intensity",".2");attr(sun,"cast-shadow","true");attr(sun,"shadow-map-size","512");
    }
    ASSERT_NE(snapshot(),nullptr);Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    EXPECT_EQ(before.textures,10u);EXPECT_EQ(before.shadow_passes,4u);
    attr(m,"clearcoat",".4");attr(m,"sheen",".6");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.material_links,after.material_links);EXPECT_EQ(before.environment_builds,after.environment_builds);
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);
    // only map presence changes the shader variant; returning to a prior mask reuses it.
    ASSERT_TRUE(m->remove_attribute("sheen-roughness-texture"));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.material_links,before.material_links+1);
    attr(m,"sheen-roughness-texture","layer-9");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.material_links,before.material_links+1);
    for(unsigned i=0;i<100;i++) ASSERT_NE(snapshot(i%2?128:96,i%2?128:96),nullptr);
    Scene3dStats settled;ASSERT_TRUE(scene3d_stats(root,&settled));EXPECT_EQ(after.graphics.resources,settled.graphics.resources);
    EXPECT_EQ(after.graphics.gpu_bytes,settled.graphics.gpu_bytes);EXPECT_EQ(after.material_links,settled.material_links);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
    attr(m,"clearcoat-texture","missing");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"texture reference"),nullptr);
}

TEST_F(Scene3dTest, TransmissionZeroRecoversPhysicalAndOneCapturePreservesOpaqueColorAndDepth) {
    DomElement* m=glass_stage(),*glass=m->parent->as_element();
    attr(glass,"visible","false");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    uint8_t background[128*128*4],opaque[128*128*4],refracted[128*128*4];memcpy(background,image->pixels,sizeof(background));
    attr(glass,"visible","true");image=snapshot();ASSERT_NE(image,nullptr);memcpy(opaque,image->pixels,sizeof(opaque));
    attr(m,"transmission","0");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(opaque,image->pixels,sizeof(opaque)),0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.transmission_captures,0u);
    attr(m,"ior","1");attr(m,"transmission","1");image=snapshot();ASSERT_NE(image,nullptr);
    const uint8_t* source=background+(48*128+80)*4;EXPECT_GT(source[0]+source[1]+source[2],100);
    pixel(image,80,48,source[0],source[1],source[2],255,2);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.transmission_captures,before.transmission_captures+1);
    EXPECT_EQ(after.graphics.frames,before.graphics.frames+1);EXPECT_EQ(after.material_links,before.material_links);
    EXPECT_EQ(after.graphics.resources,before.graphics.resources);
    ImageSurface* dense=snapshot(128,128,2);ASSERT_NE(dense,nullptr);pixel(dense,160,96,source[0],source[1],source[2],255,2);
    image=snapshot();ASSERT_NE(image,nullptr);
    memcpy(refracted,image->pixels,sizeof(refracted));attr(m,"ior","1.8");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_NE(memcmp(refracted,image->pixels,sizeof(refracted)),0);
    memcpy(refracted,image->pixels,sizeof(refracted));attr(m,"thickness","2");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_NE(memcmp(refracted,image->pixels,sizeof(refracted)),0);
    attr(m,"ior","1");attr(m,"thickness","1");attr(m,"attenuation-color","#00ff00");attr(m,"attenuation-distance","1");
    image=snapshot();ASSERT_NE(image,nullptr);pixel(image,80,48,0,source[1],0,255,2);
    ASSERT_TRUE(scene3d_stats(root,&before));ASSERT_EQ(snapshot(),image);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.graphics.frames,after.graphics.frames);EXPECT_EQ(before.transmission_captures,after.transmission_captures);
    attr(m,"alpha-mode","mask");attr(m,"opacity",".5");attr(m,"alpha-cutoff",".6");image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_EQ(memcmp(background,image->pixels,sizeof(background)),0);
    attr(m,"alpha-mode","opaque");attr(m,"opacity","1");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"position",".25 0 4");attr(camera,"target",".25 0 0");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(after.transmission_captures,before.transmission_captures+1);
}

TEST_F(Scene3dTest, TransmissionMapsOverlapEmptyEdgesResizeAndRecoveryRetainTheSharedCapture) {
    DomElement* background,*m=glass_stage(&background),*glass=m->parent->as_element();attr(m,"transmission","1");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t plain[128*128*4];memcpy(plain,image->pixels,sizeof(plain));
    DomElement* map=map_texture("glass-map","test/scene3d/assets/fidelity/layers.png");attr(map,"uv-set","1");attr(map,"uv-transform","0 -1 1 1 0 0");
    attr(m,"transmission-texture","glass-map");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_NE(memcmp(plain,image->pixels,sizeof(plain)),0);
    memcpy(plain,image->pixels,sizeof(plain));attr(m,"thickness-texture","glass-map");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_NE(memcmp(plain,image->pixels,sizeof(plain)),0);
    DomElement* second=mesh("box","#ffffff","physical"),*layer=second->last_child->as_element();attr(second,"position",".7 0 .4");attr(second,"scale",".3 .3 .3");attr(layer,"transmission",".8");
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.transmission_captures,before.transmission_captures+1);
    for(unsigned i=0;i<100;i++) ASSERT_NE(snapshot(i%2?128:96,i%2?128:96),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(after.graphics.resources,before.graphics.resources);EXPECT_EQ(after.graphics.gpu_bytes,before.graphics.gpu_bytes);
    scene3d_context_lost(root);image=snapshot();ASSERT_NE(image,nullptr);memcpy(plain,image->pixels,sizeof(plain));
    attr(camera,"fov","91");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(plain,image->pixels,sizeof(plain)),0);
    DomElement* post=element("postprocessing",root),*bloom=element("effect",post),*fxaa=element("effect",post);
    attr(bloom,"type","bloom");attr(bloom,"strength",".1");attr(bloom,"threshold",".5");attr(fxaa,"type","fxaa");
    ASSERT_TRUE(scene3d_stats(root,&before));ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.transmission_captures,before.transmission_captures+1);EXPECT_EQ(after.graphics.frames,before.graphics.frames+1);
    EXPECT_EQ(after.postprocess_passes,before.postprocess_passes+5);
    root->remove_child(post);doc.mutation_epoch++;image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(plain,image->pixels,sizeof(plain)),0);
    attr(background,"visible","false");attr(glass,"position","1.4 0 .5");ASSERT_NE(snapshot(),nullptr);
    attr(m,"alpha-mode","blend");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"coverage"),nullptr);
    attr(m,"alpha-mode","opaque");attr(m,"attenuation-distance","0");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"attenuation-distance"),nullptr);
    attr(m,"attenuation-distance","1");ASSERT_NE(snapshot(),nullptr);
}

TEST_F(Scene3dTest, TransmissionTracksReserveTheirProgramAndRejectInvalidAbsorption) {
    DomElement* m=glass_stage();attr(m,"id","glass");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","reveal");
    DomElement* factor=element("keyframe-track",clip);attr(factor,"path","glass.transmission");attr(factor,"type","number");attr(factor,"times","0 1");attr(factor,"values","0 1");
    DomElement* color=element("keyframe-track",clip);attr(color,"path","glass.attenuation-color");attr(color,"type","color");attr(color,"times","0 1");attr(color,"values","1 1 1 -1 1 1");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t opaque[128*128*4];memcpy(opaque,image->pixels,sizeof(opaque));
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    Scene3dAnimationState* state=scene3d_animations(root);ASSERT_NE(state,nullptr);auto* action=scene3d_animation_action(state,"reveal");ASSERT_NE(action,nullptr);action->loop=ANIMATION_LOOP_ONCE;action->clamp=true;ASSERT_TRUE(animation_action_play(action));
    ASSERT_TRUE(scene3d_animation_seek(state,.25));image=snapshot();ASSERT_NE(image,nullptr);EXPECT_NE(memcmp(opaque,image->pixels,sizeof(opaque)),0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.material_links,after.material_links);EXPECT_EQ(before.projection_generation,after.projection_generation);
    EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(after.transmission_captures,before.transmission_captures+1);
    ASSERT_TRUE(scene3d_animation_seek(state,1));EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"attenuation-color"),nullptr);
    ASSERT_TRUE(scene3d_animation_seek(state,0));image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(opaque,image->pixels,sizeof(opaque)),0);
}

TEST_F(Scene3dTest, PointsKeepLogicalSizesColorsAndVertexSizesAcrossCamerasAndDensity) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* cloud=drawable("points","-1 0 0 0 0 0 1 0 0","#ffffff"),*g=cloud->first_child->as_element();
    attr(cloud,"size","16");attr(g,"point-sizes",".5 1 1.5");attr(g,"colors","1 0 0 0 1 0 0 0 1");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,32,64,255,0,0);pixel(image,64,64,0,255,0);pixel(image,96,64,0,0,255);
    EXPECT_EQ(color_span(image,0,64),8u);EXPECT_EQ(color_span(image,1,64),16u);EXPECT_EQ(color_span(image,2,64),24u);pixel(image,35,67,0,0,0,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"position","0 0 8");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,0,64),8u);
    attr(camera,"type","perspective");attr(camera,"fov","60");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_NEAR(color_span(image,0,64),8,1);
    image=snapshot(128,128,2);ASSERT_NE(image,nullptr);EXPECT_NEAR(color_span(image,0,128),16,1);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(before.material_links,after.material_links);
    EXPECT_EQ(before.geometries,after.geometries);EXPECT_EQ(after.graphics.draws-before.graphics.draws,6u);
    uint64_t frames=after.graphics.frames;ASSERT_EQ(snapshot(128,128,2),image);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.graphics.frames,frames);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
    attr(cloud,"size","1");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_GT(color_span(image,1,64),0u);
}

TEST_F(Scene3dTest, SourceUvsDoNotChangePointCoverageAndLineCoordinatesInterpolate) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* cloud=drawable("points","0 0 0","#ffffff"),*g=cloud->first_child->as_element(),*m=cloud->last_child->as_element();
    attr(cloud,"size","24");attr(g,"drawable-uvs","source");attr(g,"uvs",".25 .75");
    DomElement* texture=map_texture("map","test/scene3d/assets/fidelity/colors.png");attr(texture,"uv-set","0");attr(m,"texture","map");attr(m,"alpha-mode","opaque");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,64),24u);pixel(image,64,64,255,0,0);
    pixel(image,54,54,0,0,0,0);pixel(image,64,55,255,0,0);
    root->remove_child(cloud);doc.mutation_epoch++;
    DomElement* path=drawable("line-segments","-1 0 0 1 0 0","#ffffff");g=path->first_child->as_element();m=path->last_child->as_element();
    attr(path,"width","12");attr(g,"drawable-uvs","source");attr(g,"uvs",".25 .75 .75 .75");attr(m,"texture","map");attr(m,"alpha-mode","opaque");
    image=snapshot();ASSERT_NE(image,nullptr);pixel(image,40,64,255,0,0);pixel(image,88,64,0,255,0);
    pixel(image,40,60,255,0,0);pixel(image,40,67,255,0,0);
}

TEST_F(Scene3dTest, SpritesFaceBothCamerasKeepAnchorsAndSampleGeneratedCoordinates) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* marker=drawable("sprite","0 0 0","#ffffff"),*m=marker->last_child->as_element();
    attr(marker,"size-mode","screen");attr(marker,"size","24 16");attr(marker,"center","0 0");
    DomElement* texture=map_texture("marker","test/scene3d/assets/fidelity/colors.png");attr(texture,"uv-set","0");attr(m,"texture","marker");attr(m,"alpha-mode","opaque");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,56),24u);EXPECT_EQ(color_span(image,3,76,true),16u);
    pixel(image,63,56,0,0,0,0);pixel(image,76,64,0,0,0,0);
    uint8_t front[128*128*4];memcpy(front,image->pixels,sizeof(front));Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"position","4 0 4");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(front,image->pixels,sizeof(front)),0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);
    attr(marker,"size-mode","world");attr(marker,"size","1 1");attr(marker,"center",".5 .5");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,64),32u);
    attr(camera,"position","0 0 4");attr(camera,"type","perspective");attr(camera,"fov","90");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,64),16u);
    attr(camera,"position","0 0 8");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,64),8u);
    attr(marker,"size-mode","screen");attr(marker,"size","24 16");image=snapshot(128,128,2);ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,128),48u);
    EXPECT_EQ(color_span(image,3,128,true),32u);
}

TEST_F(Scene3dTest, WideLinesKeepWidthClipBeforeExpansionAndInterpolateEndpointColors) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* path=drawable("line","-1 0 0 0 0 0 1 1 0"),*g=path->first_child->as_element(),*m=path->last_child->as_element();attr(path,"width","8");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,40,true),8u);pixel(image,40,59,0,0,0,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"type","perspective");attr(camera,"fov","60");attr(camera,"position","0 0 8");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,55,true),8u);
    image=snapshot(128,128,2);ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,110,true),16u);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(before.material_links,after.material_links);
    attr(camera,"position","0 0 0");attr(camera,"target","0 0 -1");attr(camera,"near","1");attr(camera,"far","10");attr(camera,"fov","90");
    attr(g,"positions","-.2 0 -.5 1 0 -3");attr(g,"colors","1 0 0 0 0 1");attr(m,"color","#ffffff");
    image=snapshot();ASSERT_NE(image,nullptr);pixel(image,63,64,0,0,0,0);
    float ndc=(68.5f-64)/64,t=(.2f+.5f*ndc)/(1.2f-2.5f*ndc);
    pixel(image,68,64,srgb_byte(1-t),0,srgb_byte(t),255,3);
    attr(g,"positions","0 0 -2 0 0 -2 0 0 -3");attr(g,"colors","");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(color_span(image,3,64),0u);
    attr(path,"width","0");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"positive"),nullptr);
}

TEST_F(Scene3dTest, MirroredPointInstancesStayInOneBatchAndSharedSourcesKeepTheirTopology) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* cloud=drawable("points","0 0 0"),*g=cloud->first_child->as_element();attr(cloud,"size","12");
    attr(cloud,"instances","1 0 0 0 0 1 0 0 0 0 1 0 -1 0 0 1 -1 0 0 0 0 1 0 0 0 0 1 0 1 0 0 1");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,32,64,255,0,0);pixel(image,96,64,255,0,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.meshes,1u);EXPECT_EQ(before.graphics.draws,2u);
    for(unsigned i=0;i<100;i++) ASSERT_NE(snapshot(i%2?128:96,i%2?128:96),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.graphics.resources,after.graphics.resources);EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);
    ASSERT_TRUE(cloud->remove_attribute("instances"));attr(g,"id","shared");attr(g,"positions","-.6 -.5 0 .6 -.5 0 0 .5 0");doc.mutation_epoch++;
    DomElement* triangle=element("mesh",root);attr(triangle,"geometry","shared");DomElement* material=element("material",triangle);attr(material,"type","basic");attr(material,"color","#0000ff");
    DomElement* unused=element("geometry",root);attr(unused,"type","buffer");attr(unused,"positions","0 0 0");
    ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.geometries,2u);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
}

TEST_F(Scene3dTest, ImportedFaceFreePointAndLinePrimitivesRenderAndRetireTheirUploads) {
    Input* owner=InputManager::create_input(nullptr);MarkBuilder builder(owner);
    Input* source=input_from_url(builder.createString("test/scene3d/assets/fidelity/drawables.gltf"),builder.createString("scene3d-asset"),nullptr,doc.url);
    ASSERT_NE(source,nullptr);ASSERT_FALSE(source->parse_failed)<<source->parse_error_message;
    DomElement* model=build_dom_tree_from_element(source->root.element,&doc,root);ASSERT_NE(model,nullptr);doc.mutation_epoch++;
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);unsigned covered=0;
    for(unsigned y=0;y<image->height;y++) for(unsigned x=0;x<image->width;x++) covered+=((uint8_t*)image->pixels)[y*image->pitch+x*4+3]>0;
    EXPECT_GT(covered,80u);Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.geometries,3u);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);
    root->remove_child(model);doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.geometries,0u);EXPECT_LT(after.graphics.gpu_bytes,before.graphics.gpu_bytes);
}

TEST(Scene3dMath, FrustumBoundsAndLodThresholdsAreConservativeAndStable) {
    auto lens=scene3d_perspective(90,1,1,10);
    EXPECT_TRUE(scene3d_clip_bounds(lens,{-1,-1,-3},{1,1,-2}));
    EXPECT_FALSE(scene3d_clip_bounds(lens,{5,0,-3},{6,1,-2}));
    EXPECT_FALSE(scene3d_clip_bounds(lens,{0,0,1},{1,1,2}));
    EXPECT_FALSE(scene3d_clip_bounds(lens,{0,0,-20},{1,1,-15}));
    EXPECT_TRUE(scene3d_clip_bounds(scene3d_perspective(90,1,1,INFINITY),{0,0,-1e10f},{1,1,-1e9f}));
    EXPECT_TRUE(scene3d_clip_bounds(lens,{NAN,0,0},{1,1,1}));
    EXPECT_TRUE(scene3d_clip_bounds(scene3d_orthographic(4,1,0,10),{2.125f,0,-2},{2.125f,0,-2},.125f,.125f));
    const float distances[]={0,10,30};
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,10.5,0,true),0u);
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,11.1,0,true),1u);
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,9.5,1,true),1u);
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,8.9,1,true),0u);
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,50,0,true),2u);
    EXPECT_EQ(scene3d_lod_level(distances,3,.1f,2,2,true),0u);
}

TEST_F(Scene3dTest, FrustumCullingPreservesPixelsDropsDrawsAndKeepsCameraResources) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    for(unsigned i=0;i<25;i++) {
        DomElement* object=mesh("plane","#ff0000",i==1?"standard":"basic");
        char position[40];snprintf(position,sizeof(position),"%u 0 0",i*4);attr(object,"position",position);
    }
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,64,64,255,0,0);
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));Scene3dStats on,off,after;ASSERT_TRUE(scene3d_stats(root,&on));
    EXPECT_EQ(on.graphics.draws,2u);EXPECT_EQ(on.culled_draws,24u);EXPECT_EQ(on.material_links,2u);
    attr(root,"frustum-culling","false");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    ASSERT_TRUE(scene3d_stats(root,&off));EXPECT_EQ(off.graphics.draws-on.graphics.draws,26u);
    attr(root,"frustum-culling","true");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&on));
    attr(camera,"position","4 0 4");attr(camera,"target","4 0 0");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(on.material_links,after.material_links);EXPECT_EQ(on.projection_generation,after.projection_generation);
    attr(camera,"target","0 0 0");
    for(unsigned i=0;i<100;i++) {attr(camera,"position",i%2?"0 0 4":".1 0 4");ASSERT_NE(snapshot(),nullptr);}
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(on.graphics.gpu_bytes,after.graphics.gpu_bytes);EXPECT_EQ(on.graphics.resources,after.graphics.resources);
    EXPECT_EQ(on.material_links,after.material_links);EXPECT_EQ(on.projection_generation,after.projection_generation);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);
}

TEST_F(Scene3dTest, FrustumBoundsIncludeWideEdgesAnchorsAndMirroredInstanceUnions) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* cloud=drawable("points","2.125 .5 0"),*line=drawable("line-segments","2.0625 -1 0 2.0625 0 0","#00ff00");
    attr(cloud,"size","16");attr(line,"width","8");
    DomElement* marker=drawable("sprite","-2.25 1 0","#0000ff");attr(marker,"size","1 .5");attr(marker,"center","0 .5");
    DomElement* batch=mesh();attr(batch,"instances","-1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 0 1 0 0 0 0 1 0 10 0 0 1");
    for(float density:{1.f,2.f}) {
        ImageSurface* image=snapshot(128,128,density);ASSERT_NE(image,nullptr);pixel(image,(unsigned)(127*density),(unsigned)(48*density),255,0,0);
        unsigned bytes=(unsigned)image->pitch*(unsigned)image->height;lam::Temp<uint8_t> expected((uint8_t*)mem_alloc(bytes,MEM_CAT_RENDER));memcpy(expected.get(),image->pixels,bytes);
        attr(root,"frustum-culling","false");image=snapshot(128,128,density);ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected.get(),image->pixels,bytes),0);
        attr(root,"frustum-culling","true");
    }
    attr(root,"viewBox","0 0 256 128");ASSERT_NE(snapshot(),nullptr);
    attr(camera,"type","perspective");attr(camera,"infinite-far","true");ASSERT_NE(snapshot(),nullptr);
    attr(cloud,"frustum-culling","false");attr(cloud,"position","100 0 0");ASSERT_NE(snapshot(),nullptr);
}

TEST_F(Scene3dTest, DeformedAndShaderGeometryUsesAuthoredEnvelopesOrStaysUnculled) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* object=drawable("mesh","-1 -1 0 1 -1 0 0 1 0"),*g=object->first_child->as_element(),*m=object->last_child->as_element();
    attr(object,"position","10 0 0");attr(object,"morph-weights","1");attr(g,"morph-positions","-10 0 0 -10 0 0 -10 0 0");
    pixel(snapshot(),64,64,255,0,0);Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.visibility_tests,0u);
    attr(object,"bounds","-11 -1 0 1 1 0");pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(g->remove_attribute("morph-positions"));ASSERT_TRUE(object->remove_attribute("morph-weights"));ASSERT_TRUE(object->remove_attribute("bounds"));doc.mutation_epoch++;
    attr(m,"type","shader");attr(m,"vertex","void main(){gl_Position=projection_matrix*view_matrix*model_matrix*vec4(position+vec3(-10,0,0),1);}");
    attr(m,"fragment","void main(){fragment_color=vec4(1,0,0,1);}");pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&before));attr(camera,"position","30 0 4");attr(camera,"target","30 0 0");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.visibility_tests,after.visibility_tests);EXPECT_EQ(after.graphics.draws-before.graphics.draws,2u);
    attr(object,"bounds","-11 -1 0 -9 1 0");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&before));
    attr(camera,"position","31 0 4");attr(camera,"target","31 0 0");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.culled_draws-before.culled_draws,1u);EXPECT_EQ(after.graphics.draws-before.graphics.draws,1u);
    attr(object,"bounds","1 0 0 -1 1 1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"bounds"),nullptr);
}

TEST_F(Scene3dTest, OffCameraCastersKeepTheirShadowWhenPresentationIsCulled) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* receiver=mesh("plane","#ffffff","lambert");attr(receiver->first_child->as_element(),"size","4 4 1");
    DomElement* caster=mesh("box","#ffffff","lambert");attr(caster->first_child->as_element(),"size",".5 .5 .5");attr(caster,"position","3 0 1.5");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","4 0 4");attr(sun,"cast-shadow","true");attr(sun,"shadow-size","256");attr(sun,"shadow-normal-bias","0");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t shadowed[128*128*4];memcpy(shadowed,image->pixels,sizeof(shadowed));
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_GE(before.culled_draws,1u);
    attr(root,"frustum-culling","false");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(shadowed,image->pixels,sizeof(shadowed)),0);
    attr(root,"frustum-culling","true");attr(caster,"cast-shadow","false");image=snapshot();ASSERT_NE(image,nullptr);
    unsigned changed=0;for(unsigned i=0;i<sizeof(shadowed);i++) changed+=((uint8_t*)image->pixels)[i]!=shadowed[i];EXPECT_GT(changed,100u);
    ASSERT_TRUE(scene3d_stats(root,&before));attr(camera,"position",".1 0 4");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.shadow_passes,after.shadow_passes);
    // a distant presentation may choose an empty level; its highest-detail caster still shadows.
    attr(camera,"position","0 0 4");attr(caster,"cast-shadow","true");
    DomElement* lod=element("lod",root);attr(lod,"distances","0 1");DomElement* high=element("group",lod);element("group",lod);
    ASSERT_TRUE(root->remove_child(caster));ASSERT_TRUE(high->append_child(caster));doc.mutation_epoch++;
    image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(shadowed,image->pixels,sizeof(shadowed)),0);
    ASSERT_TRUE(root->remove_child(lod));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.lod_state_bytes,0u);
}

TEST_F(Scene3dTest, LodSwitchesBothWaysWithHysteresisAndRetainsEveryLevel) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* lod=element("lod",root);attr(lod,"distances","0 10 30");
    const char* tags[]={"mesh","sprite","points"},*colors[]={"#ff0000","#00ff00","#0000ff"};
    for(unsigned level=0;level<3;level++) {
        DomElement* group=element("group",lod),*object=mesh(level?"buffer":"plane",colors[level],"basic",group,tags[level]);
        if(level) {attr(object->first_child->as_element(),"positions","0 0 0");attr(object,"size-mode","screen");attr(object,"size",level==1?"32 32":"24");}
    }
    pixel(snapshot(),64,64,255,0,0);Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.geometries,3u);EXPECT_EQ(before.lod_skips,2u);
    const char* positions[]={"0 0 10.5","0 0 11.1","0 0 10.1","0 0 9.5","0 0 8.9","0 0 40","0 0 28","0 0 26","0 0 4"};
    const unsigned levels[]={0,1,1,1,0,2,2,1,0};
    for(unsigned i=0;i<9;i++) {attr(camera,"position",positions[i]);pixel(snapshot(),64,64,levels[i]==0?255:0,levels[i]==1?255:0,levels[i]==2?255:0);}
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(before.material_links,after.material_links);
    EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);EXPECT_EQ(after.graphics.draws-before.graphics.draws,18u);
    for(unsigned i=0;i<100;i++) {attr(camera,"position",i%2?"0 0 10.2":"0 0 9.8");pixel(snapshot(),64,64,255,0,0);}
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.material_links,after.material_links);EXPECT_EQ(before.graphics.resources,after.graphics.resources);
    scene3d_context_lost(root);pixel(snapshot(),64,64,255,0,0);
    attr(root,"frustum-culling","false");pixel(snapshot(),64,64,255,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.lod_state_bytes,after.lod_state_bytes);
    attr(lod,"distances","0 10 10");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"increase"),nullptr);
}

TEST_F(Scene3dTest, NestedLodKeepsInactiveAnimationAndRetiresItsSelectionState) {
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    DomElement* lod=element("lod",root);attr(lod,"distances","0 10");
    DomElement* near=element("group",lod);mesh("plane","#ff0000","basic",near);
    DomElement* far=element("group",lod);attr(far,"id","far-level");
    DomElement* nested=element("lod",far);attr(nested,"distances","0");DomElement* level=element("group",nested);
    DomElement* green=mesh("plane","#00ff00","basic",level);attr(green->first_child->as_element(),"size","1 1 1");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","move-hidden");DomElement* track=element("keyframe-track",clip);
    attr(track,"path","far-level.position");attr(track,"type","vector");attr(track,"times","0 1");attr(track,"values","0 0 0 1 0 0");
    pixel(snapshot(),64,64,255,0,0);auto* state=scene3d_animations(root);auto* action=scene3d_animation_action(state,"move-hidden");ASSERT_NE(action,nullptr);
    action->loop=ANIMATION_LOOP_ONCE;action->clamp=true;ASSERT_TRUE(animation_action_play(action));ASSERT_TRUE(scene3d_animation_seek(state,1));pixel(snapshot(),64,64,255,0,0);
    Scene3dStats before,after;ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.lods,2u);
    attr(camera,"position","0 0 12");ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,96,64,0,255,0);pixel(image,64,64,0,0,0,0);
    ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(before.projection_generation,after.projection_generation);EXPECT_EQ(before.graphics.gpu_bytes,after.graphics.gpu_bytes);
    ASSERT_TRUE(root->remove_child(lod));doc.mutation_epoch++;
    // retire the disconnected binding before validating the reduced scene.
    ASSERT_TRUE(root->remove_child(clip));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.lods,0u);EXPECT_EQ(after.lod_state_bytes,0u);
}

TEST_F(Scene3dTest, BufferAccountingCountsCpuCopiesOnlyWhenTheyAreRetained) {
    char diagnostic[256];auto* graphics=native_gl_create(true,diagnostic,sizeof(diagnostic));ASSERT_NE(graphics,nullptr)<<diagnostic;
    NativeGlStats base,upload,copied,retired;ASSERT_TRUE(native_gl_stats(graphics,&base));const float values[]={0,1,2,3};
    auto vertex=native_gl_buffer(graphics,values,sizeof(values),false);ASSERT_NE(vertex.id,0u);ASSERT_TRUE(native_gl_stats(graphics,&upload));
    EXPECT_EQ(upload.gpu_bytes-base.gpu_bytes,sizeof(values));EXPECT_EQ(upload.cpu_bytes,base.cpu_bytes);
    auto index=native_gl_buffer(graphics,values,sizeof(values),true);ASSERT_NE(index.id,0u);ASSERT_TRUE(native_gl_stats(graphics,&copied));
    EXPECT_EQ(copied.gpu_bytes-upload.gpu_bytes,sizeof(values));EXPECT_EQ(copied.cpu_bytes-upload.cpu_bytes,sizeof(values));
    native_gl_release(graphics,vertex);native_gl_release(graphics,index);ASSERT_TRUE(native_gl_stats(graphics,&retired));
    EXPECT_EQ(retired.gpu_bytes,base.gpu_bytes);EXPECT_EQ(retired.cpu_bytes,base.cpu_bytes);EXPECT_EQ(retired.resources,base.resources);native_gl_destroy(graphics);
}

TEST_F(Scene3dTest, OutdoorFogUsesLinearDistanceAndPreservesBackgroundAndAlpha) {
    attr(root,"background","#192733");DomElement* plane=mesh();
    DomElement* fog=element("fog",root);attr(fog,"type","linear");attr(fog,"color","#0000ff");attr(fog,"near","2");attr(fog,"far","6");
    pixel(snapshot(),64,64,188,0,188);pixel(snapshot(),0,0,25,39,51);
    DomElement* material=plane->last_child->as_element();attr(material,"opacity",".5");attr(root,"background","transparent");
    pixel(snapshot(),64,64,94,0,94,128,4);
    attr(fog,"far","1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"invalid fog"),nullptr);
    attr(fog,"far","6");ASSERT_NE(snapshot(),nullptr);
    DomElement* extra=element("fog",root);attr(extra,"type","linear");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"one leaf fog"),nullptr);
}

TEST_F(Scene3dTest, OutdoorFogCoversAllBuiltInMaterialsAndHeightDensity) {
    DomElement* plane=mesh(),*material=plane->last_child->as_element();
    DomElement* fog=element("fog",root);attr(fog,"type","exp2");attr(fog,"color","#0000ff");attr(fog,"density","10");
    for(const char* kind:{"basic","lambert","standard","physical"}) {
        attr(material,"type",kind);pixel(snapshot(),64,64,0,0,255);
    }
    attr(material,"type","basic");attr(material,"color","#ffffff");
    attr(camera,"type","orthographic");attr(camera,"view_height","4");
    attr(plane,"scale",".4 .4 1");attr(plane,"position","0 .7 0");
    DomElement* lower=mesh("plane","#ffffff");attr(lower,"scale",".4 .4 1");attr(lower,"position","0 -.7 0");
    attr(fog,"type","height");attr(fog,"color","#000000");attr(fog,"density",".3");attr(fog,"falloff","2");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);
    auto* high=(const uint8_t*)image->pixels+42*image->pitch+64*4;
    auto* low=(const uint8_t*)image->pixels+86*image->pitch+64*4;
    EXPECT_GT(high[0],low[0]+35);
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    scene3d_context_lost(root);image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
}

TEST_F(Scene3dTest, OutdoorFoliageBacklightHonorsOccludingShadows) {
    DomElement* leaf=buffer_quad("standard"),*material=leaf->last_child->as_element();
    attr(material,"color","#00ff00");attr(material,"side","double");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","0 0 -3");attr(sun,"cast-shadow","true");attr(sun,"shadow-map-size","512");
    pixel(snapshot(),64,64,0,0,0);
    attr(material,"foliage","1");attr(material,"foliage-thickness","0");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);EXPECT_GT(((const uint8_t*)image->pixels)[64*image->pitch+64*4+1],200);
    DomElement* caster=mesh("box");attr(caster,"position","0 0 -1");attr(caster->first_child->as_element(),"size",".8 .8 .3");
    pixel(snapshot(),64,64,0,0,0);
    attr(caster,"cast-shadow","false");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_GT(((const uint8_t*)image->pixels)[64*image->pitch+64*4+1],200);
    attr(material,"foliage","1.1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
}

TEST_F(Scene3dTest, OutdoorWindSharesVisibleAndShadowDeformationAndRestoresExactly) {
    mesh("plane","#ff0000","lambert");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","3 0 3");attr(sun,"cast-shadow","true");attr(sun,"shadow-map-size","512");
    DomElement* leaf=buffer_quad("standard"),*geometry=leaf->first_child->as_element(),*material=leaf->last_child->as_element();
    attr(geometry,"positions","-.2 0 0 .2 0 0 .2 1 0 -.2 1 0");attr(leaf,"position",".75 -.5 .75");
    attr(material,"color","#00ff00");attr(material,"side","double");attr(material,"wind-strength",".8");attr(material,"wind-height","1");attr(material,"wind-frequency","0");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    Scene3dStats initial={};ASSERT_TRUE(scene3d_stats(root,&initial));
    attr(material,"wind-phase","1.5707963");image=snapshot();ASSERT_NE(image,nullptr);
    unsigned changed=0;for(unsigned i=0;i<sizeof(expected);i+=4) if(memcmp(expected+i,(const uint8_t*)image->pixels+i,4)) changed++;
    EXPECT_GT(changed,50u);Scene3dStats moved={};ASSERT_TRUE(scene3d_stats(root,&moved));EXPECT_EQ(moved.shadow_passes,initial.shadow_passes+1);
    attr(material,"wind-phase","0");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    scene3d_context_lost(root);image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
}

TEST_F(Scene3dTest, OutdoorCascadesShareSlotsRetainCameraResourcesAndRecover) {
    mesh("plane","#ff0000","lambert");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","3 0 3");attr(sun,"cast-shadow","true");
    attr(sun,"shadow-cascades","3");attr(sun,"shadow-distance","10");attr(sun,"shadow-map-size","512");
    DomElement* caster=mesh("box","#00ff00","lambert");attr(caster->first_child->as_element(),"size",".3 .3 .3");attr(caster,"position",".75 0 .75");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);pixel(image,64,64,0,0,0);pixel(image,52,64,218,0,0);
    uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    Scene3dStats before={};ASSERT_TRUE(scene3d_stats(root,&before));EXPECT_EQ(before.shadow_passes,3u);
    attr(camera,"fov","80");ASSERT_NE(snapshot(),nullptr);Scene3dStats after={};ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.projection_generation,before.projection_generation);EXPECT_EQ(after.graphics.gpu_bytes,before.graphics.gpu_bytes);EXPECT_GT(after.shadow_passes,before.shadow_passes);
    attr(camera,"fov","90");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    scene3d_context_lost(root);image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    DomElement* extra=element("light",root);attr(extra,"type","directional");attr(extra,"position","0 1 1");attr(extra,"cast-shadow","true");attr(extra,"shadow-cascades","2");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"shadow light quota"),nullptr);
    attr(extra,"cast-shadow","false");attr(sun,"type","point");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"directional shadow cascades"),nullptr);
}

TEST_F(Scene3dTest, OutdoorAnimatedWindRetainsGeometryWhileMovingItsShadow) {
    mesh("plane","#ff0000","lambert");
    DomElement* sun=element("light",root);attr(sun,"type","directional");attr(sun,"position","3 0 3");attr(sun,"cast-shadow","true");attr(sun,"shadow-map-size","512");
    DomElement* leaf=buffer_quad("standard"),*geometry=leaf->first_child->as_element(),*material=leaf->last_child->as_element();
    attr(geometry,"positions","-.2 0 0 .2 0 0 .2 1 0 -.2 1 0");attr(leaf,"position",".75 -.5 .75");attr(material,"id","leaf");
    attr(material,"color","#00ff00");attr(material,"side","double");attr(material,"wind-strength",".8");attr(material,"wind-frequency","0");
    DomElement* clip=element("animation-clip",root);attr(clip,"id","wind");
    DomElement* track=element("keyframe-track",clip);attr(track,"path","leaf.wind-phase");attr(track,"type","number");attr(track,"times","0 1");attr(track,"values","0 1.5707963");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint64_t original=hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height);
    Scene3dStats before={};ASSERT_TRUE(scene3d_stats(root,&before));auto* state=scene3d_animations(root);auto* action=scene3d_animation_action(state,"wind");ASSERT_NE(action,nullptr);
    ASSERT_TRUE(animation_action_play(action));ASSERT_TRUE(scene3d_animation_seek(state,.8));image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_NE(hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height),original);Scene3dStats after={};ASSERT_TRUE(scene3d_stats(root,&after));
    EXPECT_EQ(after.projection_generation,before.projection_generation);EXPECT_EQ(after.graphics.gpu_bytes,before.graphics.gpu_bytes);EXPECT_GT(after.shadow_passes,before.shadow_passes);
    ASSERT_TRUE(animation_action_stop(action));ASSERT_TRUE(scene3d_animation_update(state,0));image=snapshot();ASSERT_NE(image,nullptr);
    EXPECT_EQ(hash_fnv1a_64(image->pixels,(size_t)image->pitch*image->height),original);
}

TEST_F(Scene3dTest, OutdoorWaterRipplesRetainOpaqueCaptureAndRejectInvalidFactors) {
    DomElement* material=glass_stage();attr(material,"transmission","1");attr(material,"ior","1.333");
    attr(material,"water-amplitude",".15");attr(material,"water-frequency","4");attr(material,"water-phase","0");
    ImageSurface* image=snapshot();ASSERT_NE(image,nullptr);uint8_t expected[128*128*4];memcpy(expected,image->pixels,sizeof(expected));
    attr(material,"water-phase","1.2");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_NE(memcmp(expected,image->pixels,sizeof(expected)),0);
    attr(material,"water-phase","0");image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(root,&stats));EXPECT_EQ(stats.simulation_steps,0u);EXPECT_EQ(stats.transmission_captures,3u);
    scene3d_context_lost(root);image=snapshot();ASSERT_NE(image,nullptr);EXPECT_EQ(memcmp(expected,image->pixels,sizeof(expected)),0);
    attr(material,"water-amplitude","-1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
}

TEST_F(Scene3dTest, SurfaceEditsKeepSleepingBodiesAndRejectInvalidGeometryAtomically) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    auto* fixed=element("rigid-body",root);attr(fixed,"id","ground");attr(fixed,"type","fixed");
    auto* surface=element("collider",fixed);attr(surface,"id","ground-shape");attr(surface,"shape","mesh");
    attr(surface,"vertices","-2 0 -2 2 0 -2 -2 0 2 2 0 2");attr(surface,"indices","0 2 1 1 2 3");
    auto* body=element("rigid-body",root);attr(body,"id","ball");attr(body,"type","dynamic");attr(body,"position","0 2 0");
    auto* collider=element("collider",body);attr(collider,"id","ball-shape");attr(collider,"shape","sphere");mesh("box","#ff0000","basic",body);
    ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState result={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,100,&result,&error));ASSERT_TRUE(result.bodies[1].sleeping);
    attr(camera,"fov","60");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));
    EXPECT_TRUE(result.bodies[1].sleeping);EXPECT_EQ(result.tick,100u);
    attr(surface,"indices","0 2 99");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_EQ(result.tick,100u);EXPECT_TRUE(result.bodies[1].sleeping);
    attr(surface,"indices","0 2 1 1 2 3");attr(surface,"vertices","-2 1 -2 2 1 -2 -2 1 2 2 1 2");ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_EQ(result.tick,100u);EXPECT_DOUBLE_EQ(result.bodies[1].pose.position.y,2);
    auto* grid_body=element("rigid-body",root);attr(grid_body,"id","grid");attr(grid_body,"type","fixed");
    auto* grid=element("collider",grid_body);attr(grid,"id","grid-shape");attr(grid,"shape","heightfield");
    attr(grid,"rows","2");attr(grid,"columns","2");attr(grid,"heights","0 0 0 1");attr(grid,"cell-size","1 1");ASSERT_NE(snapshot(),nullptr);
    // adding an owner rebuilds; subsequent height-only edits retain that world's tick and runtime.
    physics=scene3d_physics(root);runtime=scene3d_runtime(root);ASSERT_TRUE(scene3d_physics_advance(physics,10,&result,&error));
    attr(grid,"heights","0 0 0 2");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&result,&error));EXPECT_EQ(result.tick,10u);
}

TEST_F(Scene3dTest, ReferencedHullEditsRetainSharedWorldMotionAndRejectBadResources) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    DomElement* bodies[2];DomElement* colliders[2];
    for(unsigned i=0;i<2;i++) {
        bodies[i]=element("rigid-body",root);attr(bodies[i],"id",i?"right":"left");attr(bodies[i],"type","dynamic");
        attr(bodies[i],"position",i?"2 0 0":"-2 0 0");
        colliders[i]=element("collider",bodies[i]);attr(colliders[i],"id",i?"right-shape":"left-shape");
        attr(colliders[i],"shape","convex");attr(colliders[i],"geometry","shared-hull");mesh("box","#ff9933","basic",bodies[i]);
    }
    // definitions follow the bodies; one shared authoring edit must reach both owned colliders.
    auto* resources=element("resources",root);auto* hull=element("collision-geometry",resources);
    attr(hull,"id","shared-hull");attr(hull,"version","1");attr(hull,"shape","convex");
    const char* cube="-.5 -.5 -.5 .5 -.5 -.5 -.5 .5 -.5 .5 .5 -.5 -.5 -.5 .5 .5 -.5 .5 -.5 .5 .5 .5 .5 .5";
    attr(hull,"vertices",cube);ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,100,&state,&error));ASSERT_EQ(state.body_count,2u);
    for(size_t i=0;i<2;i++){EXPECT_TRUE(state.bodies[i].sleeping);EXPECT_NEAR(state.bodies[i].mass,1,1e-12);}
    attr(camera,"fov","60");attr(hull,"vertices",cube);ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,100u);
    for(size_t i=0;i<2;i++)EXPECT_TRUE(state.bodies[i].sleeping);
    attr(hull,"version","2");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"version 1"),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,100u);
    for(size_t i=0;i<2;i++){EXPECT_TRUE(state.bodies[i].sleeping);EXPECT_NEAR(state.bodies[i].mass,1,1e-12);}
    attr(hull,"version","1");attr(colliders[0],"geometry","missing");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    attr(colliders[0],"geometry","shared-hull");attr(hull,"vertices","0 0 0 1 0 0 0 1 0 1 1 0");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    attr(hull,"vertices","-1 -1 -1 1 -1 -1 -1 1 -1 1 1 -1 -1 -1 1 1 -1 1 -1 1 1 1 1 1");
    attr(bodies[0],"linear-velocity","1 0 0");ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&state,&error));EXPECT_EQ(state.tick,110u);
    for(size_t i=0;i<2;i++)EXPECT_NEAR(state.bodies[i].mass,8,1e-12);
    EXPECT_NEAR(state.bodies[0].pose.position.x,-1.9,1e-12);EXPECT_DOUBLE_EQ(state.bodies[0].velocity.x,1);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,2);
}

TEST_F(Scene3dTest, CollisionResourceInstancesKeepTheExistingWorldStorageQuota) {
    element("physics",root);auto* resources=element("resources",root);auto* grid=element("collision-geometry",resources);
    attr(grid,"id","grid-data");attr(grid,"version","1");attr(grid,"shape","heightfield");
    attr(grid,"rows","2");attr(grid,"columns","2");attr(grid,"heights","0 0 0 0");
    const char* owners[]={"one","two","three"};const char* shapes[]={"shape-one","shape-two","shape-three"};
    for(unsigned i=0;i<3;i++) {
        auto* body=element("rigid-body",root);attr(body,"id",owners[i]);attr(body,"type","fixed");
        auto* collider=element("collider",body);attr(collider,"id",shapes[i]);attr(collider,"shape","heightfield");attr(collider,"geometry","grid-data");
    }
    ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,3,&state,&error));
    StrBuf* heights=strbuf_new();ASSERT_NE(heights,nullptr);
    for(unsigned i=0;i<PHYSICS_WORLD_MAX_VERTICES/2;i++)strbuf_append_str(heights,"0 ");
    attr(grid,"rows","128");attr(grid,"columns","256");attr(grid,"heights",heights->str);strbuf_free(heights);
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,3u);
    attr(grid,"rows","2");attr(grid,"columns","2");attr(grid,"heights","0 0 0 0");ASSERT_NE(snapshot(),nullptr);
    EXPECT_EQ(scene3d_physics(root),physics);ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,3u);
}

TEST_F(Scene3dTest, JointEditsRetainMotionAndRejectInvalidFramesWithoutReplacingTheWorld) {
    auto* config=element("physics",root);attr(config,"gravity","0 -10 0");attr(config,"timestep",".01");
    auto* joint=element("joint",root);attr(joint,"id","hinge");attr(joint,"type","hinge");attr(joint,"body-a","anchor");attr(joint,"body-b","bob");
    attr(joint,"anchor-b","0 1 0");attr(joint,"limits","true");attr(joint,"lower-limit","-.3");attr(joint,"upper-limit",".3");
    attr(joint,"motor","true");attr(joint,"motor-speed","1");attr(joint,"motor-max-force","1");
    auto* anchor=element("rigid-body",root);attr(anchor,"id","anchor");attr(anchor,"type","fixed");attr(anchor,"position","0 1.5 0");
    auto* bob=element("rigid-body",root);attr(bob,"id","bob");attr(bob,"type","dynamic");attr(bob,"position","0 .5 0");
    auto* shape=element("collider",bob);attr(shape,"id","shape");attr(shape,"shape","sphere");attr(shape,"radius",".2");mesh("box","#ff9933","basic",bob);
    ASSERT_NE(snapshot(),nullptr);auto* world=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(world,80,&state,&error));ASSERT_EQ(state.joint_count,1u);EXPECT_NEAR(state.joints[0].coordinate,.3,.005);
    attr(joint,"motor-speed","-1");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),world);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(world,80,&state,&error));EXPECT_EQ(state.tick,160u);EXPECT_NEAR(state.joints[0].coordinate,-.3,.005);
    double angle=state.joints[0].coordinate;attr(joint,"lower-limit","1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    EXPECT_NE(strstr(scene3d_diagnostic(root),"joint"),nullptr);EXPECT_EQ(scene3d_physics(root),world);
    ASSERT_TRUE(scene3d_physics_advance(world,0,&state,&error));EXPECT_EQ(state.tick,160u);EXPECT_DOUBLE_EQ(state.joints[0].coordinate,angle);
    attr(joint,"lower-limit","-.3");ASSERT_NE(snapshot(),nullptr);
    auto* extra=element("joint",root);attr(extra,"id","inactive");attr(extra,"type","spherical");attr(extra,"body-a","anchor");attr(extra,"body-b","bob");attr(extra,"enabled","false");
    ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),world);ASSERT_TRUE(scene3d_physics_advance(world,0,&state,&error));
    ASSERT_EQ(state.joint_count,2u);EXPECT_EQ(state.tick,160u);EXPECT_DOUBLE_EQ(state.joints[0].coordinate,angle);EXPECT_FALSE(state.joints[1].enabled);
}
TEST_F(Scene3dTest, ReentrantJointEditRejectsBeforeAnyWorldOrSnapshotMutation) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");
    auto* joint=element("joint",root);attr(joint,"id","hinge");attr(joint,"type","hinge");attr(joint,"body-a","anchor");attr(joint,"body-b","bob");
    attr(joint,"motor","true");attr(joint,"motor-speed","1");attr(joint,"motor-max-force","1");
    auto* anchor=element("rigid-body",root);attr(anchor,"id","anchor");attr(anchor,"type","fixed");
    auto* bob=element("rigid-body",root);attr(bob,"id","bob");attr(bob,"type","dynamic");
    auto* shape=element("collider",bob);attr(shape,"id","shape");attr(shape,"shape","sphere");
    struct Owner {
        Scene3dRuntime* runtime=nullptr;Scene3dPhysics* world=nullptr;
        ~Owner(){scene3d_physics_destroy(world);scene3d_runtime_destroy(runtime);}
    } owner;
    owner.runtime=scene3d_runtime_create(root);ASSERT_NE(owner.runtime,nullptr);char diagnostic[256]={};
    owner.world=scene3d_physics_create(root,diagnostic,sizeof(diagnostic),false,owner.runtime);ASSERT_NE(owner.world,nullptr)<<diagnostic;
    using Fixture = decltype(this);
    struct Probe {Fixture fixture;DomElement* joint;Scene3dPhysics* world;bool seen;Scene3dPhysicsRefresh result;};
    Probe probe={this,joint,owner.world,false,SCENE3D_PHYSICS_KEEP};
    ASSERT_TRUE(scene3d_runtime_bind(owner.runtime,SCENE3D_TICK_ANIMATION,&probe,[](void* opaque,double){
        auto* p=(Probe*)opaque;if(!p->seen){p->fixture->attr(p->joint,"motor-speed","-1");char message[256]={};
            p->result=scene3d_physics_refresh(p->world,p->fixture->root,message,sizeof(message));p->seen=true;}
        return SCENE3D_TICK_ACTIVE;
    }));
    ASSERT_TRUE(scene3d_runtime_step(owner.runtime,1));EXPECT_TRUE(probe.seen);EXPECT_EQ(probe.result,SCENE3D_PHYSICS_REJECT);
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(owner.world,0,&state,&error));
    ASSERT_EQ(state.joint_count,1u);EXPECT_GT(state.joints[0].coordinate,0);
    attr(joint,"motor-speed","1");EXPECT_EQ(scene3d_physics_refresh(owner.world,root,diagnostic,sizeof(diagnostic)),SCENE3D_PHYSICS_KEEP);
    scene3d_runtime_unbind(owner.runtime,SCENE3D_TICK_ANIMATION,&probe);
    ASSERT_TRUE(scene3d_physics_advance(owner.world,5,&state,&error));EXPECT_GT(state.joints[0].coordinate,0);
}
TEST_F(Scene3dTest, AuthoredMotionRolesRetainWorldClockAndRejectInvalidTransitions) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"timestep",".01");attr(config,"state","paused");
    auto* body=element("rigid-body",root);attr(body,"id","moving");attr(body,"type","kinematic-velocity");attr(body,"linear-velocity","1 0 0");
    auto* shape=element("collider",body);attr(shape,"id","shape");attr(shape,"shape","box");mesh("box","#ff9933","basic",body);
    ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.1,1e-12);
    attr(body,"type","dynamic");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&state,&error));EXPECT_EQ(state.tick,20u);EXPECT_NEAR(state.bodies[0].pose.position.x,.2,1e-12);
    attr(body,"type","fixed");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,20u);EXPECT_DOUBLE_EQ(state.bodies[0].velocity.x,1);
    attr(body,"linear-velocity","0 0 0");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    ASSERT_TRUE(scene3d_physics_advance(physics,10,&state,&error));EXPECT_EQ(state.tick,30u);EXPECT_NEAR(state.bodies[0].pose.position.x,.2,1e-12);
    EXPECT_DOUBLE_EQ(plen(state.bodies[0].velocity),0);
    attr(body,"type","dynamic");attr(config,"gravity","0 -10 0");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_EQ(state.tick,31u);EXPECT_NEAR(state.bodies[0].pose.position.y,-.001,1e-12);
}
TEST_F(Scene3dTest, SharedPoseSamplingAppliesOverridesBeforeChildTransformsAndKeepsBindPosesAuthored) {
    auto* parent=element("group",root);attr(parent,"position","1 2 3");attr(parent,"rotation","0 0 1.5707963267948966");
    auto* bone=element("bone",parent);attr(bone,"position","1 0 0");Scene3dMatrix world={};bool visible=false;
    ASSERT_TRUE(scene3d_world_pose(root,bone,nullptr,nullptr,nullptr,&world,&visible));
    EXPECT_NEAR(world.v[12],1,1e-6);EXPECT_NEAR(world.v[13],3,1e-6);EXPECT_NEAR(world.v[14],3,1e-6);EXPECT_TRUE(visible);
    struct Override {DomElement* node;Scene3dMatrix pose;};Override override={parent,scene3d_transform({10,20,30},{},{1,1,1})};
    auto replace=[](void* owner,DomElement* node,Scene3dMatrix* matrix){auto* selected=(Override*)owner;
        if(node!=selected->node)return false;*matrix=selected->pose;return true;};
    ASSERT_TRUE(scene3d_world_pose(root,bone,nullptr,replace,&override,&world,&visible));
    EXPECT_FLOAT_EQ(world.v[12],11);EXPECT_FLOAT_EQ(world.v[13],20);EXPECT_FLOAT_EQ(world.v[14],30);
    ASSERT_TRUE(scene3d_world_pose(root,bone,nullptr,replace,&override,&world,&visible,true));
    EXPECT_NEAR(world.v[12],1,1e-6);EXPECT_NEAR(world.v[13],3,1e-6);
    attr(parent,"visible","false");ASSERT_TRUE(scene3d_world_pose(root,bone,nullptr,replace,&override,&world,&visible));EXPECT_FALSE(visible);
    auto* outside=element("bone");Scene3dMatrix retained=world;
    EXPECT_FALSE(scene3d_world_pose(root,outside,nullptr,replace,&override,&world,&visible));EXPECT_EQ(memcmp(world.v,retained.v,sizeof(world.v)),0);
}
TEST_F(Scene3dTest, PhysicalBonesUseBindOffsetsAndTheExistingSkinPalette) {
    attr(root,"background","#000000");attr(camera,"type","orthographic");attr(camera,"view_height","4");
    attr(camera,"position","0 1 4");attr(camera,"target","0 1 0");
    auto* config=element("physics",root);attr(config,"gravity","0 -10 0");attr(config,"timestep",".05");attr(config,"state","paused");
    auto* rig=element("skeleton",root);attr(rig,"id","rig");auto* base=element("bone",rig);attr(base,"id","base-bone");attr(base,"position","0 1 0");
    auto* tip=element("bone",base);attr(tip,"id","tip-bone");attr(tip,"position","0 1 0");
    attr(base,"physics-body","base-body");attr(tip,"physics-body","tip-body");attr(base,"physics-offset","0 .5 0");attr(tip,"physics-offset","0 .5 0");
    attr(base,"inverse-bind-matrix","1 0 0 0 0 1 0 0 0 0 1 0 0 -1 0 1");
    attr(tip,"inverse-bind-matrix","1 0 0 0 0 1 0 0 0 0 1 0 0 -2 0 1");
    auto* ribbon=mesh("buffer","#ff9933");attr(ribbon,"skeleton","rig");auto* geometry=ribbon->first_child->as_element();
    attr(geometry,"positions","-.2 1 0 .2 1 0 .2 2 0 -.2 2 0");attr(geometry,"indices","0 1 2 0 2 3");
    attr(geometry,"skin-indices","0 0 0 0 0 0 0 0 1 0 0 0 1 0 0 0");attr(geometry,"skin-weights","1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0");
    auto* joint=element("joint",root);attr(joint,"id","link");attr(joint,"type","distance");attr(joint,"body-a","base-body");attr(joint,"body-b","tip-body");attr(joint,"length","1");
    for(unsigned i=0;i<2;i++) {
        auto* body=element("rigid-body",root);attr(body,"id",i?"tip-body":"base-body");attr(body,"type","dynamic");attr(body,"position",i?"0 2.5 0":"0 1.5 0");
        auto* shape=element("collider",body);attr(shape,"id",i?"tip-shape":"base-shape");attr(shape,"shape","sphere");attr(shape,"radius",".1");
    }
    pixel(snapshot(),64,48,255,153,51);auto* world=scene3d_physics(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(world,10,&state,&error));EXPECT_EQ(state.tick,10u);EXPECT_NEAR(state.bodies[0].pose.position.y,.125,1e-10);
    Scene3dMatrix bone_pose={};ASSERT_TRUE(scene3d_physics_pose(world,base,&bone_pose));EXPECT_NEAR(bone_pose.v[13],-.375,1e-6);
    ASSERT_TRUE(scene3d_physics_pose(world,tip,&bone_pose));EXPECT_NEAR(bone_pose.v[13],.625,1e-6);
    ImageSurface* fallen=snapshot();pixel(fallen,64,92,255,153,51);pixel(fallen,64,48,0,0,0);
    uint8_t expected[128*128*4];memcpy(expected,fallen->pixels,sizeof(expected));
    // an independent authored-pose control uses the same immutable bind matrices and no physical bone overlay.
    attr(base,"physics-enabled","false");attr(tip,"physics-enabled","false");attr(base,"position","0 -.375 0");
    ImageSurface* control=snapshot();ASSERT_NE(control,nullptr);EXPECT_EQ(memcmp(expected,control->pixels,sizeof(expected)),0);
    EXPECT_EQ(scene3d_physics(root),world);ASSERT_TRUE(scene3d_physics_advance(world,0,&state,&error));EXPECT_EQ(state.tick,10u);
    attr(base,"physics-body","tip-body");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),world);
    ASSERT_TRUE(scene3d_physics_advance(world,0,&state,&error));EXPECT_EQ(state.tick,10u);
}
TEST_F(Scene3dTest, PhysicalBoneFramesRoundTripRotatedOffsetsAndOwnTheRigidPose) {
    auto* config=element("physics",root);attr(config,"gravity","0 0 0");attr(config,"state","paused");
    auto* rig=element("skeleton",root);attr(rig,"id","rig");auto* bone=element("bone",rig);attr(bone,"id","bone");
    attr(bone,"physics-body","body");attr(bone,"physics-offset",".5 .1 -.2");attr(bone,"physics-rotation",".2 .3 .4");
    auto* body=element("rigid-body",root);attr(body,"id","body");attr(body,"type","dynamic");attr(body,"position","2 3 4");attr(body,"rotation",".4 -.3 .7");
    auto* shape=element("collider",body);attr(shape,"id","shape");attr(shape,"shape","box");ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);Scene3dMatrix bone_pose={},body_pose={};ASSERT_TRUE(scene3d_physics_pose(physics,bone,&bone_pose));
    ASSERT_TRUE(scene3d_physics_pose(physics,body,&body_pose));Scene3dMatrix offset=scene3d_transform({.5f,.1f,-.2f},{.2f,.3f,.4f},{1,1,1});
    Scene3dMatrix reconstructed=scene3d_multiply(bone_pose,offset);for(unsigned i=0;i<16;i++)EXPECT_NEAR(reconstructed.v[i],body_pose.v[i],2e-6);
    auto* clip=element("animation-clip",root);attr(clip,"id","scale");attr(clip,"autoplay","true");attr(clip,"duration","1");
    auto* track=element("keyframe-track",clip);attr(track,"path","bone.scale");attr(track,"type","vector");attr(track,"times","0 1");attr(track,"values","1 1 1 2 1 1");
    ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_animation_seek(scene3d_animations(root),.5));ASSERT_NE(snapshot(),nullptr);
    Scene3dMatrix animated={};bool visible=false;ASSERT_TRUE(scene3d_world_pose(root,bone,scene3d_animations(root),nullptr,nullptr,&animated,&visible));
    EXPECT_NEAR(animated.v[0],1.5,1e-6);Scene3dMatrix retained={};ASSERT_TRUE(scene3d_physics_pose(physics,bone,&retained));
    for(unsigned i=0;i<16;i++)EXPECT_NEAR(retained.v[i],bone_pose.v[i],1e-6);
    attr(bone,"physics-enabled","false");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);EXPECT_FALSE(scene3d_physics_pose(physics,bone,&bone_pose));
}

TEST_F(Scene3dTest, RagdollModesRetainMeasuredVelocityAndRecoverToAMovingAnimation) {
    auto rig=ragdoll_stage();ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));
    EXPECT_NEAR(state.bodies[0].pose.position.y,2.25,1e-6);EXPECT_EQ(state.bodies[0].motion,PHYSICS_KINEMATIC_POSITION);EXPECT_FALSE(state.joints[0].enabled);
    ASSERT_TRUE(scene3d_physics_advance(physics,2,&state,&error));EXPECT_EQ(state.tick,2u);
    EXPECT_NEAR(state.bodies[0].pose.position.x,.2,1e-6);EXPECT_NEAR(state.bodies[0].velocity.x,1,1e-6);
    PhysicsPose before=state.bodies[0].pose;attr(rig.control,"mode","physical");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,2u);EXPECT_EQ(state.bodies[0].motion,PHYSICS_DYNAMIC);
    EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.x,before.position.x);EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.y,before.position.y);EXPECT_TRUE(state.joints[0].enabled);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.3,1e-6);EXPECT_NEAR(state.bodies[0].pose.position.y,2.15,1e-6);
    before=state.bodies[0].pose;attr(rig.control,"mode","recover");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,3u);EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.y,before.position.y);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.35,1e-6);EXPECT_NEAR(state.bodies[0].pose.position.y,2.2,1e-6);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.5,1e-6);EXPECT_NEAR(state.bodies[0].pose.position.y,2.25,1e-6);
    Scene3dMatrix bone={};ASSERT_TRUE(scene3d_physics_pose(physics,rig.base,&bone));EXPECT_NEAR(bone.v[13],2,1e-6);
    attr(camera,"fov","75");ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_EQ(state.tick,6u);EXPECT_NEAR(state.bodies[0].pose.position.x,.6,1e-6);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(runtime,&stats));EXPECT_EQ(stats.ticks,state.tick);
}
TEST_F(Scene3dTest, RagdollSamplingUsesTheUpcomingKinematicParentTarget) {
    auto* parent=element("rigid-body",root);attr(parent,"id","carrier");attr(parent,"type","kinematic-position");
    auto* shape=element("collider",parent);attr(shape,"id","carrier-shape");attr(shape,"shape","box");
    auto rig=ragdoll_stage(parent);ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));
    PhysicsCommand command={};command.kind=PHYSICS_TARGET;command.body=state.bodies[0].id;command.pose={{3,0,0},{0,0,0,1}};
    ASSERT_TRUE(scene3d_physics_apply(physics,&command,1,&error));ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));
    EXPECT_TRUE(state.bodies[0].target_set);EXPECT_DOUBLE_EQ(state.bodies[0].target.position.x,3);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));
    EXPECT_NEAR(state.bodies[0].pose.position.x,3,1e-6);EXPECT_NEAR(state.bodies[1].pose.position.x,3.1,1e-6);
    EXPECT_NEAR(state.bodies[1].velocity.x,31,1e-5);EXPECT_NEAR(state.bodies[2].pose.position.x,3.1,1e-6);
    // rejected membership edits keep the existing world and its achieved pose.
    attr(rig.control,"bones","base base");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    attr(rig.control,"bones","base tip");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,1u);
}
TEST_F(Scene3dTest, RagdollRecoverySlerpsTheShortestArcAndRejectsNonrigidTargets) {
    auto rig=ragdoll_stage();attr(rig.config,"gravity","0 0 0");attr(rig.control,"mode","physical");ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));
    attr(rig.base,"quaternion","0 0 -.7071067811865475 -.7071067811865475");attr(rig.control,"mode","recover");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(fabs(state.bodies[0].pose.rotation.z),sin(acos(-1.0)/8),1e-6);
    EXPECT_NEAR(fabs(state.bodies[0].pose.rotation.w),cos(acos(-1.0)/8),1e-6);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(fabs(state.bodies[0].pose.rotation.z),sqrt(.5),1e-6);
    attr(rig.base,"scale","2 1 1");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_EQ(scene3d_physics(root),physics);
    attr(rig.base,"scale","1 1 1");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,3u);
}
TEST_F(Scene3dTest, RagdollActivationKeepsMeasuredAngularAndOffsetVelocity) {
    auto rig=ragdoll_stage();attr(rig.track,"path","base.quaternion");attr(rig.track,"type","quaternion");attr(rig.track,"values","0 0 0 1 0 0 .7071067811865475 .7071067811865475");
    ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,2,&state,&error));PhysicsBodyState before[2]={state.bodies[0],state.bodies[1]};
    EXPECT_NEAR(before[0].angular_velocity.z,acos(-1.0)/20,1e-6);EXPECT_GT(fabs(before[0].velocity.x),.03);
    EXPECT_GT(fabs(before[1].velocity.x),fabs(before[0].velocity.x));
    attr(rig.control,"mode","physical");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));
    for(size_t i=0;i<2;i++) {
        EXPECT_EQ(memcmp(&state.bodies[i].pose,&before[i].pose,sizeof(PhysicsPose)),0);
        EXPECT_EQ(memcmp(&state.bodies[i].velocity,&before[i].velocity,sizeof(PhysicsVec)),0);
        EXPECT_EQ(memcmp(&state.bodies[i].angular_velocity,&before[i].angular_velocity,sizeof(PhysicsVec)),0);
    }
}
TEST_F(Scene3dTest, RagdollSeekReplaysAuthoredAnimationAndKeepsTheSceneOwner) {
    ragdoll_stage();ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);auto* runtime=scene3d_runtime(root);
    auto* previous_animation=scene3d_animations(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,7,&state,&error));PhysicsBodyState expected[2]={state.bodies[0],state.bodies[1]};
    ASSERT_TRUE(scene3d_seek(root,7));EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_runtime(root),runtime);
    EXPECT_NE(scene3d_animations(root),previous_animation);ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,7u);
    for(size_t i=0;i<2;i++){EXPECT_EQ(memcmp(&state.bodies[i].pose,&expected[i].pose,sizeof(PhysicsPose)),0);EXPECT_EQ(memcmp(&state.bodies[i].velocity,&expected[i].velocity,sizeof(PhysicsVec)),0);}
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.8,1e-6);
    ASSERT_TRUE(scene3d_seek(root,0));ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,0u);
    EXPECT_NEAR(state.bodies[0].pose.position.x,0,1e-6);EXPECT_DOUBLE_EQ(plen(state.bodies[0].velocity),0);
}
TEST_F(Scene3dTest, RagdollSeekReplaysPhysicalAndRecoveryModes) {
    auto rig=ragdoll_stage();attr(rig.control,"mode","physical");ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,5,&state,&error));PhysicsPose expected=state.bodies[0].pose;
    ASSERT_TRUE(scene3d_seek(root,5));ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(memcmp(&state.bodies[0].pose,&expected,sizeof(expected)),0);
    attr(rig.control,"mode","recover");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_seek(root,1));
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.05,1e-6);
    EXPECT_NEAR(state.bodies[0].pose.position.y,(8+2.25)*.5,1e-6);EXPECT_EQ(state.tick,1u);
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));EXPECT_NEAR(state.bodies[0].pose.position.x,.2,1e-6);EXPECT_NEAR(state.bodies[0].pose.position.y,2.25,1e-6);
}
TEST_F(Scene3dTest, FailedRagdollReplayKeepsThePublishedWorldAndAnimation) {
    auto rig=ragdoll_stage();auto* scale=element("keyframe-track",rig.clip);attr(scale,"path","base.scale");attr(scale,"type","vector");
    attr(scale,"times","0 1");attr(scale,"values","1 1 1 2 1 1");ASSERT_NE(snapshot(),nullptr);
    auto* physics=scene3d_physics(root);auto* animation=scene3d_animations(root);auto* action=scene3d_animation_action(animation,"walk");
    PhysicsState state={};PhysicsError error={};ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));PhysicsPose retained=state.bodies[0].pose;
    EXPECT_FALSE(scene3d_seek(root,5));EXPECT_EQ(scene3d_physics(root),physics);EXPECT_EQ(scene3d_animations(root),animation);EXPECT_DOUBLE_EQ(action->time,0);
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,0u);EXPECT_EQ(memcmp(&state.bodies[0].pose,&retained,sizeof(retained)),0);
    Scene3dRuntimeStats stats={};ASSERT_TRUE(scene3d_runtime_stats(scene3d_runtime(root),&stats));EXPECT_FALSE(stats.faulted);EXPECT_EQ(stats.ticks,0u);
    ASSERT_NE(snapshot(),nullptr);attr(scale,"values","1 1 1 1 1 1");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_seek(root,5));
    ASSERT_TRUE(scene3d_physics_advance(physics,0,&state,&error));EXPECT_EQ(state.tick,5u);EXPECT_NEAR(state.bodies[0].pose.position.x,.5,1e-6);
}
TEST_F(Scene3dTest, RagdollVelocityParentSamplingUsesTheMassCentreOrbit) {
    auto* parent=element("rigid-body",root);attr(parent,"id","carrier");attr(parent,"type","kinematic-velocity");attr(parent,"angular-velocity","0 0 1");
    auto* shape=element("collider",parent);attr(shape,"id","carrier-shape");attr(shape,"shape","box");attr(shape,"position","1 0 0");
    ragdoll_stage(parent);ASSERT_NE(snapshot(),nullptr);auto* physics=scene3d_physics(root);PhysicsState state={};PhysicsError error={};
    ASSERT_TRUE(scene3d_physics_advance(physics,1,&state,&error));
    EXPECT_NEAR(state.bodies[0].pose.position.x,1-cos(.1),1e-12);EXPECT_NEAR(state.bodies[0].pose.position.y,-sin(.1),1e-12);
    EXPECT_NEAR(state.bodies[1].pose.position.x,1-cos(.1)+.1*cos(.1)-2.25*sin(.1),1e-6);
    EXPECT_NEAR(state.bodies[1].pose.position.y,-sin(.1)+.1*sin(.1)+2.25*cos(.1),1e-6);
}

TEST_F(Scene3dTest, NavigationGeometrySnapshotsWorldTransformsWithoutProjectionAndIgnoreCameraEdits) {
    auto* resources=element("resources",root);auto* geometry=element("geometry",resources);attr(geometry,"id","shared");attr(geometry,"type","buffer");
    attr(geometry,"positions","0 0 0 0 0 2 2 0 0");
    auto* parent=element("group",root);attr(parent,"position","4 3 2");attr(parent,"scale","2 1 3");
    auto* first=element("mesh",parent);attr(first,"id","floor");attr(first,"geometry","shared");attr(first,"position","1 0 0");
    auto* second=element("mesh",root);attr(second,"id","obstacle");attr(second,"geometry","shared");
    Scene3dNavigationSelection selections[]={{"floor",true,7,2},{"obstacle",false,1,0}};
    Scene3dNavigationGeometry geometry_copy;lam::ArrayList<DomNodeRef> refs;char diagnostic[256]={};Scene3dStats stats={};
    ASSERT_TRUE(scene3d_collect_navigation_geometry(root,selections,2,&geometry_copy,&refs,diagnostic,sizeof(diagnostic)))<<diagnostic;
    EXPECT_FALSE(scene3d_stats(root,&stats));EXPECT_EQ(scene3d_runtime(root),nullptr);ASSERT_EQ(geometry_copy.triangles.size(),2u);EXPECT_EQ(refs.size(),4u);
    const auto& t=geometry_copy.triangles[0];EXPECT_DOUBLE_EQ(t.vertices[0].x,6);EXPECT_DOUBLE_EQ(t.vertices[0].y,3);EXPECT_DOUBLE_EQ(t.vertices[1].z,8);
    EXPECT_DOUBLE_EQ(t.vertices[2].x,10);EXPECT_EQ(t.groups,7u);EXPECT_DOUBLE_EQ(t.cost,2);EXPECT_TRUE(t.walkable);EXPECT_FALSE(geometry_copy.triangles[1].walkable);
    NavigationBakeDesc bake_desc={geometry_copy.triangles.data(),geometry_copy.triangles.size(),{{0,-1,0},{12,8,10},1,.125,0,1.5,.375,acos(-1.0)/4,1024,4096,1000000},nullptr,nullptr};
    NavigationBake* bake=nullptr;NavigationError error={};ASSERT_TRUE(navigation_bake_create(&bake_desc,&bake,&error))<<(error.message?error.message:"");
    NavigationBakeStats baked={};EXPECT_TRUE(navigation_bake_stats(bake,&baked));EXPECT_GT(baked.cells,0u);navigation_bake_destroy(bake);EXPECT_FALSE(scene3d_stats(root,&stats));
    Scene3dNavigationGeometry equivalent;attr(camera,"position","9 8 7");
    ASSERT_TRUE(scene3d_collect_navigation_geometry(root,selections,2,&equivalent,nullptr,diagnostic,sizeof(diagnostic)))<<diagnostic;
    EXPECT_STREQ(geometry_copy.source_hash,equivalent.source_hash);
    attr(first,"position","2 0 0");ASSERT_TRUE(scene3d_collect_navigation_geometry(root,selections,2,&equivalent,nullptr,diagnostic,sizeof(diagnostic)));
    EXPECT_STRNE(geometry_copy.source_hash,equivalent.source_hash);EXPECT_DOUBLE_EQ(geometry_copy.triangles[0].vertices[0].x,6);
}
TEST_F(Scene3dTest, NavigationGeometryRejectsAmbiguousAndPosedSourcesWithoutReplacingAnOwnedSnapshot) {
    auto* source=buffer_quad();attr(source,"id","floor");Scene3dNavigationSelection selection={"floor",true,UINT32_MAX,1};
    Scene3dNavigationGeometry copied;char diagnostic[256]={};
    ASSERT_TRUE(scene3d_collect_navigation_geometry(root,&selection,1,&copied,nullptr,diagnostic,sizeof(diagnostic)))<<diagnostic;
    char retained[65];memcpy(retained,copied.source_hash,sizeof(retained));
    attr(source,"instance-count","2");EXPECT_FALSE(scene3d_collect_navigation_geometry(root,&selection,1,&copied,nullptr,diagnostic,sizeof(diagnostic)));
    EXPECT_NE(strstr(diagnostic,"instance snapshot"),nullptr);EXPECT_STREQ(copied.source_hash,retained);EXPECT_EQ(copied.triangles.size(),2u);
    attr(source,"instance-count","1");attr(source,"skeleton","rig");EXPECT_FALSE(scene3d_collect_navigation_geometry(root,&selection,1,&copied,nullptr,diagnostic,sizeof(diagnostic)));
    attr(source,"skeleton","");auto* duplicate=element("mesh",root);attr(duplicate,"id","floor");
    EXPECT_FALSE(scene3d_collect_navigation_geometry(root,&selection,1,&copied,nullptr,diagnostic,sizeof(diagnostic)));EXPECT_NE(strstr(diagnostic,"ambiguous"),nullptr);
    EXPECT_STREQ(copied.source_hash,retained);
}
TEST_F(Scene3dTest, NavigationGeometryReaderEnforcesTheCallersVertexBudget) {
    auto* source=buffer_quad();auto* geometry=source->first_child->as_element();Scene3dGeometrySource decoded={};char diagnostic[256]={};
    Pool* pool=pool_create();ASSERT_NE(pool,nullptr);
    EXPECT_FALSE(scene3d_read_geometry(geometry,pool,&decoded,diagnostic,sizeof(diagnostic),3));
    ASSERT_TRUE(scene3d_read_geometry(geometry,pool,&decoded,diagnostic,sizeof(diagnostic),4))<<diagnostic;EXPECT_EQ(decoded.vertex_count,4u);EXPECT_EQ(decoded.index_count,6u);
    pool_destroy(pool);
}
TEST_F(Scene3dTest, NavigationAssetsRetainCompiledDataAcrossCameraAndGraphicsRecovery) {
    auto* asset=navigation_asset();mesh();ASSERT_NE(snapshot(),nullptr);
    auto* cache=scene3d_navigation(root);ASSERT_NE(cache,nullptr);uint64_t generation=0,current=0;
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&generation));Scene3dNavigationStats before={},after={};
    ASSERT_TRUE(scene3d_navigation_stats(cache,&before));EXPECT_EQ(before.compilations,1u);EXPECT_EQ(before.nodes,9u);EXPECT_EQ(before.edges,24u);
    Scene3dStats projected={},changed={};ASSERT_TRUE(scene3d_stats(root,&projected));
    ASSERT_TRUE(scene3d_runtime_step(scene3d_runtime(root),3));
    attr(camera,"fov","70");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_navigation_stats(cache,&after));EXPECT_EQ(after.compilations,before.compilations);
    attr(asset,"data","{\"type\":\"grid\",\"size\":[4,3]}");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_stats(root,&changed));
    EXPECT_EQ(changed.projection_generation,projected.projection_generation);ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&current));EXPECT_GT(current,generation);
    generation=current;scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_navigation(root),cache);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&current));EXPECT_EQ(current,generation);
    ASSERT_TRUE(scene3d_navigation_stats(cache,&after));EXPECT_EQ(after.compilations,2u);EXPECT_EQ(after.invalidations,1u);
    Scene3dRuntimeStats clock={};ASSERT_TRUE(scene3d_runtime_stats(scene3d_runtime(root),&clock));EXPECT_EQ(clock.ticks,3u);
}

TEST_F(Scene3dTest, NavigationAssetEditsPublishTogetherAndRetireStaleGenerations) {
    auto* resources=element("resources",root);auto* first=navigation_asset("a","{\"type\":\"grid\",\"size\":[3,3]}",resources);
    auto* second=navigation_asset("b","{\"type\":\"grid\",\"size\":[3,3]}",resources);ASSERT_NE(snapshot(),nullptr);
    auto* cache=scene3d_navigation(root);uint64_t a=0,b=0,current=0;
    ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&a));ASSERT_TRUE(scene3d_navigation_generation(cache,"b",&b));
    attr(first,"data","{\"type\":\"grid\",\"size\":[4,3]}");attr(second,"data","{bad");
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"JSON object"),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&current));EXPECT_EQ(current,a);ASSERT_TRUE(scene3d_navigation_generation(cache,"b",&current));EXPECT_EQ(current,b);
    Scene3dNavigationStats rejected={},repeated={};ASSERT_TRUE(scene3d_navigation_stats(cache,&rejected));EXPECT_EQ(rejected.nodes,18u);EXPECT_EQ(rejected.invalidations,0u);
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);ASSERT_TRUE(scene3d_navigation_stats(cache,&repeated));EXPECT_EQ(repeated.compilations,rejected.compilations);
    attr(second,"data","{\"type\":\"grid\",\"size\":[3,3]}");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&current));EXPECT_GT(current,a);a=current;
    ASSERT_TRUE(scene3d_navigation_generation(cache,"b",&current));EXPECT_EQ(current,b);
    attr(first,"data","{\"size\":[4,3],\"type\":\"grid\"}");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&current));EXPECT_EQ(current,a); // equivalent source order preserves paths
    attr(first,"revision","1");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&current));EXPECT_GT(current,a);a=current;
    ASSERT_TRUE(resources->remove_child(first));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);EXPECT_FALSE(scene3d_navigation_generation(cache,"a",&current));
    ASSERT_TRUE(resources->append_child(first));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"a",&current));EXPECT_GT(current,a);
}

TEST_F(Scene3dTest, NavigationAssetsFollowEquivalentTemplateRebuildsAndRejectInvalidPlacement) {
    navigation_asset();ASSERT_NE(snapshot(),nullptr);auto* cache=scene3d_navigation(root);uint64_t generation=0,current=0;
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&generation));auto* previous=root;
    root=element("scene3d");camera=element("camera",root);attr(camera,"type","perspective");attr(camera,"id","main");attr(camera,"position","0 0 4");attr(camera,"target","0 0 0");attr(camera,"fov","90");attr(root,"camera","main");
    auto* asset=navigation_asset();scene3d_follow_rebuild(&doc,previous,root);doc.root=lam::up(root);doc.mutation_epoch++;
    ASSERT_NE(snapshot(),nullptr);EXPECT_EQ(scene3d_navigation(root),cache);ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&current));EXPECT_EQ(current,generation);
    attr(asset,"position","1 0 0");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"world coordinates"),nullptr);
    ASSERT_TRUE(asset->remove_attribute("position"));doc.mutation_epoch++;ASSERT_NE(snapshot(),nullptr);
    auto* parent=asset->parent->as_element();ASSERT_TRUE(parent->remove_child(asset));ASSERT_TRUE(root->append_child(asset));doc.mutation_epoch++;
    EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"directly to scene resources"),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&current));EXPECT_EQ(current,generation);
}

TEST_F(Scene3dTest, NavigationRejectsReentrantAuthoredEditsBeforeCachePublication) {
    Scene3dNavigationOwner owner;auto* asset=navigation_asset();ASSERT_TRUE(owner.create(root));char diagnostic[256]={};
    auto* cache=owner.cache;auto* runtime=owner.runtime;uint64_t before=0,after=0;
    ASSERT_TRUE(scene3d_navigation_prepare(cache,root,diagnostic,sizeof(diagnostic)));
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&before));
    using Fixture=decltype(this);struct Probe {Fixture fixture;DomElement* asset;Scene3dNavigation* cache;bool seen,accepted;};Probe probe={this,asset,cache,false,true};
    ASSERT_TRUE(scene3d_runtime_bind(runtime,SCENE3D_TICK_ANIMATION,&probe,[](void* opaque,double){
        auto* p=(Probe*)opaque;if(!p->seen){p->fixture->attr(p->asset,"data","{\"type\":\"grid\",\"size\":[4,4]}");char diagnostic[256]={};
            p->accepted=scene3d_navigation_prepare(p->cache,p->fixture->root,diagnostic,sizeof(diagnostic));p->seen=true;}return SCENE3D_TICK_ACTIVE;
    }));
    ASSERT_TRUE(scene3d_runtime_step(runtime,1));EXPECT_TRUE(probe.seen);EXPECT_FALSE(probe.accepted);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&after));EXPECT_EQ(before,after);
    scene3d_runtime_unbind(runtime,SCENE3D_TICK_ANIMATION,&probe);
    ASSERT_TRUE(scene3d_navigation_prepare(cache,root,diagnostic,sizeof(diagnostic)));
    ASSERT_TRUE(scene3d_navigation_generation(cache,"routes",&after));EXPECT_GT(after,before);
}

TEST_F(Scene3dTest, NavigationScenePackageReusesAssetsAndReportsStaleAndRejectedSources) {
    ASSERT_NE(load_page("test/scene3d/navigation.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===6&&navState.nodes.length===7&&navState.asset==='routes'").to_const()).asBool());
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);
    EXPECT_EQ(scene3d_physics(scene),nullptr);EXPECT_EQ(scene3d_animations(scene),nullptr);
    Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(scene,&stats));EXPECT_EQ(stats.graphics.resources,0u);
    ASSERT_FALSE(item_is_error(run_js("globalThis.oldGeneration=navState.generation;navRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.compilations===4&&navState.queries===1&&navState.assets.length===4&&!navState.rejected").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('road')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===2&&navState.nodes.join(',')==='a,b'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('current',oldGeneration)")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState===true").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('routes').setAttribute('data','{bad');navRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.rejected&&navState.assets[0].generation===oldGeneration&&typeof navState.diagnostic==='string'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('current',oldGeneration)")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState===false").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('routes').setAttribute('data',JSON.stringify({type:'grid',size:[3,3]}));navRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===2&&navState.generation!==oldGeneration").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('current',oldGeneration)")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState===false").to_const()).asBool());
}

TEST_F(Scene3dTest, TypedNavigationAssetsRetainGenerationsAcrossTemplateRedraws) {
    ASSERT_NE(load_page("test/scene3d/navigation-rerender.ls"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===6").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.oldGeneration=navState.generation;document.getElementById('redraw').click()")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('current',oldGeneration)")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState===true").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('road')")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===2").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('open').click()")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.cost===2&&navState.generation!==oldGeneration").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('current',oldGeneration)")));pump_js();EXPECT_TRUE(ItemReader(run_js("navState===false").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('surface')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&Math.abs(navState.length-Math.sqrt(2.5))<1e-12").to_const()).asBool());
}

TEST_F(Scene3dTest, NavigationGenerationsDoNotAliasAfterViewportReleaseOrAcrossViewports) {
    navigation_asset();ASSERT_NE(snapshot(),nullptr);uint64_t first=0,recreated=0,other=0;
    ASSERT_TRUE(scene3d_navigation_generation(scene3d_navigation(root),"routes",&first));
    scene3d_release_subtree(root);ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(scene3d_navigation(root),"routes",&recreated));EXPECT_GT(recreated,first);
    auto* container=element("div");doc.root=lam::up(container);ASSERT_TRUE(container->append_child(root));
    auto* viewport=element("scene3d",container);auto* resources=element("resources",viewport);
    navigation_asset("routes","{\"type\":\"grid\",\"size\":[3,3]}",resources);
    Scene3dNavigationOwner owner;ASSERT_TRUE(owner.create(viewport));
    char diagnostic[256]={};ASSERT_TRUE(scene3d_navigation_prepare(owner.cache,viewport,diagnostic,sizeof(diagnostic)));
    ASSERT_TRUE(scene3d_navigation_generation(owner.cache,"routes",&other));EXPECT_GT(other,recreated);
}

TEST_F(Scene3dTest, SurfaceNavigationEditsCompareGeometryBeyondTheCentroidGraph) {
    auto* asset=navigation_asset("floor","{\"type\":\"mesh\",\"vertices\":[[0,0],[3,0],[0,3]],\"triangles\":[[0,1,2]]}",nullptr,"navigation-mesh");mesh();
    ASSERT_NE(snapshot(),nullptr);auto* cache=scene3d_navigation(root);uint64_t first=0,changed=0;
    ASSERT_TRUE(scene3d_navigation_generation(cache,"floor",&first));Scene3dStats before={},after={};ASSERT_TRUE(scene3d_stats(root,&before));
    Scene3dNavigationStats stats={};ASSERT_TRUE(scene3d_navigation_stats(cache,&stats));EXPECT_EQ(stats.vertices,3u);EXPECT_EQ(stats.nodes,1u);
    attr(camera,"fov","70");ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_navigation_stats(cache,&stats));EXPECT_EQ(stats.compilations,1u);
    // the centroid remains (1,1), but its triangle and nearest-surface queries change.
    attr(asset,"data","{\"type\":\"mesh\",\"vertices\":[[0,0],[6,0],[-3,3]],\"triangles\":[[0,1,2]]}");ASSERT_NE(snapshot(),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"floor",&changed));EXPECT_GT(changed,first);ASSERT_TRUE(scene3d_stats(root,&after));EXPECT_EQ(after.projection_generation,before.projection_generation);
    first=changed;attr(asset,"data","{\"type\":\"grid\",\"size\":[2,2]}");EXPECT_EQ(scene3d_snapshot(root,&ui,128,128,1),nullptr);EXPECT_NE(strstr(scene3d_diagnostic(root),"kind disagree"),nullptr);
    ASSERT_TRUE(scene3d_navigation_generation(cache,"floor",&changed));EXPECT_EQ(changed,first);
    attr(asset,"data","{\"type\":\"mesh\",\"vertices\":[[0,0],[6,0],[-3,3]],\"triangles\":[[0,1,2]]}");ASSERT_NE(snapshot(),nullptr);
    scene3d_context_lost(root);ASSERT_NE(snapshot(),nullptr);ASSERT_TRUE(scene3d_navigation_generation(cache,"floor",&changed));EXPECT_EQ(changed,first);
}

TEST_F(Scene3dTest, SurfaceNavigationScenePackageReturnsGroundedPathsAndVersions) {
    ASSERT_NE(load_page("test/scene3d/navigation.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('surface')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.asset==='floor'&&navState.nodes.length>0&&Math.abs(navState.length-Math.sqrt(2.5))<1e-12&&Math.abs(navState.cost-navState.length)<1e-12&&navState.portals.length+1===navState.nodes.length").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.floorGeneration=navState.generation;navRequest('ramp')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.asset==='ramp'&&Math.abs(navState.length-(1.5+Math.sqrt(2)))<1e-12&&Math.abs(navState.cost-(1.5+2*Math.sqrt(2)))<1e-12&&navState.points.length>=4").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("navRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.assets.length===4&&navState.vertices===12&&navState.compilations===4&&!navState.rejected").to_const()).asBool());
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);EXPECT_EQ(scene3d_physics(scene),nullptr);Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(scene,&stats));EXPECT_EQ(stats.graphics.resources,0u);
}
TEST_F(Scene3dTest, BakedNavigationScenePackageReusesClearanceAndVersionsProfileChanges) {
    ASSERT_NE(load_page("test/scene3d/navigation-bake.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("navRequest('bake')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='complete'&&navState.asset==='clearance'&&navState.domain==='voxel_cells'&&navState.nodes.length===3&&navState.profile.radius===0.1&&navState.profile.height===1.5").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.clearanceGeneration=navState.generation;navRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.assets.length===1&&navState.compilations===1&&navState.nodes===4&&navState.edges===8&&!navState.rejected").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('main').setAttribute('position','1 0 4');navRequest('bake')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.generation===clearanceGeneration").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.bakeData=JSON.parse(document.getElementById('clearance').getAttribute('data'));bakeData.settings.height=1.25;document.getElementById('clearance').setAttribute('data',JSON.stringify(bakeData));navRequest('bake')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.generation>clearanceGeneration&&navState.profile.height===1.25").to_const()).asBool());
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);EXPECT_EQ(scene3d_physics(scene),nullptr);Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(scene,&stats));EXPECT_EQ(stats.graphics.resources,0u);
    ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);scene3d_context_lost(scene);ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);
    uint64_t retained=0;ASSERT_TRUE(scene3d_navigation_generation(scene3d_navigation(scene),"clearance",&retained));
    EXPECT_EQ((int64_t)retained,ItemReader(run_js("navState.generation").to_const()).asInt());
    ASSERT_FALSE(item_is_error(run_js("globalThis.latestClearance=navState.generation;bakeData.settings.cell_size=0;document.getElementById('clearance').setAttribute('data',JSON.stringify(bakeData));navRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.rejected&&navState.assets[0].generation===latestClearance").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("bakeData.settings.cell_size=1;bakeData.triangles=[];document.getElementById('clearance').setAttribute('data',JSON.stringify(bakeData));navRequest('bake')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("navState.status==='unreachable'&&navState.points.length===0&&navState.generation>latestClearance&&navState.profile.frame==='y_up'").to_const()).asBool());
}
TEST_F(Scene3dTest, NavigationBakeJobsPublishWhilePausedAndRoundTripCompiledMetadata) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('pause')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobCache.assets.length===1&&!jobCache.assets[0].ready&&jobCache.nodes===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));
    ASSERT_TRUE(wait_navigation_bake());EXPECT_TRUE(ItemReader(run_js("jobState.status==='ready'&&jobState.cells===36&&jobState.source_hash.length===64&&jobState.settings_hash.length===64").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('main').setAttribute('position','1 0 4');jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'&&jobState.current&&Number.isInteger(jobState.generation)").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.source_hash===jobState.source_hash&&jobPath.settings_hash===jobState.settings_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.firstPath=jobPath;jobRequest('export',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("savedBake.format==='cells'&&savedBake.version===1&&savedBake.frame==='y_up'&&savedBake.cells.length===36&&savedBake.source_hash===jobPath.source_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.savedText=JSON.stringify(savedBake);document.getElementById('routes').setAttribute('data',savedText);jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.source_hash===firstPath.source_hash&&jobPath.settings_hash===firstPath.settings_hash&&jobPath.length===firstPath.length&&jobPath.cost===firstPath.cost").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState===true").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'").to_const()).asBool());
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(scene,&stats));EXPECT_EQ(stats.graphics.resources,0u);
    Scene3dRuntimeStats clock={};ASSERT_TRUE(scene3d_runtime_stats(scene3d_runtime(scene),&clock));EXPECT_TRUE(clock.paused);EXPECT_EQ(clock.ticks,0u);
}
TEST_F(Scene3dTest, NavigationBakeJobsRejectSourceSettingsAndSupersededGenerations) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('source-parent').setAttribute('position','0.5 0 0');jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'&&!jobState.current").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobData=JSON.parse(document.getElementById('routes').getAttribute('data'));jobData.settings.height=1.4;document.getElementById('routes').setAttribute('data',JSON.stringify(jobData));jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'").to_const()).asBool());ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();ASSERT_FALSE(item_is_error(run_js("globalThis.previousJob=jobState.token;jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.latestJob=jobState.token;jobToken=previousJob")));ASSERT_TRUE(wait_navigation_bake());EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',previousJob)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobToken=latestJob")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'&&jobState.current").to_const()).asBool());
}
TEST_F(Scene3dTest, NavigationBakeJobsCancelAndTeardownWithoutPublishing) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token;jobRequest('cancel',jobToken)")));pump_js();ASSERT_TRUE(wait_navigation_bake());
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='cancelled'").to_const()).asBool());ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='cancelled'&&jobState.generation===null").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("!jobCache.assets[0].ready&&jobCache.nodes===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    // document teardown cancels/joins this larger copied snapshot without a tick or poll.
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobData=JSON.parse(document.getElementById('routes').getAttribute('data'));jobData.settings.cell_size=0.05;document.getElementById('routes').setAttribute('data',JSON.stringify(jobData));jobRequest('start')")));pump_js();
}
TEST_F(Scene3dTest, NavigationBakeJobsBoundRetainedSnapshotsAndKeepFailedResultsUnpublished) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.heldTokens=[];(async()=>{for(let i=0;i<8;i++){await jobRequest('start');heldTokens.push(jobState.token)}await jobRequest('start')})().catch(error=>{globalThis.jobLimit=String(error)})")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("heldTokens.length===8&&jobLimit.includes('eight retained jobs')").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.releaseIndex=0")));
    for(unsigned i=0;i<8;i++) {
        ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=heldTokens[releaseIndex++]")));ASSERT_TRUE(wait_navigation_bake());
        ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    }
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobData=JSON.parse(document.getElementById('routes').getAttribute('data'));jobData.settings.max_work=2;document.getElementById('routes').setAttribute('data',JSON.stringify(jobData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='failed'&&jobState.diagnostic.includes('work budget')").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='failed'&&jobState.generation===null").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("!jobCache.assets[0].ready&&jobCache.nodes===0").to_const()).asBool());
}
TEST_F(Scene3dTest, NavigationBakeJobsValidateSourceIdentityAndSurviveGraphicsRecovery) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("const old=document.getElementById('floor');const fresh=old.cloneNode(true);old.parentNode.replaceChild(fresh,old);jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'").to_const()).asBool());ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);scene3d_context_lost(scene);
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'&&jobState.current").to_const()).asBool());
    ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    auto* owner=scene3d_navigation(scene);Item clone=run_js("globalThis.nextWorld=document.getElementById('world').cloneNode(true);nextWorld");
    auto* next=(DomNode*)radiant_dom_unwrap_node(clone);ASSERT_NE(next,nullptr);ASSERT_TRUE(next->is_element());
    // retarget the retained service as a structural renderer rebuild does, before retiring its old root.
    scene3d_follow_rebuild(page,scene,next);
    ASSERT_FALSE(item_is_error(run_js("const previous=document.getElementById('world');previous.parentNode.replaceChild(nextWorld,previous)")));pump_js();
    EXPECT_EQ(scene3d_navigation(next->as_element()),owner);ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'&&jobState.diagnostic.includes('root')").to_const()).asBool());
}
TEST_F(Scene3dTest, NavigationBakeJobsPublishAndReopenGenuineEmptyDomains) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobData=JSON.parse(document.getElementById('routes').getAttribute('data'));jobData.sources=[{id:'floor',walkable:false}];document.getElementById('routes').setAttribute('data',JSON.stringify(jobData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());EXPECT_TRUE(ItemReader(run_js("jobState.cells===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'&&jobState.current").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('export',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("savedBake.cells.length===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('routes').setAttribute('data',JSON.stringify(savedBake));jobRequest('path').catch(error=>{globalThis.jobFailure=String(error)})")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='unreachable'&&jobPath.points.length===0").to_const()).asBool())
        <<ItemReader(run_js("(typeof jobFailure==='undefined'?'':jobFailure)+' '+JSON.stringify(savedBake)").to_const()).asString()->chars;
}
TEST_F(Scene3dTest, NavigationBakeJobsKeepPendingAndPublishedDataAcrossEquivalentTemplates) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs-rerender.ls"),nullptr);pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('redraw').click()")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("document.getElementById('lab').getAttribute('data-redraws')==='1'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'").to_const()).asBool());ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.templateBakeGeneration=jobPath.generation;globalThis.templateBakeHash=jobPath.source_hash;document.getElementById('redraw').click()")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.generation===templateBakeGeneration&&jobPath.source_hash===templateBakeHash").to_const()).asBool());
}


TEST_F(Scene3dTest, PolygonNavigationBakeJobsPublishRegionQuotasAndReopenSavedCells) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.bakeData=JSON.parse(document.getElementById('routes').getAttribute('data'));bakeData.settings.representation='polygons';document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    EXPECT_TRUE(ItemReader(run_js("jobState.cells===36&&jobState.status==='ready'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.domain==='voxel_polygons'&&jobPath.profile.representation==='polygons'&&jobPath.region_count===1&&jobPath.nodes.length===1&&jobPath.points.length===2&&Math.abs(jobPath.length-Math.sqrt(8))<1e-12").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.firstPath=jobPath;jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobCache.nodes===1&&jobCache.edges===0&&jobCache.assets[0].ready").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('export',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("savedBake.cells.length===36&&savedBake.settings.representation==='polygons'&&savedBake.settings_hash===firstPath.settings_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('routes').setAttribute('data',JSON.stringify(savedBake));jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.domain==='voxel_polygons'&&jobPath.region_count===1&&jobPath.length===firstPath.length&&jobPath.cost===firstPath.cost&&jobPath.source_hash===firstPath.source_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);Scene3dStats stats={};ASSERT_TRUE(scene3d_stats(scene,&stats));EXPECT_EQ(stats.graphics.resources,0u);
    ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);scene3d_context_lost(scene);ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.region_count===1&&jobPath.length===firstPath.length").to_const()).asBool());
}
TEST_F(Scene3dTest, PolygonNavigationBakeProfileEditsRejectStaleJobsAndRecountTheGraph) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.bakeData=JSON.parse(document.getElementById('routes').getAttribute('data'));bakeData.settings.representation='polygons';document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.oldToken=globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("bakeData.settings.representation='cells';document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('publish',oldToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'&&jobState.generation===null").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('start')")));pump_js();ASSERT_FALSE(item_is_error(run_js("globalThis.cellToken=globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',cellToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobCache.nodes===36&&jobCache.edges===120").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("bakeData.settings.representation='polygons';document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.polyToken=globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',polyToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobCache.nodes===1&&jobCache.edges===0&&!jobCache.rejected").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.domain==='voxel_polygons'&&jobPath.nodes.length===1").to_const()).asBool());
}

TEST_F(Scene3dTest, PolygonNavigationCacheRetainsTheCombinedBackingStorageQuota) {
    const char* data=R"json({"type":"bake","settings":{"low":[0,-1,0],"high":[128,4,256],"cell_size":1,"cell_height":0.125,"radius":0,"height":1.5,"max_step":0.375,"representation":"polygons"},"triangles":[[[0,0,0],[0,0,256],[128,0,256]],[[0,0,0],[128,0,256],[128,0,0]]]})json";
    auto* resources=element("resources",root);navigation_asset("first",data,resources,"navigation-bake");navigation_asset("second",data,resources,"navigation-bake");
    Scene3dNavigationOwner owner;ASSERT_TRUE(owner.create(root));char diagnostic[256]={};
    ASSERT_TRUE(scene3d_navigation_prepare(owner.cache,root,diagnostic,sizeof(diagnostic)))<<diagnostic;
    Scene3dNavigationStats stats={};ASSERT_TRUE(scene3d_navigation_stats(owner.cache,&stats));EXPECT_EQ(stats.nodes,2u);EXPECT_EQ(stats.assets,2u);EXPECT_EQ(stats.storage_nodes,(size_t)NAVIGATION_MAX_NODES);
    uint64_t before=0,after=0;ASSERT_TRUE(scene3d_navigation_generation(owner.cache,"first",&before));
    navigation_asset("overflow",data,resources,"navigation-bake");
    EXPECT_FALSE(scene3d_navigation_prepare(owner.cache,root,diagnostic,sizeof(diagnostic)));EXPECT_NE(strstr(diagnostic,"quota"),nullptr);
    ASSERT_TRUE(scene3d_navigation_stats(owner.cache,&stats));EXPECT_EQ(stats.nodes,2u);EXPECT_EQ(stats.assets,2u);
    ASSERT_TRUE(scene3d_navigation_generation(owner.cache,"first",&after));EXPECT_EQ(before,after);
}

TEST_F(Scene3dTest, PolygonNavigationBakePublicationPreservesBackingStorageAndCanRetryAfterRelease) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs.html"),nullptr);pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.bakeData=JSON.parse(document.getElementById('routes').getAttribute('data'));bakeData.settings.representation='polygons';document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('export',jobToken)")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.cached=document.getElementById('routes').cloneNode(false);cached.id='cached';cached.setAttribute('data',JSON.stringify(savedBake));document.getElementById('routes').parentNode.appendChild(cached);bakeData.settings.cell_size=0.015625;bakeData.settings.radius=0;document.getElementById('routes').setAttribute('data',JSON.stringify(bakeData));jobRequest('start')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.jobToken=jobState.token")));ASSERT_TRUE(wait_navigation_bake());EXPECT_TRUE(ItemReader(run_js("jobState.status==='ready'&&jobState.cells===65536").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.publishError=null;(async()=>{try{await jobRequest('publish',jobToken)}catch(e){publishError=String(e)}})()")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("typeof publishError==='string'&&publishError.includes('quota')").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobCache.nodes===1&&jobCache.storage_nodes===36&&jobCache.assets.length===2").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("cached.remove();jobRequest('publish',jobToken)")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobState.status==='published'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobCache.nodes===1&&jobCache.storage_nodes===65536&&jobCache.assets.length===1").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
}

TEST_F(Scene3dTest, LinkedNavigationJobsPublishSaveAndRetainExplicitTraversals) {
    ASSERT_NE(load_page("test/scene3d/navigation-links.html"),nullptr);pump_js();ASSERT_TRUE(start_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links.length===1&&jobPath.links[0].id==='gap'&&jobPath.links[0].kind==='jump'&&jobPath.links[0].duration===0.4&&jobPath.links[0].start_point===0&&jobPath.links[0].end_point===1&&jobPath.points.length===2&&jobPath.cost===4").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.firstLinkedPath=jobPath;globalThis.linkData=JSON.parse(document.getElementById('routes').getAttribute('data'));linkData.links[0].disabled=true;document.getElementById('routes').setAttribute('data',JSON.stringify(linkData));jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='unreachable'&&jobPath.links.length===0&&jobPath.generation>firstLinkedPath.generation&&jobPath.source_hash===firstLinkedPath.source_hash&&jobPath.settings_hash===firstLinkedPath.settings_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobCache.assets[0].ready&&jobCache.assets[0].link_count===1&&jobCache.nodes===4&&jobCache.storage_nodes===12").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('export',jobToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("savedBake.links.length===1&&savedBake.links[0].id==='gap'&&!savedBake.links[0].disabled&&savedBake.cells.length===10").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('routes').setAttribute('data',JSON.stringify(savedBake));jobEndpoints={start:[5.5,0,2],end:[1.5,0,2]};jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links.length===1&&jobPath.links[0].reverse&&jobPath.links[0].start[0]===5.5&&jobPath.cost===4").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('release',jobToken)")));pump_js();
    auto* scene=dom_find_element_by_id(page->root->as_element(),"world");ASSERT_NE(scene,nullptr);ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);scene3d_context_lost(scene);ASSERT_NE(scene3d_snapshot(scene,&ui,128,128,1),nullptr);
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links[0].reverse").to_const()).asBool());
}
TEST_F(Scene3dTest, LinkedNavigationEditsRejectInvalidAttachmentsWithoutReplacingTheWorkingGeneration) {
    ASSERT_NE(load_page("test/scene3d/navigation-links.html"),nullptr);pump_js();ASSERT_TRUE(start_navigation_bake());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("globalThis.firstLinkedGeneration=jobPath.generation;globalThis.linkData=JSON.parse(document.getElementById('routes').getAttribute('data'));linkData.links[0].end=[100,0,100];document.getElementById('routes').setAttribute('data',JSON.stringify(linkData));jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobCache.rejected&&jobCache.assets[0].generation===firstLinkedGeneration&&jobCache.assets[0].ready&&jobCache.diagnostic.includes('attachment')").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("linkData.links[0].end=[5.5,0,2];linkData.links[0].disabled=true;document.getElementById('routes').setAttribute('data',JSON.stringify(linkData));jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("!jobCache.rejected&&jobCache.assets[0].ready&&jobCache.assets[0].generation>firstLinkedGeneration").to_const()).asBool())
        <<ItemReader(run_js("JSON.stringify({cache:jobCache,data:linkData})").to_const()).asString()->chars;
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='unreachable'&&jobPath.links.length===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("linkData.links[0].disabled=false;document.getElementById('routes').setAttribute('data',JSON.stringify(linkData));jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links[0].id==='gap'").to_const()).asBool());
}
TEST_F(Scene3dTest, LinkedNavigationPendingJobsRejectChangedActionMetadata) {
    ASSERT_NE(load_page("test/scene3d/navigation-links.html"),nullptr);pump_js();ASSERT_TRUE(start_navigation_bake(false));
    ASSERT_FALSE(item_is_error(run_js("globalThis.oldLinkToken=jobToken;globalThis.linkData=JSON.parse(document.getElementById('routes').getAttribute('data'));linkData.links[0].cost=0;document.getElementById('routes').setAttribute('data',JSON.stringify(linkData));jobRequest('publish',oldLinkToken)")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobState.status==='stale'&&jobState.generation===null").to_const()).asBool());
    ASSERT_TRUE(start_navigation_bake());ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links[0].cost===0&&jobPath.cost===0").to_const()).asBool());
}
TEST_F(Scene3dTest, TypedNavigationLinksRetainGeometryAcrossTemplateAndLinkOnlyEdits) {
    ASSERT_NE(load_page("test/scene3d/navigation-jobs-rerender.ls"),nullptr);pump_js();ASSERT_TRUE(start_navigation_bake(false));
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('redraw').click()")));pump_js();
    ASSERT_FALSE(item_is_error(run_js("jobRequest('publish',jobToken)")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links.length===1&&jobPath.links[0].id==='warp'&&jobPath.cost===0").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("globalThis.typedLinkPath=jobPath;document.getElementById('redraw').click()")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.generation===typedLinkPath.generation&&jobPath.links[0].id==='warp'").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('toggle').click()")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('read')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobCache.assets[0].ready&&jobCache.assets[0].generation>typedLinkPath.generation").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();EXPECT_TRUE(ItemReader(run_js("jobPath.status==='complete'&&jobPath.links.length===0&&jobPath.cost>0&&jobPath.source_hash===typedLinkPath.source_hash").to_const()).asBool());
    ASSERT_FALSE(item_is_error(run_js("document.getElementById('toggle').click()")));pump_js();ASSERT_FALSE(item_is_error(run_js("jobRequest('path')")));pump_js();
    EXPECT_TRUE(ItemReader(run_js("jobPath.links.length===1&&jobPath.cost===0&&jobPath.source_hash===typedLinkPath.source_hash").to_const()).asBool());
}
