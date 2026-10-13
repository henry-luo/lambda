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
#include "../lambda/runtime/root_vector.h"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/runtime-state.h"
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
    EXPECT_EQ(native_gl_buffer(graphics,color,129u*1024u*1024u).id,0u);
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
    NativeGlDraw draw={program,vertices,texture,6,1,false,false,0,false};ASSERT_TRUE(native_gl_draw(graphics,&draw));
    ImageSurface* capture=native_gl_snapshot(graphics,target);ASSERT_NE(capture,nullptr);
    pixel(capture,16,4,255,0,0);pixel(capture,16,27,0,0,255);image_surface_destroy(capture);
    native_gl_release(graphics,texture);sampler.max_size=0;EXPECT_EQ(native_gl_texture(graphics,&image,sampler).id,0u);
    native_gl_destroy(graphics);
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
    NativeGlDraw draw={program,vertices,{},3,1,true,false,0,false};
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
