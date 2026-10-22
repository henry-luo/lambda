#include "radiant_render_test_utils.hpp"
#include "../lambda/io/image_io.h"
#include <gtest/gtest.h>

#include "../radiant/render.hpp"
#include "../lib/image_process.h"
#include "../lambda/dom/dom_observers.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>
#include <thorvg_capi.h>
#include "../radiant/rdt_path_internal.hpp"
#include "../radiant/render_metrics.hpp"

TEST(SvgTransformTest, IndividualPropertiesComposeBeforeCssAndAttributeLists) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    SvgLengthContext lengths = {200.0f, 100.0f, 16.0f, 8.0f};
    Bound box = {0.0f, 0.0f, 200.0f, 100.0f};
    const char* individual[] = {"100px 20px", "90deg", "2 1"};
    const char* lists[] = {"translateX(5px)", "translate(5 0)"};
    for (unsigned index = 0; index < 2; index++) {
        RdtMatrix matrix = svg_resolve_local_transform(lists[index], index == 0,
            nullptr, &box, &lengths, individual);
        EXPECT_NEAR(matrix.e11, 0.0f, .0001f);
        EXPECT_NEAR(matrix.e12, -1.0f, .0001f);
        EXPECT_NEAR(matrix.e21, 2.0f, .0001f);
        EXPECT_NEAR(matrix.e22, 0.0f, .0001f);
        EXPECT_NEAR(matrix.e13, 100.0f, .0001f);
        EXPECT_NEAR(matrix.e23, 30.0f, .0001f);
    }
    box = {10.0f, 20.0f, 30.0f, 30.0f};
    const char* percentage[] = {"50% 25%", "none", "none"};
    RdtMatrix matrix = svg_resolve_local_transform(nullptr, false, "center", &box, &lengths, percentage);
    EXPECT_FLOAT_EQ(matrix.e13, 10.0f);
    EXPECT_FLOAT_EQ(matrix.e23, 2.5f);
    pool_destroy(pool);
}

TEST(ImageProcessTest, RunningBoxMatchesIndependentNeighborhoods) {
    struct Rows { double input[70], output[70]; } rows={};
    for (int i=0;i<70;i++) rows.input[i]=(i*17%31)*0.125;
    auto read=[](void* data,int64_t y,double* row) -> bool {
        memcpy(row,((Rows*)data)->input+y*14,14*sizeof(double)); return true;
    };
    auto write=[](void* data,int64_t y,const double* row) -> bool {
        memcpy(((Rows*)data)->output+y*14,row,14*sizeof(double)); return true;
    };
    for (int kernel : {1,2,3,8,127}) {
        ASSERT_TRUE(image_box_filter(7,5,2,kernel,read,write,&rows));
        for (int y=0;y<5;y++) for (int x=0;x<7;x++) for (int c=0;c<2;c++) {
            double sum=0;
            for (int ky=0;ky<kernel;ky++) for (int kx=0;kx<kernel;kx++) {
                int sy=y+ky-kernel/2, sx=x+kx-kernel/2;
                sy=sy<0 ? 0 : sy>4 ? 4 : sy; sx=sx<0 ? 0 : sx>6 ? 6 : sx;
                sum+=rows.input[(sy*7+sx)*2+c];
            }
            EXPECT_NEAR(rows.output[(y*7+x)*2+c],sum/(kernel*kernel),1e-12);
        }
    }
}

TEST(ImageProcessTest, CacheInvalidationMatchesFreshEvaluation) {
    uint8_t pixels[64*48*4];
    for (size_t i=0;i<sizeof(pixels);i++) pixels[i]=(uint8_t)(i*37%256);
    ImageProcessSource source={pixels,64,48,{64*4,4,1},1};
    ImageProcessOptions options; options.angle=3; options.gamma=1.2; options.softness=3;
    ImageProcessCache* cache=image_process_cache_create(300000);
    ASSERT_NE(cache,nullptr);
    for (int step=0;step<10;step++) {
        if (step==1) options.vignette=0.7;
        if (step==2) options.sharpen=0.9;
        if (step==3) options.exposure=0.4;
        if (step==4) { options.output_width=31; options.output_height=27; }
        if (step==5) { source.revision++; pixels[0]=255; }
        if (step==6) {
            options.curve_counts[0]=2;
            options.curves[0][0][1]=1; options.curves[0][1][0]=1;
        }
        if (step==7) options.curves[0][1][1]=0.3;
        if (step==8) { options.output_black=0.1; options.output_white=0.8; }
        if (step==9) options.smooth_curves=true;
        ImageProcessResult cached, fresh;
        ASSERT_TRUE(image_process(source,options,&cached,cache));
        ASSERT_TRUE(image_process(source,options,&fresh));
        ASSERT_EQ(cached.width,fresh.width); ASSERT_EQ(cached.height,fresh.height);
        for (size_t i=0;i<(size_t)fresh.width*fresh.height*4;i++)
            EXPECT_LE(abs((int)cached.pixels[i]-(int)fresh.pixels[i]),1) << "step=" << step << " offset=" << i;
        if (step==1 || step==2 || step==4) {
            EXPECT_EQ(cached.color_hits,1u); EXPECT_EQ(cached.blur_hits,1u);
        }
        if (step==3) { EXPECT_EQ(cached.color_hits,0u); EXPECT_EQ(cached.blur_hits,0u); }
        if (step==5) { EXPECT_EQ(cached.geometry_hits,0u); EXPECT_EQ(cached.color_hits,0u); EXPECT_EQ(cached.blur_hits,0u); }
        if (step>=6) {
            EXPECT_EQ(cached.geometry_hits,1u); EXPECT_EQ(cached.color_hits,0u); EXPECT_EQ(cached.blur_hits,0u);
        }
        EXPECT_LE(cached.cache_bytes,300000u);
        image_process_result_free(&cached); image_process_result_free(&fresh);
    }
    image_process_cache_destroy(cache);
}

TEST(ImageProcessTest, LevelsAndChannelCurvesMatchIndependentReference) {
    uint8_t pixels[256*4];
    for (int x=0;x<256;x++) {
        pixels[x*4]=(uint8_t)x; pixels[x*4+1]=(uint8_t)(255-x);
        pixels[x*4+2]=(uint8_t)(x*37%256); pixels[x*4+3]=(uint8_t)(x%255+1);
    }
    ImageProcessSource source={pixels,256,1,{256*4,4,1},1};
    ImageProcessOptions options;
    options.black=0.1; options.white=0.9; options.gamma=1.7;
    options.output_black=0.15; options.output_white=0.85;
    const double master[][2]={{0,0.1},{0.3,0.8},{0.7,0.2},{1,0.9}};
    options.curve_counts[0]=4; memcpy(options.curves[0],master,sizeof(master));
    for (int channel=1;channel<4;channel++) {
        options.curve_counts[channel]=2;
        options.curves[channel][0][1]=0.1*channel;
        options.curves[channel][1][0]=1; options.curves[channel][1][1]=1-0.1*channel;
    }
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result));
    for (int x=0;x<256;x++) {
        for (int c=0;c<3;c++) {
            double value=fmax(0.0,fmin(1.0,(pixels[x*4+c]/255.0-0.1)/0.8));
            value=pow(value,1.0/1.7);
            unsigned segment=value<0.3 ? 0 : value<0.7 ? 1 : 2;
            const double* a=master[segment]; const double* b=master[segment+1];
            value=a[1]+(b[1]-a[1])*(value-a[0])/(b[0]-a[0]);
            value=0.1*(c+1)+value*(1-0.2*(c+1));
            int expected=(int)round((0.15+value*0.7)*255);
            EXPECT_LE(abs((int)result.pixels[x*4+c]-expected),1) << "x=" << x << " channel=" << c;
        }
        EXPECT_EQ(result.pixels[x*4+3],pixels[x*4+3]);
    }
    image_process_result_free(&result);
}

TEST(ImageProcessTest, CurvesPreservePeaksBetweenEightBitLookupSamples) {
    uint8_t pixels[]={64,64,64,255};
    ImageProcessSource source={pixels,1,1,{4,4,1},1};
    ImageProcessOptions options; options.gamma=2;
    double peak=sqrt(64.0/255.0);
    const double points[][2]={{0,0},{peak-0.00001,0},{peak,1},{peak+0.00001,0},{1,0}};
    options.curve_counts[0]=5; memcpy(options.curves[0],points,sizeof(points));
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result));
    EXPECT_EQ(result.pixels[0],255); EXPECT_EQ(result.pixels[1],255); EXPECT_EQ(result.pixels[2],255);
    image_process_result_free(&result);
}

TEST(ImageProcessTest, SmoothCurvesMatchParabolaAndInvalidateColorCache) {
    uint8_t pixels[256*4];
    for (int x=0;x<256;x++) {
        for (int c=0;c<3;c++) pixels[x*4+c]=(uint8_t)x;
        pixels[x*4+3]=(uint8_t)(x%255+1);
    }
    ImageProcessSource source={pixels,256,1,{256*4,4,1},1};
    ImageProcessOptions options;
    const double points[][2]={{0,0},{0.5,1},{1,0}};
    options.curve_counts[0]=3; memcpy(options.curves[0],points,sizeof(points));
    ImageProcessCache* cache=image_process_cache_create(65536);
    ASSERT_NE(cache,nullptr);
    ImageProcessResult linear, smooth;
    ASSERT_TRUE(image_process(source,options,&linear,cache));
    options.smooth_curves=true;
    ASSERT_TRUE(image_process(source,options,&smooth,cache));
    EXPECT_EQ(smooth.color_hits,0u);
    EXPECT_NE(linear.pixels[64*4],smooth.pixels[64*4]);
    // this symmetric three-knot curve is exactly y=4*x*(1-x), independently of the implementation.
    for (int x=0;x<256;x++) {
        double value=x/255.0;
        int expected=(int)lround(4*value*(1-value)*255);
        for (int c=0;c<3;c++) EXPECT_LE(abs((int)smooth.pixels[x*4+c]-expected),1);
        EXPECT_EQ(smooth.pixels[x*4+3],pixels[x*4+3]);
    }
    image_process_result_free(&linear); image_process_result_free(&smooth);
    ASSERT_TRUE(image_process(source,options,&smooth,cache));
    EXPECT_EQ(smooth.color_hits,1u);
    image_process_result_free(&smooth);
    image_process_cache_destroy(cache);
}

TEST(ImageProcessTest, SmoothNonuniformCurvesPreserveSegmentBoundsAndExtrema) {
    uint8_t pixels[256*4];
    for (int x=0;x<256;x++) for (int c=0;c<4;c++) pixels[x*4+c]=(uint8_t)(c==3 ? 255 : x);
    ImageProcessSource source={pixels,256,1,{256*4,4,1},1};
    ImageProcessOptions options; options.smooth_curves=true;
    const double points[][2]={{0,0.1},{0.02,0.9},{0.4,0.2},{0.7,0.2},{1,0.8}};
    options.curve_counts[0]=5; memcpy(options.curves[0],points,sizeof(points));
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result));
    for (int x=0;x<256;x++) {
        double value=x/255.0;
        int segment=value<0.02 ? 0 : value<0.4 ? 1 : value<0.7 ? 2 : 3;
        int low=(int)floor(fmin(points[segment][1],points[segment+1][1])*255);
        int high=(int)ceil(fmax(points[segment][1],points[segment+1][1])*255);
        EXPECT_GE(result.pixels[x*4],low); EXPECT_LE(result.pixels[x*4],high);
        if (x>0 && value>=0.4 && value<=0.7) EXPECT_EQ(result.pixels[x*4],51);
    }
    image_process_result_free(&result);
}

TEST(ImageProcessTest, InvalidToneDataFailsBeforeAllocatingOutput) {
    uint8_t pixels[]={64,128,192,255};
    ImageProcessSource source={pixels,1,1,{4,4,1},1};
    for (int variant=0;variant<6;variant++) {
        ImageProcessOptions options;
        options.curve_counts[0]=3;
        options.curves[0][1][0]=0.5; options.curves[0][1][1]=0.8;
        options.curves[0][2][0]=1; options.curves[0][2][1]=1;
        if (variant==0) options.curve_counts[0]=IMAGE_TONE_POINTS+1;
        if (variant==1) options.curves[0][2][0]=0.5;
        if (variant==2) options.curves[0][1][1]=NAN;
        if (variant==3) options.curves[0][1][1]=1.1;
        if (variant==4) options.curves[0][0][0]=0.1;
        if (variant==5) { options.output_black=0.8; options.output_white=0.2; }
        ImageProcessResult result;
        EXPECT_FALSE(image_process(source,options,&result));
        EXPECT_EQ(result.pixels,nullptr);
    }
}

TEST(ImageProcessTest, CacheBudgetAndCancellationKeepOwnedBuffersBounded) {
    uint8_t pixels[32*24*4]={};
    ImageProcessSource source={pixels,32,24,{32*4,4,1},1};
    ImageProcessCache* cache=image_process_cache_create(8192);
    ImageProcessOptions options; options.angle=2; options.softness=6;
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result,cache));
    EXPECT_LE(result.cache_bytes,8192u);
    image_process_result_free(&result);
    bool cancelled=true;
    EXPECT_FALSE(image_process(source,options,&result,cache,&cancelled));
    EXPECT_EQ(result.pixels,nullptr);
    image_process_cache_destroy(cache);
}

TEST(ImageProcessTest, CallerOwnsTheProvidedOutputBuffer) {
    uint8_t pixels[]={255,0,0,255,0,0,255,127}, output[8]={};
    ImageProcessSource source={pixels,2,1,{8,4,1},1};
    ImageProcessOptions options;
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result,nullptr,nullptr,output,sizeof(output)));
    EXPECT_FALSE(result.owns_pixels);
    EXPECT_EQ(result.pixels,output);
    EXPECT_EQ(memcmp(pixels,output,sizeof(output)),0);
    image_process_result_free(&result);
    EXPECT_EQ(memcmp(pixels,output,sizeof(output)),0);
    EXPECT_FALSE(image_process(source,options,&result,nullptr,nullptr,output,sizeof(output)-1));
}

TEST(ImageProcessTest, TwelveMegapixelEffectsUseStripScratch) {
    size_t bytes=4000u*3000u*4u;
    uint8_t* pixels=(uint8_t*)mem_alloc(bytes,MEM_CAT_TEMP);
    ASSERT_NE(pixels,nullptr);
    memset(pixels,128,bytes);
    ImageProcessSource source={pixels,4000,3000,{4000*4,4,1},1};
    ImageProcessOptions options; options.softness=6; options.sharpen=0.8; options.vignette=0.5;
    ImageProcessResult result;
    ASSERT_TRUE(image_process(source,options,&result));
    EXPECT_LT(result.scratch_bytes,8u*1024u*1024u);
    EXPECT_EQ(result.pixels[3],128); EXPECT_EQ(result.pixels[bytes-1],128);
    image_process_result_free(&result); mem_free(pixels);
}

TEST(RdtVectorTest, CompactRectangleAffineExpansionPreservesContourBounds) {
    RdtPath* source = rdt_path_new();
    RdtPath* transformed = rdt_path_new();
    ASSERT_NE(source, nullptr); ASSERT_NE(transformed, nullptr);
    rdt_path_add_rect(source, 1.0f, 2.0f, 4.0f, 6.0f, 0.0f, 0.0f);
    RdtMatrix matrix = {1.0f, 0.5f, 2.0f, -0.25f, 1.0f, 3.0f, 0.0f, 0.0f, 1.0f};
    ASSERT_TRUE(render_path_append_transformed(transformed, source, &matrix));
    rdt_path_free(source);
    float left, top, right, bottom;
    ASSERT_TRUE(rdt_path_get_bounds(transformed, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(left, 4.0f); EXPECT_FLOAT_EQ(right, 11.0f);
    EXPECT_FLOAT_EQ(top, 3.75f); EXPECT_FLOAT_EQ(bottom, 10.75f);
    Bound rectangle;
    EXPECT_FALSE(rdt_path_get_rect_bound(transformed, nullptr, &rectangle));
    rdt_path_free(transformed);
}

TEST(RdtVectorTest, RectangleClipBoundsRejectCurvesCompoundPathsAndRotation) {
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 3.5f, 5.0f, 8.0f, 4.0f, 0.0f, 0.0f);
    Bound bounds;
    ASSERT_TRUE(rdt_path_get_rect_bound(path, nullptr, &bounds));
    EXPECT_FLOAT_EQ(bounds.left, 3.5f); EXPECT_FLOAT_EQ(bounds.bottom, 9.0f);
    RdtMatrix flipped = rdt_matrix_identity();
    flipped.e11 = -2.0f; flipped.e22 = 1.5f; flipped.e13 = 30.0f;
    ASSERT_TRUE(rdt_path_get_rect_bound(path, &flipped, &bounds));
    EXPECT_FLOAT_EQ(bounds.left, 7.0f); EXPECT_FLOAT_EQ(bounds.right, 23.0f);
    EXPECT_FLOAT_EQ(bounds.top, 7.5f); EXPECT_FLOAT_EQ(bounds.bottom, 13.5f);
    RdtMatrix rotated = rdt_matrix_identity(); rotated.e12 = 1.0f;
    EXPECT_FALSE(rdt_path_get_rect_bound(path, &rotated, &bounds));
    rdt_path_add_rect(path, 0, 0, 1, 1, 0, 0);
    EXPECT_FALSE(rdt_path_get_rect_bound(path, nullptr, &bounds));
    rdt_path_free(path);
    Corner zero_radius = {};
    path = render_path_create_rounded_rect({3.5f, 5.0f, 8.0f, 4.0f}, &zero_radius);
    ASSERT_TRUE(rdt_path_get_rect_bound(path, nullptr, &bounds));
    rdt_path_free(path);
    path = rdt_path_new(); rdt_path_add_rect(path, 0, 0, 8, 4, 1, 1);
    EXPECT_FALSE(rdt_path_get_rect_bound(path, nullptr, &bounds));
    rdt_path_free(path);
}
#include "../radiant/render_glyph_run_raster_lower.hpp"
#include "../radiant/event.hpp"
#include "../radiant/svg_animation.hpp"
#include "../radiant/radiant.hpp"
#include "../lib/font/font_internal.h"
#include "../lib/font/font_tables.h"
#include "../lib/endian.h"
#include "../lib/str.h"

#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_engine.h"
#include "../lambda/dom/dom_host_engine.h"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/enhanced_file_cache.h"

#include "../lib/image.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"

TEST(RadiantProviderInit, NativeEmbeddingRegistrationIsIdempotent) {
    ASSERT_TRUE(radiant_register_dom_engine_providers());
    const DomHostEngineOps* operations = dom_host_engine_ops();
    ASSERT_NE(operations, nullptr);
    EXPECT_TRUE(radiant_register_dom_engine_providers());
    EXPECT_EQ(dom_host_engine_ops(), operations);
}

TEST(RdtVectorTest, GaussianFlatRunsPreserveEdgesChannelsAndPitches) {
    // independent Gaussian step-response samples (sigma=1.25, support=4).
    const int points[] = {0,4,6,10,11,16,21,22,28,32};
    const uint32_t expected[][10] = {
        {0x542a150bu,0x80402010u,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0,0},
        {0x80402010u,0x80402010u,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0,0},
        {0x542a150bu,0x80402010u,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0,0x2c160b05u},
        {0x03010100u,0x72391d0eu,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0,0},
        {0x80402010u,0x80402010u,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0,0},
        {0x03010100u,0x72391d0eu,0x80402010u,0x6a351b0du,0x562b150bu,0x40201008u,0x2a150b05u,0x160b0503u,0x0e070302u,0x7d3f1f10u}
    };
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    ScratchArena scratch = {}; scratch_init(&scratch, arena);
    uint32_t source_pixels[33 * 35], output_pixels[33 * 37];
    ImageSurface source = {}, output = {};
    source.width = output.width = source.height = output.height = 33;
    source.pitch = 35 * 4; output.pitch = 37 * 4;
    source.pixels = source_pixels; output.pixels = output_pixels;
    for (unsigned mask = 0; mask < 2; mask++) for (unsigned axis = 0; axis < 2; axis++) {
        for (int row = 0; row < 33; row++) for (int column = 0; column < 35; column++) {
            int position = axis ? row : column;
            source_pixels[row * 35 + column] = position < 11 ? 0x80402010u : position < 22 ? 0x40201008u : 0;
            if (mask) source_pixels[row * 35 + column] = (source_pixels[row * 35 + column] >> 24) * 0x01010101u;
        }
        for (unsigned variant = 0; variant < 6; variant++) {
            for (uint32_t& pixel : output_pixels) pixel = 0xdeadbeefu;
            ASSERT_TRUE(render_gaussian_blur_axis(&scratch, &source, &output, 1.25f,
                axis, variant < 3 ? 0 : 3, variant < 3 ? 33 : 30, variant % 3));
            for (int line = 0; line < 33; line++) {
                for (unsigned sample = 0; sample < sizeof(points) / sizeof(points[0]); sample++) {
                    int index = axis ? points[sample] * 37 + line : line * 37 + points[sample];
                    uint32_t want = expected[variant][sample];
                    if (mask) want = (want >> 24) * 0x01010101u;
                    EXPECT_EQ(output_pixels[index], want) << mask << ':' << axis << ':' << variant << ':' << sample;
                }
                EXPECT_EQ(output_pixels[line * 37 + 33], 0xdeadbeefu);
            }
        }
    }
    arena_destroy(arena);
}

TEST(ClipGeometry, EllipticalCornersRetainTheSamePaintAndHitRegion) {
    Corner radius = {};
    for (size_t corner = 0; corner < 4; corner++) {
        radius.horizontal[corner] = 20.0f;
        radius.vertical[corner] = 10.0f;
    }
    Rect rect = {10, 20, 100, 60};
    const float points[][2] = {{1, 1}, {8, 4}, {99, 1}, {92, 4}, {99, 59}, {92, 56}, {1, 59}, {8, 56}};
    for (size_t index = 0; index < sizeof(points) / sizeof(*points); index++)
        EXPECT_EQ(clip_point_in_rounded_rect(10 + points[index][0], 20 + points[index][1], rect, &radius),
            index % 2 == 1) << index;
    EXPECT_TRUE(clip_point_in_rounded_rect(60, 50, rect, &radius));
    EXPECT_FALSE(clip_point_in_rounded_rect(9, 50, rect, &radius));
    EXPECT_FALSE(clip_point_in_rounded_rect(10, 20, {10, 20, 0, 0}, &radius));
}

TEST(ClipGeometry, ClipMarginOutsetsPreserveSquareAndSmallCorners) {
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT; element.set_synthetic(true);
    element.view_type = RDT_VIEW_BLOCK;
    ViewBlock* block = lam::view_as_block(&element);
    ASSERT_NE(block, nullptr);
    BoundaryProp boundary = {}; BorderProp border = {};
    ScrollProp scroll = SCROLL_PROP_DEFAULT;
    block->bound = lam::view_prop(&boundary); block->scroller = lam::view_prop(&scroll);
    boundary.border = lam::own(&border);
    block->width = block->height = 100.0f;
    scroll.has_clip = true; scroll.overflow_x = scroll.overflow_y = CSS_VALUE_CLIP;
    scroll.overflow_clip_box = CSS_VALUE_BORDER_BOX;
    scroll.clip = {0.0f, 0.0f, 100.0f, 100.0f};
    const struct { float radius, margin, expected, inside; } cases[] = {
        {0.0f, 10.0f, 0.0f, 1.0f},
        // CSS Backgrounds 3 section 4.2 includes the original corner's edge coverage.
        {4.0f, 20.0f, 13.765243f, 5.0f},
        {0.000001f, 20.0f, 0.000004f, 1.0f},
        {25.0f, 10.0f, 35.0f, 12.0f}
    };
    for (const auto& item : cases) {
        for (size_t corner = 0; corner < 4; corner++) {
            border.radius.horizontal[corner] = border.radius.vertical[corner] = item.radius;
        }
        scroll.overflow_clip_margin = item.margin;
        Bound clip; Corner radius;
        ASSERT_TRUE(render_clip_overflow_geometry(block, &clip, &radius));
        EXPECT_FLOAT_EQ(clip.left, -item.margin); EXPECT_FLOAT_EQ(clip.top, -item.margin);
        EXPECT_FLOAT_EQ(radius.top_left, item.expected);
        EXPECT_FLOAT_EQ(radius.top_left_y, item.expected);
        Rect rect = {clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top};
        EXPECT_TRUE(clip_point_in_rounded_rect(clip.left + item.inside,
            clip.top + item.inside, rect, &radius));
    }
    boundary.border = nullptr; block->bound = nullptr; block->scroller = nullptr;
}

TEST(RdtVectorTest, SharpRectangleAffineExpansionProducesClosedContours) {
    RdtPath* source = rdt_path_new(); RdtPath* expanded = rdt_path_new();
    ASSERT_NE(source, nullptr); ASSERT_NE(expanded, nullptr);
    rdt_path_add_rect(source, 3.5f, 5.0f, 8.0f, 4.0f, 0.0f, 0.0f);
    RdtMatrix matrix = rdt_matrix_identity(); matrix.e11 = -2.0f; matrix.e22 = 1.5f; matrix.e13 = 30.0f;
    ASSERT_TRUE(render_path_append_transformed(expanded, source, &matrix));
    Bound bound;
    ASSERT_TRUE(rdt_path_get_bounds(expanded, &bound.left, &bound.top, &bound.right, &bound.bottom));
    EXPECT_FLOAT_EQ(bound.left, 7.0f); EXPECT_FLOAT_EQ(bound.right, 23.0f);
    EXPECT_FLOAT_EQ(bound.top, 7.5f); EXPECT_FLOAT_EQ(bound.bottom, 13.5f);
    RdtPathMetrics metrics = {};
    ASSERT_TRUE(render_path_metrics_build(&metrics, source, &matrix));
    EXPECT_EQ(metrics.count, 4u); EXPECT_EQ(metrics.subpath_count, 1u); EXPECT_TRUE(metrics.closed);
    EXPECT_FLOAT_EQ(metrics.length, 44.0f);
    render_path_metrics_destroy(&metrics); rdt_path_free(source); rdt_path_free(expanded);
}

TEST(ClipGeometry, PaintContainmentClipsWithoutAllocatingAScrollPane) {
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    element.view_type = RDT_VIEW_BLOCK;
    ViewBlock* block = lam::view_as_block(&element);
    ASSERT_NE(block, nullptr);
    BlockProp properties = BLOCK_PROP_DEFAULT;
    BoundaryProp boundary = {}; BorderProp border = {};
    properties.contain_paint = true;
    block->blk = lam::view_prop(&properties);
    block->bound = lam::view_prop(&boundary);
    boundary.border = lam::own(&border);
    block->width = 100; block->height = 60;
    for (size_t side = 0; side < 4; side++) {
        border.width.values[side] = 5;
        border.radius.horizontal[side] = 20;
        border.radius.vertical[side] = 15;
    }
    Bound clip; Corner radius;
    ASSERT_TRUE(render_clip_overflow_geometry(block, &clip, &radius));
    EXPECT_FLOAT_EQ(clip.left, 5); EXPECT_FLOAT_EQ(clip.top, 5);
    EXPECT_FLOAT_EQ(clip.right, 95); EXPECT_FLOAT_EQ(clip.bottom, 55);
    EXPECT_FLOAT_EQ(radius.top_left, 15); EXPECT_FLOAT_EQ(radius.top_left_y, 10);
    EXPECT_EQ(block->scroller, nullptr);
    properties.contain_paint = false;
    EXPECT_FALSE(render_clip_overflow_geometry(block, &clip, &radius));
    boundary.border = nullptr;
    block->blk = nullptr; block->bound = nullptr;
}

TEST(TextInkBounds, DirtyAndViewportCullingPreserveOffBoxInkAndRestoreState) {
    for (float density : {1.0f, 2.0f}) {
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 40, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        char family[] = "Arial";
        FontProp style = FONT_PROP_DEFAULT; style.family = lam::up(family); style.font_size = 40;
        style.font_handle = lam::counted(font_resolve(fonts, &description)); ASSERT_NE(style.font_handle, nullptr);
        UiContext ui = {}; ui.font_ctx = lam::up(fonts); ui.device_scale = density;
        DomElement parent = {}, owner = {};
        parent.node_type = owner.node_type = DOM_NODE_ELEMENT;
        parent.set_synthetic(true); owner.set_synthetic(true);
        parent.view_type = owner.view_type = RDT_VIEW_BLOCK;
        owner.parent = lam::up<DomNode>(&parent); owner.x = 20; owner.y = -50;
        owner.width = 280; owner.height = 50;
        BlockProp properties = BLOCK_PROP_DEFAULT;
        owner.blk = lam::view_prop(&properties); owner.font = lam::view_prop(&style);
        DomText text = {}; text.node_type = DOM_NODE_TEXT; text.view_type = RDT_VIEW_TEXT;
        text.text = lam::up("ABC"); text.length = 3;
        text.parent = lam::up<DomNode>(&owner); text.font = lam::view_ref(&style);
        owner.first_child = lam::own<DomNode>(&text);
        TextRect rect = {}; rect.width = 84; rect.height = 50; rect.length = 3;
        text.rect = lam::view_prop(&rect);
        RasterRenderContext raster = {}; raster.ui_context = lam::up(&ui);
        raster.raster_scale = density; raster.block.clip = {0, 0, 320 * density, 100 * density};
        raster.has_dirty_union = true; raster.dirty_union = {30, 1, 40, 8};
        raster.font = {lam::up(&style), 40}; raster.color.c = 0xff0000ffu;
        Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr); scratch_init(&raster.scratch, arena);
        BlockBlot saved_block = raster.block; FontBox saved_font = raster.font;
        TextShadow shadow = {}; shadow.color.c = 0xff0000ffu; shadow.offset_y = 40; shadow.blur_radius = 4;
        for (unsigned variant = 0; variant < 3; variant++) {
            SCOPED_TRACE(variant);
            SCOPED_TRACE(density);
            style.text_emphasis_enabled = variant == 0; style.text_emphasis_mark = 0x2022;
            style.text_emphasis_under = true;
            style.text_deco = variant == 1 ? CSS_VALUE_UNDERLINE : CSS_VALUE_NONE;
            style.text_deco_extra = 0; style.text_deco_thickness = 3;
            style.text_underline_offset = 20; style.text_underline_offset_mode = 1;
            style.text_deco_skip_ink = CSS_VALUE_NONE;
            style.text_shadow = variant == 2 ? lam::own(&shadow) : nullptr;
            ViewBlock* block = lam::view_as_block(&owner);
            // the foreground border misses both regions while its independently visible ink does not.
            for (unsigned repeat = 0; repeat < 16; repeat++) {
                EXPECT_FALSE(render_block_viewport_misses(&raster, block));
                EXPECT_FALSE(render_block_dirty_misses(&raster, block));
                EXPECT_EQ(raster.font.style, saved_font.style);
                EXPECT_FLOAT_EQ(raster.block.x, saved_block.x); EXPECT_FLOAT_EQ(raster.block.y, saved_block.y);
                EXPECT_FLOAT_EQ(raster.block.clip.top, saved_block.clip.top);
            }
            if (variant == 1) {
                size_t soft, hard, critical; memtrack_get_limits(&soft, &hard, &critical);
                size_t usage = memtrack_get_current_usage(); memtrack_set_limits(usage, usage, usage + 64);
                bool viewport_misses = render_block_viewport_misses(&raster, block);
                bool dirty_misses = render_block_dirty_misses(&raster, block);
                memtrack_set_limits(soft, hard, critical);
                // bounds allocation failure must retain the subtree for the later paint attempt.
                EXPECT_FALSE(viewport_misses); EXPECT_FALSE(dirty_misses);
                EXPECT_EQ(raster.font.style, saved_font.style);
                EXPECT_FLOAT_EQ(raster.block.y, saved_block.y);
            }
            properties.contain_paint = true;
            EXPECT_TRUE(render_block_viewport_misses(&raster, block));
            EXPECT_TRUE(render_block_dirty_misses(&raster, block));
            properties.contain_paint = false;
        }
        style.text_shadow = nullptr; text.font = nullptr; text.rect = nullptr; text.parent = nullptr;
        owner.first_child = nullptr; owner.font = nullptr; owner.blk = nullptr; owner.parent = nullptr;
        scratch_release(&raster.scratch); arena_destroy(arena);
        font_handle_release(style.font_handle); style.font_handle = nullptr; font_context_destroy(fonts);
    }
}

TEST(TextOverflowPaint, ScopedProjectionPreservesSourceAndRestoresNestedState) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    font_context_add_scan_directory(fonts, "test/layout/data/font");
    FontStyleDesc description = {"Liberation Mono", 20, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontHandle* handle = font_resolve(fonts, &description);
    ASSERT_NE(handle, nullptr);
    FontProp font = FONT_PROP_DEFAULT;
    font.font_handle = lam::counted(handle); font.font_size = 20;
    FontBox box = {lam::up(&font), 20};
    UiContext ui = {}; ui.font_ctx = lam::up(fonts);
    RenderContext context = {}; context.ui_context = lam::up(&ui);
    context.font = box; context.color = {.r=20, .g=40, .b=60, .a=255};
    DomElement owner = {}, nested = {};
    owner.node_type = nested.node_type = DOM_NODE_ELEMENT;
    owner.set_synthetic(true); nested.set_synthetic(true);
    owner.view_type = nested.view_type = RDT_VIEW_BLOCK;
    BlockProp properties = BLOCK_PROP_DEFAULT;
    ScrollProp scroll = SCROLL_PROP_DEFAULT;
    properties.text_overflow = CSS_VALUE_ELLIPSIS; scroll.overflow_x = CSS_VALUE_HIDDEN;
    owner.blk = lam::view_prop(&properties); owner.scroller = lam::view_prop(&scroll);
    float advance = font_measure_char(handle, 'A');
    owner.width = advance * 4.25f + font_measure_char(handle, 0x2026);
    owner.height = 24;
    DomText text = {}; text.node_type = DOM_NODE_TEXT; text.view_type = RDT_VIEW_TEXT;
    text.text = lam::up("ÁBCDEFGHIJ"); text.length = strlen(text.text);
    text.parent = lam::up(&owner); owner.first_child = lam::own<DomNode>(&text);
    TextRect source = {}; source.width = advance * 10; source.height = 24;
    source.length = (int)text.length; // bounded fixture UTF-8 byte count
    text.rect = lam::view_prop(&source);
    const CssEnum directions[] = {CSS_VALUE_LTR, CSS_VALUE_RTL};
    for (CssEnum direction : directions) {
        bool rtl = direction == CSS_VALUE_RTL;
        properties.direction = direction;
        source.x = rtl ? owner.width - source.width : 0.0f;
        for (int iteration = 0; iteration < 256; iteration++) {
            {
                RenderTextPaintScope paint(&context, lam::view_as_block(&owner), &box, &context.color);
                ASSERT_NE(context.text_paint, nullptr);
                RenderTextPaintData* outer = context.text_paint;
                {
                    RenderTextPaintScope child(&context, lam::view_as_block(&nested), &box, &context.color);
                    EXPECT_EQ(context.text_paint, nullptr);
                }
                EXPECT_EQ(context.text_paint, outer);
                RenderTextPaintIterator iterator(&context, lam::view_require_text(&text));
                TextRect* projected = iterator.next(); ASSERT_NE(projected, nullptr);
                EXPECT_EQ(projected->length, rtl ? 4 : 6);
                if (projected->length != (rtl ? 4 : 6)) break;
                EXPECT_EQ(projected->start_index, rtl ? 8 : 0);
                EXPECT_EQ(projected->has_trailing_ellipsis, !rtl);
                lam::Temp<char> visible(render_text_create_export_segment(
                    text.text_data(), projected, CSS_VALUE_NONE, true));
                EXPECT_STREQ(visible.get(), rtl ? "GHIJ" : "ÁBCD…");
                if (rtl) {
                    float visible_x = projected->x;
                    TextRect* marker = iterator.next();
                    EXPECT_NE(marker, nullptr);
                    if (marker) {
                        EXPECT_EQ(marker->length, 0);
                        EXPECT_TRUE(marker->has_trailing_ellipsis);
                        EXPECT_NEAR(marker->x + marker->width, visible_x, 0.01f);
                    }
                }
                EXPECT_EQ(iterator.next(), nullptr);
            }
            EXPECT_EQ(context.text_paint, nullptr);
            EXPECT_EQ(context.font.style, &font);
            EXPECT_EQ(context.color.c, (Color{.r=20, .g=40, .b=60, .a=255}).c);
            EXPECT_EQ(source.length, (int)text.length);
            EXPECT_FLOAT_EQ(source.width, advance * 10);
            EXPECT_FLOAT_EQ(source.x, rtl ? owner.width - source.width : 0.0f);
            EXPECT_FALSE(source.has_trailing_ellipsis);
            EXPECT_EQ(source.next, nullptr);
        }
    }
    DomElement atom = {};
    atom.node_type = DOM_NODE_ELEMENT; atom.set_synthetic(true);
    atom.view_type = RDT_VIEW_INLINE_BLOCK;
    atom.display = {CSS_VALUE_INLINE_BLOCK, CSS_VALUE_FLOW_ROOT};
    atom.parent = lam::up(&owner); atom.x = advance * 4; atom.width = 20; atom.height = 20;
    text.next_sibling = lam::own<DomNode>(&atom);
    properties.direction = CSS_VALUE_LTR; properties.white_space = CSS_VALUE_NOWRAP;
    text.text = lam::up("ABCD"); text.length = 4;
    source.x = 0; source.length = 4; source.width = advance * 4;
    for (float width : {62.0f, 100.0f}) {
        owner.width = width;
        RenderTextPaintScope paint(&context, lam::view_as_block(&owner), &box, &context.color);
        EXPECT_EQ(render_text_paint_atomic_hidden(&context, &atom), width == 62.0f);
        RenderTextPaintIterator iterator(&context, lam::view_require_text(&text));
        TextRect* projected = iterator.next();
        ASSERT_NE(projected, nullptr);
        EXPECT_EQ(projected->length, 4);
        EXPECT_EQ(projected->has_trailing_ellipsis, width == 62.0f);
        EXPECT_EQ(source.length, 4);
        EXPECT_FALSE(source.has_trailing_ellipsis);
        EXPECT_FLOAT_EQ(atom.x, advance * 4); EXPECT_FLOAT_EQ(atom.width, 20);
    }
    text.next_sibling = nullptr; atom.parent = nullptr;
    properties.direction = CSS_VALUE_RTL;
    text.text = lam::up("אבגדהו"); text.length = strlen(text.text);
    source.length = (int)text.length; // INT_CAST_OK: bounded fixture UTF-8 byte count
    source.width = advance * 6;
    owner.width = advance * 4.25f + font_measure_char(handle, 0x2026);
    source.x = owner.width - source.width;
    ASSERT_TRUE(layout_inherited_direction_is_rtl(&text));
    ASSERT_TRUE(layout_text_run_has_uniform_rtl_order(text.text_data(), text.length));
    {
        RenderTextPaintScope paint(&context, lam::view_as_block(&owner), &box, &context.color);
        RenderTextPaintIterator iterator(&context, lam::view_require_text(&text));
        int glyphs = 0, markers = 0;
        while (TextRect* projected = iterator.next()) {
            if (projected->has_trailing_ellipsis) {
                EXPECT_EQ(projected->length, 0); markers++;
            } else {
                EXPECT_EQ(projected->start_index, glyphs * 2);
                EXPECT_EQ(projected->length, 2);
                EXPECT_NEAR(projected->x, owner.width - advance * (float)(glyphs + 1), 0.01f);
                glyphs++;
            }
        }
        EXPECT_EQ(glyphs, 4); EXPECT_EQ(markers, 1);
        EXPECT_EQ(source.start_index, 0); EXPECT_EQ(source.length, 12);
        EXPECT_FALSE(source.has_trailing_ellipsis);
    }
    text.rect = nullptr; owner.first_child = nullptr; text.parent = nullptr;
    owner.blk = nullptr; owner.scroller = nullptr;
    font_handle_release(handle); font_context_destroy(fonts);
}

TEST(TextOverflowPaint, SpacingProjectionKeepsComputedAdvancesAndCollapsedByteOffsets) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    font_context_add_scan_directory(fonts, "test/layout/data/font");
    FontStyleDesc description = {"Liberation Mono", 20, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontHandle* handle = font_resolve(fonts, &description);
    ASSERT_NE(handle, nullptr);
    FontProp font = FONT_PROP_DEFAULT;
    font.font_handle = lam::counted(handle); font.font_size = 20;
    float advance = font_measure_char(handle, 'a');
    font.space_width = font_measure_char(handle, ' ');
    font.word_spacing = 10;
    FontBox box = {lam::up(&font), 20};
    UiContext ui = {}; ui.font_ctx = lam::up(fonts);
    RenderContext context = {}; context.ui_context = lam::up(&ui); context.font = box;
    DomElement owner = {}; owner.node_type = DOM_NODE_ELEMENT; owner.set_synthetic(true);
    owner.view_type = RDT_VIEW_BLOCK; owner.width = 62; owner.height = 24;
    BlockProp properties = BLOCK_PROP_DEFAULT; properties.text_overflow = CSS_VALUE_ELLIPSIS;
    properties.white_space = CSS_VALUE_NOWRAP;
    ScrollProp scroll = SCROLL_PROP_DEFAULT; scroll.overflow_x = CSS_VALUE_HIDDEN;
    owner.blk = lam::view_prop(&properties); owner.scroller = lam::view_prop(&scroll);
    DomText text = {}; text.node_type = DOM_NODE_TEXT; text.view_type = RDT_VIEW_TEXT;
    text.parent = lam::up(&owner); owner.first_child = lam::own<DomNode>(&text);
    TextRect source = {}; source.width = advance * 8 + (font.space_width + 10) * 2;
    source.height = 24; text.rect = lam::view_prop(&source);
    const char* sources[] = {"ab cd efgh", "ab   cd efgh"};
    const CssEnum directions[] = {CSS_VALUE_LTR, CSS_VALUE_RTL};
    for (const char* input : sources) {
        text.text = lam::up(input); text.length = strlen(input);
        source.length = (int)text.length; // INT_CAST_OK: bounded fixture byte count.
        for (CssEnum direction : directions) {
            SCOPED_TRACE(input);
            SCOPED_TRACE(direction);
            properties.direction = direction;
            bool rtl = direction == CSS_VALUE_RTL;
            source.x = rtl ? owner.width - source.width : 0.0f;
            RenderTextPaintScope paint(&context, lam::view_as_block(&owner), &box, &context.color);
            RenderTextPaintIterator iterator(&context, lam::view_require_text(&text));
            TextRect* visible = iterator.next(); ASSERT_NE(visible, nullptr);
            EXPECT_EQ(visible->start_index, rtl ? (int)text.length - 4 : 0);
            EXPECT_EQ(visible->length, rtl ? 4 : (int)text.length - 7);
            // matching LTR styles combine the marker into the projected rectangle.
            EXPECT_NEAR(visible->width, rtl ? advance * 4 : advance * 2 + font.space_width +
                10 + font_measure_char(handle, 0x2026), .01f);
            EXPECT_NEAR(visible->x, rtl ? owner.width - advance * 4 : 0, .01f);
            EXPECT_EQ(source.length, (int)text.length);
            EXPECT_NEAR(source.width, advance * 8 + (font.space_width + 10) * 2, .01f);
        }
    }
    text.rect = nullptr; owner.first_child = nullptr; text.parent = nullptr;
    owner.blk = nullptr; owner.scroller = nullptr;
    font_handle_release(handle); font_context_destroy(fonts);
}

TEST(TextOverflowPaint, ExportSlicesKeepTheirWhitespaceProcessingContext) {
    const struct Case { const char* source; int start; int length; CssEnum white_space; CssEnum transform; const char* expected; } cases[] = {
        {"ab   cd", 0, 7, CSS_VALUE_NOWRAP, CSS_VALUE_NONE, "ab cd"},
        {"ab   cd", 0, 7, CSS_VALUE_PRE, CSS_VALUE_NONE, "ab   cd"},
        {"ab   cd", 3, 4, CSS_VALUE_NOWRAP, CSS_VALUE_NONE, "cd"},
        {"ab   cd", 3, 4, CSS_VALUE_PRE, CSS_VALUE_NONE, "  cd"},
        {"ab   cd", 0, 7, CSS_VALUE_NOWRAP, CSS_VALUE_UPPERCASE, "AB CD"},
        {"A\nB", 0, 2, CSS_VALUE_PRE, CSS_VALUE_NONE, "A"},
        // a DOM CR is a space before the preserved LF (CSS Text 3 §4).
        {"A\r\nB", 0, 3, CSS_VALUE_PRE_WRAP, CSS_VALUE_NONE, "A "},
        {"A \nB", 0, 3, CSS_VALUE_PRE_WRAP, CSS_VALUE_NONE, "A "},
        {"A\rB", 0, 3, CSS_VALUE_PRE, CSS_VALUE_NONE, "A B"},
        {"A B", 0, 3, CSS_VALUE_PRE, CSS_VALUE_NONE, "A B"},
        {"A\rB", 0, 3, CSS_VALUE_BREAK_SPACES, CSS_VALUE_NONE, "A B"},
        {"A\nB", 0, 2, CSS_VALUE_PRE_LINE, CSS_VALUE_NONE, "A"},
        {"A\nB", 0, 2, CSS_VALUE_BREAK_SPACES, CSS_VALUE_NONE, "A"},
        {"A\nB", 0, 2, CSS_VALUE_NORMAL, CSS_VALUE_NONE, "A "},
    };
    for (const Case& sample : cases) {
        SCOPED_TRACE(sample.source);
        TextRect slice = {}; slice.start_index = sample.start; slice.length = sample.length;
        lam::Temp<char> visible(render_text_create_export_segment(
            (const unsigned char*)sample.source, &slice, sample.transform, false, sample.white_space));
        EXPECT_STREQ(visible.get(), sample.expected);
        EXPECT_EQ(slice.start_index, sample.start); EXPECT_EQ(slice.length, sample.length);
    }
}

TEST(TextOverflowPaint, CapitalizedSlicesPreserveSourceWordBoundaries) {
    const struct { const char* source; int start; int length; const char* expected; } cases[] = {
        {"abcdefghij", 0, 4, "Abcd"},
        {"abcdefghij", 6, 4, "ghij"},
        {"abc ghij", 4, 4, "Ghij"},
        {"ághij", 3, 4, "ghij"},
        {"abc\tghij", 4, 4, "Ghij"},
    };
    for (const auto& sample : cases) {
        SCOPED_TRACE(sample.source);
        TextRect slice = {}; slice.start_index = sample.start; slice.length = sample.length;
        lam::Temp<char> visible(render_text_create_export_segment(
            (const unsigned char*)sample.source, &slice, CSS_VALUE_CAPITALIZE, true));
        EXPECT_STREQ(visible.get(), sample.expected);
    }
}

TEST(TextOverflowPaint, EffectGlyphBitmapsAndAdvancesUseLogicalPaintCoordinates) {
    for (float density : {1.0f, 2.0f}) {
        SCOPED_TRACE(density);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Liberation Mono", 20, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT;
        style.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(style.font_handle, nullptr);
        style.font_size = 20; style.space_width = 12; style.letter_spacing = 1;
        FontBox box = {lam::up(&style), 20};
        PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up("A A");
        run.text_len = 3; run.x = 5; run.baseline_y = 30; run.word_spacing = 2;
        Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
        DisplayList list = {}; dl_init(&list, arena);
        render_glyph_run_raster_lower(&run, &list);
        ASSERT_EQ(list.item_count(), 2);
        const DlDrawGlyph& first = list.data()[0].draw_glyph;
        const DlDrawGlyph& second = list.data()[1].draw_glyph;
        GlyphInfo glyph = font_get_glyph(style.font_handle, 'A');
        float expected_gap = glyph.advance_x + 1 + text_space_advance(&style, 2);
        float first_x = first.has_transform ? first.transform.e13 : (float)first.x;
        float second_x = second.has_transform ? second.transform.e13 : (float)second.x;
        EXPECT_FLOAT_EQ(second_x - first_x, roundf(5 + expected_gap) - 5);
        EXPECT_FLOAT_EQ(first.has_transform ? first.transform.e11 : 1, 1 / density);
        dl_destroy(&list); arena_destroy(arena);
        font_handle_release(style.font_handle); font_context_destroy(fonts);
    }
}

TEST(TextOverflowPaint, ColorGlyphShadowUsesAlphaCoverage) {
    uint8_t pixels[] = {0, 0, 255, 255, 0, 128, 0, 128};
    GlyphBitmap bitmap = {}; bitmap.buffer = pixels; bitmap.width = 2; bitmap.height = 1;
    bitmap.pitch = 8; bitmap.pixel_mode = GLYPH_PIXEL_BGRA; bitmap.bitmap_scale = 1.0f;
    ImageSurface* surface = image_surface_create(2, 1); ASSERT_NE(surface, nullptr);
    memset(surface->pixels, 255, (size_t)surface->pitch);
    DlDrawGlyph shadow = {}; shadow.bitmap = bitmap;
    shadow.clip = {0, 0, 2, 1}; shadow.color = Color{.r=0,.g=0,.b=0,.a=255};
    dl_replay_draw_glyph(surface, &shadow);
    const uint8_t* result = (const uint8_t*)surface->pixels;
    for (unsigned channel = 0; channel < 3; channel++) {
        EXPECT_EQ(result[channel], 0);
        EXPECT_EQ(result[4 + channel], 127);
    }
    image_surface_destroy(surface);
}

#ifdef __APPLE__
TEST(TextOverflowPaint, SelectedFaceShapingRejectsForeignGlyphIdsAndKeepsCacheNamespaces) {
    FontContextConfig config = {}; config.pixel_ratio = 1.0f;
    FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
    FontStyleDesc description = {"Arial", 16, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontHandle* font = font_resolve(fonts, &description); ASSERT_NE(font, nullptr);
    const GlyphBitmap* scalar = font_render_glyph(font, 'A', GLYPH_RENDER_NORMAL);
    ASSERT_NE(scalar, nullptr); GlyphBitmap expected = *scalar;
    const GlyphBitmap* indexed = font_render_glyph_index(font, 'A', GLYPH_RENDER_NORMAL);
    ASSERT_NE(indexed, nullptr);
    // the same integer names a different glyph in the Unicode and face-local namespaces.
    EXPECT_TRUE(indexed->width != expected.width || indexed->height != expected.height ||
        indexed->pitch != expected.pitch || memcmp(indexed->buffer, expected.buffer,
            (size_t)expected.pitch * expected.height) != 0);
    scalar = font_render_glyph(font, 'A', GLYPH_RENDER_NORMAL); ASSERT_NE(scalar, nullptr);
    EXPECT_EQ(scalar->width, expected.width); EXPECT_EQ(scalar->height, expected.height);
    EXPECT_EQ(memcmp(scalar->buffer, expected.buffer, (size_t)expected.pitch * expected.height), 0);

    struct Probe { int calls; uint32_t glyph; } probe = {};
    auto visit = [](void* pointer, uint32_t glyph, float, float, float) {
        Probe* value = (Probe*)pointer; value->calls++; value->glyph = glyph;
    };
    float advance = -1.0f;
    const char source[] = "🐕‍🦺";
    EXPECT_FALSE(font_visit_face_glyphs(font, source, sizeof(source) - 1, visit, &probe, &advance));
    EXPECT_EQ(probe.calls, 0); EXPECT_FLOAT_EQ(advance, -1.0f);
    FontHandle* emoji = font_resolve_for_emoji(fonts, &description, 0x1F415);
    ASSERT_NE(emoji, nullptr);
    ASSERT_TRUE(font_visit_face_glyphs(emoji, source, sizeof(source) - 1, visit, &probe, &advance));
    EXPECT_EQ(probe.calls, 1); EXPECT_FLOAT_EQ(advance, 20.0f);
    EXPECT_NE(probe.glyph, font_get_glyph_index(emoji, 0x1F415));
    const GlyphBitmap* joined = font_render_glyph_index(emoji, probe.glyph, GLYPH_RENDER_NORMAL);
    ASSERT_NE(joined, nullptr); EXPECT_EQ(joined->pixel_mode, GLYPH_PIXEL_BGRA);
    EXPECT_GT(joined->width, 0); EXPECT_GT(joined->height, 0);
    font_handle_release(emoji); font_handle_release(font); font_context_destroy(fonts);
}

TEST(TextOverflowPaint, GraphemeClustersPreserveShapedGlyphsAndFollowingTextAdvance) {
    struct Sample { const char* text; int cluster_glyphs; float advance; };
    const Sample samples[] = {
        {"🐕‍🦺M", 1, 20.0f}, {"👨‍👩‍👧‍👦M", 1, 20.0f},
        {"👩🏽‍💻M", 1, 20.0f}, {"🏳️‍🌈M", 1, 20.0f},
        {"🇸🇬M", 1, 20.0f}, {"1️⃣M", 1, 20.0f},
        {"❤️M", 1, 20.0f}, {"😀M", 1, 20.0f},
        {"🐕‍😀M", 2, 40.0f},
        {"e\u0301M", 1, 8.90625f}, {"A\u030AM", 1, 10.671875f},
        {"n\u0303M", 1, 8.90625f}, {"u\u0308\u0301M", 1, 8.90625f},
        {"ι\u0308\u0301M", 1, 3.5625f}, {"и\u0306M", 1, 8.9375f},
        {"=\u0338M", 1, 8.78125f}, {"ハ\u309AM", 1, 16.0f},
    };
    for (float density : {1.0f, 2.0f}) {
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 16, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT;
        style.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(style.font_handle, nullptr);
        style.font_size = 16.0f; style.letter_spacing = 3.0f;
        FontBox box = {lam::up(&style), 16.0f};
        FontProp replacement = FONT_PROP_DEFAULT;
        description.size_px = 32.0f;
        replacement.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(replacement.font_handle, nullptr);
        replacement.font_size = 32.0f;
        Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
        for (unsigned pass = 0; pass < 3; pass++) {
            if (pass == 1) font_context_reset_glyph_caches(fonts);
            for (const Sample& sample : samples) {
                SCOPED_TRACE(sample.text);
                SCOPED_TRACE(density);
                SCOPED_TRACE(pass);
                PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up(sample.text);
                run.text_len = (int)strlen(sample.text); run.x = 5.0f; run.baseline_y = 40.0f;
                DisplayList list = {}; dl_init(&list, arena);
                PaintList recorded;
                if (pass == 2) {
                    paint_glyph_run(&recorded, &run);
                    box = {lam::up(&replacement), 32.0f};
                    render_glyph_run_raster_lower(&recorded.data()[0].glyph_run, &list);
                    box = {lam::up(&style), 16.0f};
                } else render_glyph_run_raster_lower(&run, &list);
                // browser controls distinguish one ligature from an unsupported two-glyph composition.
                EXPECT_EQ(list.item_count(), sample.cluster_glyphs + 1);
                if (list.item_count() > 0) {
                    const DlDrawGlyph& marker = list.data()[list.item_count() - 1].draw_glyph;
                    EXPECT_EQ(marker.bitmap.pixel_mode, GLYPH_PIXEL_GRAY);
                    float pixel_to_css = render_font_pixel_to_css(style.font_handle, &marker.bitmap);
                    float actual_x = marker.has_transform ? marker.transform.e13 : (float)marker.x;
                    float expected_x = roundf(run.x + sample.advance + 3.0f +
                        (float)marker.bitmap.bearing_x * pixel_to_css);
                    EXPECT_FLOAT_EQ(actual_x, expected_x);
                }
                dl_destroy(&list);
            }
        }
        arena_destroy(arena);
        font_handle_release(replacement.font_handle);
        font_handle_release(style.font_handle); font_context_destroy(fonts);
    }
}
#endif

TEST(TextOverflowPaint, SynthesizedPunctuationSpaceKeepsLogicalWidthAcrossDensities) {
    for (float density : {1.0f, 2.0f}) {
        SCOPED_TRACE(density);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config);
        ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Ahem", 10.0f, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontHandle* handle = font_load_from_file(fonts, "test/layout/support/fonts/Ahem.ttf", &description);
        ASSERT_NE(handle, nullptr);
        float advance = 0.0f;
        ASSERT_TRUE(font_get_synthesized_space_advance(handle, 0x2008, &advance));
        EXPECT_FLOAT_EQ(advance, 10.0f);
        EXPECT_FALSE(font_get_synthesized_space_advance(handle, '.', &advance));
        FontProp style = FONT_PROP_DEFAULT;
        style.font_handle = lam::counted(handle);
        style.font_size = 10.0f;
        style.letter_spacing = 1.0f;
        FontBox font = {lam::up(&style), 10.0f};
        PaintGlyphRun run = {};
        run.font = lam::up(&font);
        run.text = lam::up("x\xE2\x80\x88y");
        run.text_len = 5;
        run.x = 5.0f;
        run.baseline_y = 20.0f;
        Arena* arena = arena_create_default();
        ASSERT_NE(arena, nullptr);
        DisplayList list = {}; dl_init(&list, arena);
        render_glyph_run_raster_lower(&run, &list);
        ASSERT_EQ(list.item_count(), 2);
        const DlDrawGlyph& first = list.data()[0].draw_glyph;
        const DlDrawGlyph& second = list.data()[1].draw_glyph;
        float first_x = first.has_transform ? first.transform.e13 : (float)first.x;
        float second_x = second.has_transform ? second.transform.e13 : (float)second.x;
        EXPECT_FLOAT_EQ(second_x - first_x, 22.0f);
        dl_destroy(&list);
        arena_destroy(arena);
        font_handle_release(handle);
        font_context_destroy(fonts);
    }
}

TEST(TextOverflowPaint, RecordedGlyphRunsRetainFontSelectionAfterPaintContextChanges) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
    FontStyleDesc description = {"Liberation Mono", 20, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontProp original = FONT_PROP_DEFAULT, replacement = FONT_PROP_DEFAULT;
    original.font_handle = lam::counted(font_resolve(fonts, &description));
    description.size_px = 30;
    replacement.font_handle = lam::counted(font_resolve(fonts, &description));
    original.font_size = 20; replacement.font_size = 30;
    ASSERT_NE(original.font_handle, nullptr); ASSERT_NE(replacement.font_handle, nullptr);
    FontBox current = {lam::up(&original), 20}, expected = current;
    PaintGlyphRun run = {}; run.font = lam::up(&current);
    run.text = lam::up("A"); run.text_len = 1; run.font_size = 20; run.baseline_y = 40;
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    PaintList paint = {}; paint_list_init(&paint, arena);
    paint_glyph_run(&paint, &run);
    current = {lam::up(&replacement), 30};
    // grow the command storage after switching the context; neither event may change earlier runs.
    for (int index = 0; index < 256; index++) paint_glyph_run(&paint, &run);
    ASSERT_EQ(paint.item_count(), 257);
    RdtPath* recorded = render_path_create_text_run(&paint.data()[0].glyph_run, fonts);
    ASSERT_NE(recorded, nullptr);
    run.font = lam::up(&expected);
    RdtPath* reference = render_path_create_text_run(&run, fonts);
    ASSERT_NE(reference, nullptr);
    float actual_left, actual_top, actual_right, actual_bottom;
    float expected_left, expected_top, expected_right, expected_bottom;
    ASSERT_TRUE(rdt_path_get_bounds(recorded, &actual_left, &actual_top, &actual_right, &actual_bottom));
    ASSERT_TRUE(rdt_path_get_bounds(reference, &expected_left, &expected_top, &expected_right, &expected_bottom));
    EXPECT_FLOAT_EQ(actual_left, expected_left); EXPECT_FLOAT_EQ(actual_top, expected_top);
    EXPECT_FLOAT_EQ(actual_right, expected_right); EXPECT_FLOAT_EQ(actual_bottom, expected_bottom);
    rdt_path_free(recorded); rdt_path_free(reference);
    paint_list_destroy(&paint); arena_destroy(arena);
    font_handle_release(original.font_handle); font_handle_release(replacement.font_handle);
    font_context_destroy(fonts);
}

class SvgAnimationLifetimeTest : public ::testing::Test {
protected:
    Pool* authored_pool = nullptr;
    Input* authored_owner = nullptr;
    DomDocument doc{};
    UiContext ui{};
    DomElement* root = nullptr;
    DomElement* svg = nullptr;

    DomElement* element(const char* tag, DomElement* parent = nullptr, Element* source = nullptr) {
        // authored SVG identity distinguishes DOM fixtures from layout-only boxes.
        if (!source) {
            Input* owner = authored_input();
            if (!owner) return nullptr;
            MarkBuilder builder(owner);
            source = builder.element(tag).final().element;
        }
        DomElement* node = DomElement::create(&doc, tag, source);
        if (node && parent && !static_cast<DomNode*>(parent)->append_child(node)) return nullptr;
        return node;
    }

    Input* authored_input() {
        if (!authored_pool) authored_pool = mem_pool_create(nullptr, MEM_ROLE_INPUT, "test.svg.animation.authored");
        if (authored_pool && !authored_owner) authored_owner = Input::create(authored_pool);
        return authored_owner;
    }

    void SetUp() override {
        ASSERT_TRUE(radiant_register_dom_engine_providers());
        Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
        ASSERT_TRUE(doc.init(owner));
        root = element("div"); ASSERT_NE(root, nullptr); doc.root = lam::up(root);
        svg = element("svg", root); ASSERT_NE(svg, nullptr);
        ASSERT_TRUE(svg->set_attribute("width", "200"));
        ASSERT_TRUE(svg->set_attribute("height", "200"));
        ui.document = lam::up(&doc); doc.js.host_ui_context = &ui;
        doc.js.host_driven_loop = true;
        ASSERT_NE(state_store_create(&doc), nullptr);
    }

    void TearDown() override {
        state_store_destroy(&doc); doc.destroy();
        mem_pool_destroy(authored_pool);
    }

    DomElement* animate(DomElement* target, const char* name, const char* from, const char* to) {
        DomElement* animation = element("animate", target);
        if (!animation || !animation->set_attribute("attributeName", name) ||
            !animation->set_attribute("attributeType", "XML") ||
            !animation->set_attribute("dur", "2s") || !animation->set_attribute("fill", "freeze") ||
            (from && !animation->set_attribute("from", from)) || !animation->set_attribute("to", to)) return nullptr;
        return animation;
    }

    uint64_t animation_allocations() {
        MemSnapshot* snapshot = mem_snapshot_capture(mem_context_root());
        uint64_t allocations = 0;
        if (snapshot) for (uint32_t i = 0; i < snapshot->count; i++)
            if (strcmp(snapshot->samples[i].label, "svg.animation.state") == 0)
                allocations += snapshot->samples[i].alloc_count;
        mem_snapshot_free(snapshot);
        return allocations;
    }
};

TEST_F(SvgAnimationLifetimeTest, PathGeometrySurvivesPaintPlacementAndSourceValueChanges) {
    MarkBuilder builder(authored_input());
    Item source = builder.element("svg").attr("xmlns", "http://www.w3.org/2000/svg")
        .attr("width", "200").attr("height", "200")
        .child(builder.element("path").attr("d", "M0 0 L20 0 L20 20 Z").attr("fill", "red").final()).final();
    svg = build_dom_tree_from_element(source.element, &doc, nullptr); ASSERT_NE(svg, nullptr);
    doc.root = lam::up(svg);
    DomElement* path = svg->first_child->as_element(); ASSERT_NE(path, nullptr);
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    RdtPathStorage* original = nullptr;
    for (int iteration = 0; iteration < 5; iteration++) {
        SCOPED_TRACE(iteration);
        if (iteration == 1) ASSERT_TRUE(path->set_attribute("fill", "blue"));
        if (iteration == 2) ASSERT_TRUE(svg->set_attribute("viewBox", "0 0 100 100"));
        if (iteration == 3) ASSERT_TRUE(path->set_attribute("d", "M0 0 L40 0 L40 20 Z"));
        if (iteration == 4) ASSERT_TRUE(path->set_attribute("d", "M0 0 L20 0 L20 20 Z"));
        DisplayList list; PaintList paint;
        RasterRenderContext context = {}; context.ui_context = lam::up(&ui);
        context.dl = lam::up(&list); context.paint_list = lam::up(&paint);
        context.raster_scale = 1; context.block.clip = {0, 0, 200, 200};
        context.block.x = (float)iteration;
        scratch_init(&context.scratch, arena);
        render_svg_begin_paint_pass();
        render_custom_svg_subscene(&context, dom_element_to_element(svg), 200, 200);
        const DlFillPath* fill = nullptr;
        for (size_t i = 0; i < list.size(); i++) if (list[i].op == DL_FILL_PATH) fill = &list[i].fill_path;
        ASSERT_NE(fill, nullptr) << "commands=" << list.size() << " parses=" << counters.path_parses;
        if (iteration == 0) original = fill->path->storage;
        if (iteration == 3) EXPECT_NE(fill->path->storage, original);
        else EXPECT_EQ(fill->path->storage, original);
        EXPECT_EQ(counters.path_parses, iteration < 3 ? 1u : 2u);
        EXPECT_EQ(counters.path_copy_bytes, 0u);
        dl_destroy(&list); paint_list_destroy(&paint); scratch_release(&context.scratch);
    }
    arena_destroy(arena);
}

TEST_F(SvgAnimationLifetimeTest, BasicShapeCacheSkipsConstructionForPaintAndPlacementChanges) {
    MarkBuilder builder(authored_input());
    Item source = builder.element("svg").attr("xmlns", "http://www.w3.org/2000/svg")
        .attr("width", "200").attr("height", "100")
        .child(builder.element("rect").attr("width", "50%").attr("height", "20")
            .attr("rx", "5").attr("fill", "red").final()).final();
    svg = build_dom_tree_from_element(source.element, &doc, nullptr); ASSERT_NE(svg, nullptr);
    doc.root = lam::up(svg);
    DomElement* rect = svg->first_child->as_element(); ASSERT_NE(rect, nullptr);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    RdtPathStorage* original = nullptr;
    for (int iteration = 0; iteration < 3; iteration++) {
        if (iteration == 1) ASSERT_TRUE(rect->set_attribute("fill", "blue"));
        if (iteration == 2) ASSERT_TRUE(rect->set_attribute("width", "75%"));
        DisplayList list; PaintList paint;
        RasterRenderContext context = {}; context.ui_context = lam::up(&ui);
        context.dl = lam::up(&list); context.paint_list = lam::up(&paint);
        context.raster_scale = 1; context.block.clip = {0, 0, 300, 100};
        context.block.x = (float)iteration;
        scratch_init(&context.scratch, arena);
        render_svg_begin_paint_pass();
        render_custom_svg_subscene(&context, dom_element_to_element(svg), 200, 100);
        const DlFillPath* fill = nullptr;
        for (size_t i = 0; i < list.size(); i++) if (list[i].op == DL_FILL_PATH) fill = &list[i].fill_path;
        ASSERT_NE(fill, nullptr);
        if (iteration == 0) original = fill->path->storage;
        if (iteration == 1) {
            EXPECT_EQ(fill->path->storage, original); EXPECT_EQ(counters.basic_shape_builds, 1u);
            EXPECT_EQ(counters.basic_shape_cache_hits, 1u);
        }
        if (iteration == 2) {
            EXPECT_NE(fill->path->storage, original); EXPECT_EQ(counters.basic_shape_builds, 2u);
        }
        dl_destroy(&list); paint_list_destroy(&paint); scratch_release(&context.scratch);
    }
    arena_destroy(arena);
}

TEST_F(SvgAnimationLifetimeTest, WallclockGrammarCalendarZonesAndFractionalSeconds) {
    const double origin = 946684800.25; // 2000-01-01T00:00:00.25Z
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(2000-01-01T00:00Z)", origin), -.25);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock( 2000-01-01T01:00:01.125+01:00 )", origin), .875);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(1999-12-31T23:00:00-01:00)", origin), -.25);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(2000-02-29T00:00Z)", origin), 59 * 86400.0 - .25);
    EXPECT_TRUE(isfinite(svg_animation_wallclock_value("wallclock(9999-12-31T23:59:59Z)", origin)));
    time_t epoch = (time_t)floor(origin);
    struct tm local = {};
#ifdef _WIN32
    ASSERT_EQ(localtime_s(&local, &epoch), 0);
#else
    ASSERT_NE(localtime_r(&epoch, &local), nullptr);
#endif
    char clock[64], value[96];
    ASSERT_GT(strftime(clock, sizeof(clock), "%H:%M:%S", &local), 0u);
    snprintf(value, sizeof(value), "wallclock(%s)", clock);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value(value, origin), -.25);
    ASSERT_GT(strftime(clock, sizeof(clock), "%Y-%m-%d", &local), 0u);
    snprintf(value, sizeof(value), "wallclock(%s)", clock);
    local.tm_hour = local.tm_min = local.tm_sec = 0; local.tm_isdst = -1;
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value(value, origin), (double)mktime(&local) - origin);
    const char* invalid[] = {"wallclock(2001-02-29T00:00Z)", "wallclock(2000-04-31)",
        "wallclock(2000-01-01t00:00Z)", "wallclock(2000-01-01T24:00Z)", "wallclock(00:00:60)",
        "wallclock(00:00:01.)", "wallclock(00:00 +01:00)", "wallclock(00:00+01)",
        "wallclock(2000-01-01Z)", "wallclock(2000-01-01T00:00Z)+1s", "wallclock()"};
    for (const char* value : invalid) EXPECT_TRUE(isnan(svg_animation_wallclock_value(value, origin))) << value;
}

TEST_F(SvgAnimationLifetimeTest, AccessKeysResolveOffsetsRestartAndEndAcrossFocusedFragments) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("x", "10"));
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("begin", "accessKey(\xc3\xa9)+0.5s;accessKey(a)"));
    ASSERT_TRUE(animation->set_attribute("end", "accessKey(z)"));
    svg_animation_pause(svg, true);
    dom_engine_svg_timing_key(root, "\xc3\xa9");
    svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "35");
    dom_engine_svg_timing_key(root, "z");
    svg_animation_set_time(svg, 2);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "35");
    dom_engine_svg_timing_key(root, "a");
    svg_animation_set_time(svg, 3);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    ASSERT_TRUE(animation->set_attribute("restart", "whenNotActive"));
    dom_engine_svg_timing_key(root, "a");
    svg_animation_set_time(svg, 3.5);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "85");
}

TEST_F(SvgAnimationLifetimeTest, AnimationQueriesUseResolvedIntervalsAndLiveTarget) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("begin", "2s;6s"));
    svg_animation_pause(svg, true);
    double start = -1;
    EXPECT_TRUE(svg_animation_start_time(animation, &start)); EXPECT_DOUBLE_EQ(start, 2);
    EXPECT_DOUBLE_EQ(svg_animation_simple_duration(animation), 2);
    EXPECT_EQ(svg_animation_target_element(animation), rect);
    svg_animation_set_time(svg, 4);
    EXPECT_TRUE(svg_animation_start_time(animation, &start)); EXPECT_DOUBLE_EQ(start, 6);
    svg_animation_set_time(svg, 8);
    EXPECT_FALSE(svg_animation_start_time(animation, &start));
    ASSERT_TRUE(animation->set_attribute("href", "#absent"));
    EXPECT_EQ(svg_animation_target_element(animation), nullptr);
    ASSERT_TRUE(animation->set_attribute("dur", "indefinite"));
    EXPECT_TRUE(isnan(svg_animation_simple_duration(animation)));
}

TEST_F(SvgAnimationLifetimeTest, ExpiredNegativeIntervalsDoNotFreezeOrFeedSyncbases) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* expired = animate(rect, "x", "10", "110"); ASSERT_NE(expired, nullptr);
    ASSERT_TRUE(expired->set_attribute("id", "expired"));
    ASSERT_TRUE(expired->set_attribute("begin", "-3s"));
    DomElement* dependent = animate(rect, "y", "10", "110"); ASSERT_NE(dependent, nullptr);
    ASSERT_TRUE(dependent->set_attribute("begin", "expired.end+2s"));
    svg_animation_pause(svg, true);
    svg_animation_set_time(svg, 1.5);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    EXPECT_EQ(svg_animation_value(rect, "y"), nullptr);
    double start = 0;
    EXPECT_FALSE(svg_animation_start_time(expired, &start));
    ASSERT_TRUE(expired->set_attribute("begin", "-1s"));
    svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    ASSERT_TRUE(expired->set_attribute("begin", "-3s;1s"));
    svg_animation_set_time(svg, 2);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    EXPECT_TRUE(svg_animation_start_time(expired, &start)); EXPECT_DOUBLE_EQ(start, 1);
}

TEST_F(SvgAnimationLifetimeTest, UseControlsIsolateImplicitEventsShareQualifiedIdsAndReleaseTheirOwners) {
    DomElement* defs = element("defs", svg); ASSERT_NE(defs, nullptr);
    DomElement* prototype = element("g", defs); ASSERT_NE(prototype, nullptr);
    ASSERT_TRUE(prototype->set_attribute("id", "prototype"));
    DomElement* rect = element("rect", prototype); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("id", "source"));
    DomElement* implicit = animate(rect, "x", "10", "110"); ASSERT_NE(implicit, nullptr);
    ASSERT_TRUE(implicit->set_attribute("begin", "click"));
    DomElement* qualified = animate(rect, "y", "10", "110"); ASSERT_NE(qualified, nullptr);
    ASSERT_TRUE(qualified->set_attribute("begin", "source.click"));
    DomElement* first = element("use", svg); ASSERT_NE(first, nullptr);
    DomElement* second = element("use", svg); ASSERT_NE(second, nullptr);
    svg_animation_pause(svg, true);
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_EQ(svg_animation_value(rect, "x"), nullptr); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_EQ(svg_animation_value(rect, "x"), nullptr); }
    svg_animation_use_event(first, rect, "click", true, 0);
    svg_animation_set_time(svg, 1);
    {
        SvgAnimationSourceScope scope(&doc, prototype, first);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
        EXPECT_STREQ(svg_animation_value(rect, "y"), "60");
    }
    {
        SvgAnimationSourceScope scope(&doc, prototype, second);
        EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
        EXPECT_STREQ(svg_animation_value(rect, "y"), "60");
    }
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    uint64_t retained = animation_allocations();
    svg_animation_forget_source_document(&doc, &doc);
    EXPECT_LT(animation_allocations(), retained);
    EXPECT_EQ(svg_animation_use_source(first), nullptr);
    EXPECT_EQ(svg_animation_use_source(second), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, UseRepeatEventsDriveQualifiedDependentsOnTheHostClock) {
    DomElement* defs = element("defs", svg); ASSERT_NE(defs, nullptr);
    DomElement* prototype = element("g", defs); ASSERT_NE(prototype, nullptr);
    DomElement* rect = element("rect", prototype); ASSERT_NE(rect, nullptr);
    DomElement* pulse = animate(rect, "x", "10", "110"); ASSERT_NE(pulse, nullptr);
    ASSERT_TRUE(pulse->set_attribute("id", "pulse"));
    ASSERT_TRUE(pulse->set_attribute("begin", "click"));
    ASSERT_TRUE(pulse->set_attribute("dur", "1s"));
    ASSERT_TRUE(pulse->set_attribute("repeatCount", "2"));
    DomElement* dependent = animate(rect, "y", "10", "110"); ASSERT_NE(dependent, nullptr);
    ASSERT_TRUE(dependent->set_attribute("begin", "pulse.repeat(1)"));
    DomElement* first = element("use", svg); ASSERT_NE(first, nullptr);
    DomElement* second = element("use", svg); ASSERT_NE(second, nullptr);
    svg_animation_prepare(svg);
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_EQ(svg_animation_value(rect, "y"), nullptr); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_EQ(svg_animation_value(rect, "y"), nullptr); }
    svg_animation_use_event(first, rect, "click", true, 0);
    ASSERT_TRUE(animation_scheduler_tick(doc.state->animation_scheduler, 1.25, nullptr));
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_STREQ(svg_animation_value(rect, "y"), "22.5"); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_STREQ(svg_animation_value(rect, "y"), "22.5"); }
}

TEST_F(SvgAnimationLifetimeTest, ClockSurvivesLayoutReleaseAndStopsOnPauseDetachAndDocumentTeardown) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("x", "10"));
    ASSERT_NE(animate(rect, "x", "10", "110"), nullptr);
    svg_animation_prepare(svg);
    AnimationScheduler* scheduler = doc.state->animation_scheduler;
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_TRUE(animation_scheduler_tick(scheduler, 1.0, nullptr));
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    EXPECT_STREQ(rect->get_attribute("x"), "10");
    animation_scheduler_remove_views(scheduler);
    EXPECT_EQ(scheduler->count, 1);
    svg_animation_pause(svg, true);
    EXPECT_EQ(scheduler->count, 0);
    svg_animation_set_time(svg, 1.5);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "85");
    svg_animation_pause(svg, false);
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_TRUE(animation_scheduler_tick(scheduler, 1.25, nullptr));
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 1.75);
    ASSERT_TRUE(root->remove_child(svg));
    EXPECT_FALSE(animation_scheduler_tick(scheduler, 1.5, nullptr));
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    svg_animation_prepare(svg);
    EXPECT_EQ(scheduler->count, 0); // a detached pinned tree must not restart its driver.
    ASSERT_TRUE(root->append_child(svg));
    svg_animation_prepare(svg);
    ASSERT_EQ(scheduler->count, 1);
    // D4.2.6: scheduler teardown releases its clock before document resources disappear.
    state_store_destroy(&doc);
    doc.destroy();
    EXPECT_EQ(doc.services.svg_animation_registry, nullptr);
    EXPECT_EQ(doc.state, nullptr);
}

TEST_F(SvgAnimationLifetimeTest, HtmlBoundariesOwnIndependentFragmentClocksAndPrivateDocumentsAdvanceAllFragments) {
    DomElement* foreign = element("foreignObject", svg); ASSERT_NE(foreign, nullptr);
    DomElement* html = element("div", foreign); ASSERT_NE(html, nullptr); ASSERT_FALSE(dom_element_is_svg(html));
    DomElement* inner = element("svg", html); ASSERT_NE(inner, nullptr);
    DomElement* nested = element("svg", svg); ASSERT_NE(nested, nullptr);
    DomElement* outer_shape = element("rect", svg); ASSERT_NE(outer_shape, nullptr);
    DomElement* inner_shape = element("rect", inner); ASSERT_NE(inner_shape, nullptr);
    DomElement* nested_shape = element("rect", nested); ASSERT_NE(nested_shape, nullptr);
    DomElement* shapes[] = {outer_shape, inner_shape, nested_shape};
    for (DomElement* target : shapes)
        ASSERT_NE(animate(target, "x", "10", "110"), nullptr);
    EXPECT_TRUE(svg_animation_has_elements(foreign));
    svg_animation_prepare(svg); svg_animation_pause(svg, true); svg_animation_pause(inner, true);
    svg_animation_set_time(inner, 2);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 0);
    EXPECT_STREQ(svg_animation_value(outer_shape, "x"), "10");
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "110");
    svg_animation_set_time(svg, 1);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(nested), 1);
    svg_animation_set_time(nested, 2);
    svg_animation_pause(nested, false);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 1);
    EXPECT_TRUE(svg_animation_paused(svg));
    EXPECT_STREQ(svg_animation_value(nested_shape, "x"), "60");
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "110");
    svg_animation_set_document_time(&doc, .5);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), .5);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(inner), .5);
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "35");
}

TEST_F(SvgAnimationLifetimeTest, ComponentTransferTablesAreOwnedBoundedAndRetiredAcrossMutation) {
    MarkBuilder builder(authored_input());
    Item source = builder.element("svg").attr("xmlns", "http://www.w3.org/2000/svg")
        .child(builder.element("filter").attr("id", "transfer")
            .child(builder.element("feComponentTransfer")
                .child(builder.element("feFuncR").attr("type", "linear").attr("slope", "0").final())
                .child(builder.element("feFuncR").attr("type", "table").attr("tableValues", "0 .25 1").final())
                .final()).final()).final();
    svg = build_dom_tree_from_element(source.element, &doc, nullptr); ASSERT_NE(svg, nullptr);
    doc.root = lam::up(svg);
    DomElement* filter = svg->first_child->as_element(); ASSERT_NE(filter, nullptr);
    DomElement* primitive = filter->first_child->as_element(); ASSERT_NE(primitive, nullptr);
    DomElement* channel = primitive->last_child->as_element(); ASSERT_NE(channel, nullptr);
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterProgram* original = render_css_svg_filter_compile(&doc, "#transfer", &scratch);
    ASSERT_NE(original, nullptr); ASSERT_TRUE(original->valid); ASSERT_EQ(original->count, 1u);
    ASSERT_TRUE(original->nodes[0].valid);
    const RdtSvgTransferFunction& old = original->nodes[0].transfer[0];
    EXPECT_EQ(old.kind, RDT_SVG_TRANSFER_TABLE); ASSERT_EQ(old.count, 3u);
    EXPECT_FLOAT_EQ(old.table[1], .25f);
    ASSERT_TRUE(channel->set_attribute("tableValues", "1 0"));
    dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, channel, primitive);
    RdtSvgFilterProgram* changed = render_css_svg_filter_compile(&doc, "#transfer", &scratch);
    ASSERT_NE(changed, nullptr); EXPECT_NE(changed, original);
    ASSERT_TRUE(changed->nodes[0].valid); ASSERT_EQ(changed->nodes[0].transfer[0].count, 2u);
    EXPECT_FLOAT_EQ(changed->nodes[0].transfer[0].table[0], 1.0f);
    // pinned programs retain their tables after the authored attribute and active program change.
    EXPECT_FLOAT_EQ(old.table[1], .25f);
    render_svg_filter_program_release(changed);
    StrBuf* oversized = strbuf_new(); ASSERT_NE(oversized, nullptr);
    for (size_t index = 0; index <= RDT_SVG_FILTER_MAX_TRANSFER_VALUES; index++) strbuf_append_str(oversized, "0 ");
    ASSERT_TRUE(channel->set_attribute("tableValues", oversized->str));
    dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, channel, primitive);
    strbuf_free(oversized);
    changed = render_css_svg_filter_compile(&doc, "#transfer", &scratch);
    ASSERT_NE(changed, nullptr); EXPECT_FALSE(changed->nodes[0].valid);
    render_svg_filter_program_release(changed);
    ASSERT_TRUE(channel->set_attribute("tableValues", "0 invalid 1"));
    dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, channel, primitive);
    changed = render_css_svg_filter_compile(&doc, "#transfer", &scratch);
    ASSERT_NE(changed, nullptr); EXPECT_FALSE(changed->nodes[0].valid);
    render_svg_filter_program_release(changed); render_svg_filter_program_release(original);
    scratch_release(&scratch); arena_destroy(arena);
}

TEST_F(SvgAnimationLifetimeTest, FilterProgramsTrackClockChangesWithoutDomMutation) {
    DomElement* offset = element("feOffset", svg); ASSERT_NE(offset, nullptr);
    ASSERT_NE(animate(offset, "dx", "0", "100"), nullptr);
    svg_animation_prepare(svg); svg_animation_pause(svg, true);
    Element filter = {};
    uint64_t epoch = doc.mutation_epoch;
    RdtSvgFilterProgram* before = render_svg_filter_program_acquire(&doc, &filter);
    ASSERT_NE(before, nullptr); before->compiled = true;
    EXPECT_EQ(render_svg_filter_program_acquire(&doc, &filter), before);
    render_svg_filter_program_release(before);
    svg_animation_set_time(svg, 1);
    EXPECT_EQ(doc.mutation_epoch, epoch);
    RdtSvgFilterProgram* after = render_svg_filter_program_acquire(&doc, &filter);
    ASSERT_NE(after, nullptr); EXPECT_NE(after, before); EXPECT_FALSE(after->compiled);
    EXPECT_TRUE(before->compiled); // pinned callers keep their earlier immutable facts.
    EXPECT_NE(after->animation_generation, before->animation_generation);
    render_svg_filter_program_release(before); render_svg_filter_program_release(after);
}

TEST_F(SvgAnimationLifetimeTest, FilterProgramsSeparateAnimatedInstancesFromStaticReferences) {
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("g").final();
    DomElement* group = element("g", svg, source.element); ASSERT_NE(group, nullptr);
    source = builder.element("filter").final();
    Element* filter_source = source.element;
    DomElement* filter = element("filter", group, filter_source); ASSERT_NE(filter, nullptr);
    DomElement* flood = element("feFlood", filter); ASSERT_NE(flood, nullptr);
    ASSERT_NE(animate(flood, "flood-color", "red", "blue"), nullptr);
    DomElement* outside = element("g", svg); ASSERT_NE(outside, nullptr);
    svg_animation_mark_reference(svg); svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    RdtSvgFilterProgram* base = render_svg_filter_program_acquire(&doc, filter_source); ASSERT_NE(base, nullptr);
    RdtSvgFilterProgram* sampled = nullptr;
    {
        SvgAnimationSourceScope instance(&doc, group);
        sampled = render_svg_filter_program_acquire(&doc, filter_source); ASSERT_NE(sampled, nullptr);
        EXPECT_NE(sampled, base); EXPECT_GT(sampled->animation_generation, 0u);
        EXPECT_STREQ(svg_animation_value(flood, "flood-color"), "rgba(127.5,0,127.5,1)");
    }
    {
        SvgAnimationSourceScope static_resource(&doc, outside);
        RdtSvgFilterProgram* same = render_svg_filter_program_acquire(&doc, filter_source);
        EXPECT_EQ(same, base); EXPECT_EQ(same->animation_generation, 0u);
        render_svg_filter_program_release(same);
    }
    render_svg_filter_program_release(sampled); render_svg_filter_program_release(base);
}

TEST_F(SvgAnimationLifetimeTest, TypedSamplesResolveLengthListsPairsAnglesAndIntegerRounding) {
    DomElement* turbulence = element("feTurbulence", svg); ASSERT_NE(turbulence, nullptr);
    ASSERT_NE(animate(turbulence, "numOctaves", "1", "4"), nullptr);
    DomElement* blur = element("feGaussianBlur", svg); ASSERT_NE(blur, nullptr);
    ASSERT_NE(animate(blur, "stdDeviation", "2", "6 10"), nullptr);
    DomElement* marker = element("marker", svg); ASSERT_NE(marker, nullptr);
    ASSERT_NE(animate(marker, "orient", "0deg", "0.5turn"), nullptr);
    DomElement* text = element("text", svg); ASSERT_NE(text, nullptr);
    ASSERT_NE(animate(text, "x", "10px 20px", "1in 2in"), nullptr);
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_NE(animate(rect, "x", nullptr, "100"), nullptr);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(turbulence, "numOctaves"), "3");
    EXPECT_STREQ(svg_animation_value(blur, "stdDeviation"), "4 6");
    EXPECT_STREQ(svg_animation_value(marker, "orient"), "90");
    EXPECT_STREQ(svg_animation_value(text, "x"), "53 106");
    EXPECT_STREQ(svg_animation_value(rect, "x"), "50");
    EXPECT_EQ(rect->get_attribute("x"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, ToAnimationsUseResourceAndFilterInitialValues) {
    struct Case { const char* tag; const char* name; const char* to; const char* expected; };
    const Case cases[] = {
        {"marker", "orient", "90", "45"}, {"marker", "markerWidth", "9", "6"},
        {"linearGradient", "x2", "0%", "50%"}, {"radialGradient", "fx", "100%", "75%"},
        {"feTurbulence", "numOctaves", "5", "3"}, {"feGaussianBlur", "stdDeviation", "4", "2 2"},
        {"feMorphology", "radius", "4 6", "2 3"}, {"feFuncR", "slope", "3", "2"},
        {"filter", "x", "10%", "0%"}, {"filter", "width", "80%", "100%"},
        {"feFlood", "width", "80%", "90%"}, {"feOffset", "x", "10%", "5%"},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        ASSERT_NE(animate(targets[i], cases[i].name, nullptr, cases[i].to), nullptr);
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].expected) << cases[i].name;
}

TEST_F(SvgAnimationLifetimeTest, DiscreteToUsesSvgHalfwayAndKeyTimeBoundaries) {
    DomElement* normal = element("rect", svg); ASSERT_NE(normal, nullptr);
    DomElement* keyed = element("rect", svg); ASSERT_NE(keyed, nullptr);
    DomElement* targets[] = {normal, keyed};
    for (DomElement* target : targets) {
        ASSERT_TRUE(target->set_attribute("x", "10"));
        DomElement* animation = animate(target, "x", nullptr, "110"); ASSERT_NE(animation, nullptr);
        ASSERT_TRUE(animation->set_attribute("calcMode", "discrete"));
        if (target == keyed) ASSERT_TRUE(animation->set_attribute("keyTimes", "0;.75"));
    }
    svg_animation_pause(svg, true);
    struct Case { double time; const char* normal; const char* keyed; };
    const Case cases[] = {{0, "10", "10"}, {.5, "10", "10"}, {1, "110", "10"},
        {1.5, "110", "110"}, {2, "110", "110"}, {0, "10", "10"}};
    for (const Case& sample : cases) {
        svg_animation_set_time(svg, sample.time);
        EXPECT_STREQ(svg_animation_value(normal, "x"), sample.normal);
        EXPECT_STREQ(svg_animation_value(keyed, "x"), sample.keyed);
    }
}

TEST_F(SvgAnimationLifetimeTest, IndefiniteDurationIgnoresKeyTimesAndKeepsFirstValue) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "20", "100"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("dur", "indefinite"));
    ASSERT_TRUE(animation->set_attribute("keyTimes", ".5;1"));
    ASSERT_TRUE(animation->set_attribute("end", "2s"));
    svg_animation_pause(svg, true);
    const double times[] = {0.0, 1.0, 2.0, 3.0};
    for (double time : times) {
        svg_animation_set_time(svg, time);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "20");
    }
}

TEST_F(SvgAnimationLifetimeTest, DiscreteToUsesAbsentXmlEnumInitialValues) {
    struct Case { const char* tag; const char* name; const char* to; const char* initial; };
    const Case cases[] = {
        {"linearGradient", "spreadMethod", "repeat", "pad"},
        {"pattern", "patternUnits", "userSpaceOnUse", "objectBoundingBox"},
        {"marker", "markerUnits", "userSpaceOnUse", "strokeWidth"},
        {"svg", "preserveAspectRatio", "none", "xMidYMid meet"},
        {"feComposite", "operator", "xor", "over"},
        {"feTurbulence", "type", "fractalNoise", "turbulence"},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        ASSERT_NE(animate(targets[i], cases[i].name, nullptr, cases[i].to), nullptr);
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, .5);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].initial);
    svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].to);
        EXPECT_EQ(targets[i]->get_attribute(cases[i].name), nullptr);
    }
}

TEST_F(SvgAnimationLifetimeTest, RelativeLengthsRetainFontDependencyAndOpacityPercentagesStayDimensionless) {
    DomElement* parent = element("g", svg); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("font-size", "20"));
    DomElement* font = animate(parent, "font-size", "20", "40"); ASSERT_NE(font, nullptr);
    ASSERT_TRUE(font->set_attribute("begin", "0.5s")); ASSERT_TRUE(font->set_attribute("dur", "1s"));
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr); MarkBuilder builder(owner);
    Item source = builder.element("rect").attr("width", "1em").attr("height", "20").final();
    DomElement* rect = element("rect", parent, source.element); ASSERT_NE(rect, nullptr);
    ASSERT_NE(animate(rect, "width", "1em", "2em"), nullptr);
    ASSERT_NE(animate(rect, "opacity", "100%", "0.5"), nullptr);
    DomElement* path = element("path", parent); ASSERT_NE(path, nullptr);
    ASSERT_NE(animate(path, "stroke-dasharray", "1em 1em", "2em 2em"), nullptr);
    source = builder.element("rect").attr("width", "1em").attr("height", "20").final();
    DomElement* mixed = element("rect", parent, source.element); ASSERT_NE(mixed, nullptr);
    ASSERT_NE(animate(mixed, "width", "1em", "80px"), nullptr);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(rect, "width"), "1.5em");
    EXPECT_STREQ(svg_animation_value(path, "stroke-dasharray"), "1.5em 1.5em");
    EXPECT_STREQ(svg_animation_value(rect, "opacity"), "0.75");
    float left, top, right, bottom;
    ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 45);
    ASSERT_TRUE(dom_svg_element_geometry_bounds(mixed, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 55);
    svg_animation_set_time(svg, 2);
    ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 80);
    EXPECT_STREQ(svg_animation_value(rect, "opacity"), "0.5");
}

TEST_F(SvgAnimationLifetimeTest, ToAnimationsUseAbsentIriStopAndColorMatrixInitialValues) {
    DomElement* use = element("use", svg); ASSERT_NE(use, nullptr);
    ASSERT_NE(animate(use, "href", nullptr, "#shape"), nullptr);
    DomElement* stop = element("stop", svg); ASSERT_NE(stop, nullptr);
    ASSERT_NE(animate(stop, "offset", nullptr, "1"), nullptr);
    DomElement* matrix = element("feColorMatrix", svg); ASSERT_NE(matrix, nullptr);
    ASSERT_NE(animate(matrix, "values", nullptr, "0 0 1 0 0 0 1 0 0 0 1 0 0 0 0 0 0 0 1 0"), nullptr);
    DomElement* typed = element("feColorMatrix", svg); ASSERT_NE(typed, nullptr);
    ASSERT_TRUE(typed->set_attribute("type", "saturate"));
    ASSERT_NE(animate(typed, "values", nullptr, "180"), nullptr);
    DomElement* type = animate(typed, "type", nullptr, "hueRotate"); ASSERT_NE(type, nullptr);
    ASSERT_TRUE(type->set_attribute("begin", "0.1s"));
    ASSERT_TRUE(type->set_attribute("dur", "0.2s"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(use, "href"), "");
    EXPECT_STREQ(svg_animation_value(matrix, "values"), "1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 0");
    svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(use, "href"), "#shape");
    EXPECT_STREQ(svg_animation_value(stop, "offset"), "0.5");
    EXPECT_STREQ(svg_animation_value(matrix, "values"), "0.5 0 0.5 0 0 0 1 0 0 0 0.5 0 0.5 0 0 0 0 0 1 0");
    // the default values list follows the sampled type, regardless of animation begin priority.
    EXPECT_STREQ(svg_animation_value(typed, "type"), "hueRotate");
    EXPECT_STREQ(svg_animation_value(typed, "values"), "90");
    EXPECT_EQ(use->get_attribute("href"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, InvalidPathStructuresAndAttributeTypesDoNotReplaceBaseValues) {
    DomElement* path = element("path", svg); ASSERT_NE(path, nullptr);
    ASSERT_NE(animate(path, "d", "M0 0 L20 0", "M0 0 Q10 20 20 0"), nullptr);
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("attributeType", "invalid"));
    DomElement* spaced = element("rect", svg); ASSERT_NE(spaced, nullptr);
    animation = animate(spaced, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("attributeType", " XML "));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(path, "d"), nullptr);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    EXPECT_STREQ(svg_animation_value(spaced, "x"), "60");
}

TEST_F(SvgAnimationLifetimeTest, RetiredControlsArePrunedWhileDetachedPinnedControlsSurvive) {
    // authored nodes retire through the lifecycle registry; synthetic layout nodes have a different owner.
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("rect").final();
    DomElement* rect = element("rect", svg, source.element); ASSERT_NE(rect, nullptr);
    source = builder.element("animate").attr("attributeName", "x").attr("attributeType", "XML")
        .attr("from", "10").attr("to", "110").attr("dur", "2s").attr("fill", "freeze")
        .attr("begin", "indefinite").final();
    DomElement* animation = element("animate", rect, source.element); ASSERT_NE(animation, nullptr);
    svg_animation_pause(svg, true);
    ASSERT_TRUE(svg_animation_begin_end(animation, false, 0));
    svg_animation_set_time(svg, 1);
    ASSERT_STREQ(svg_animation_value(rect, "x"), "60");
    uint64_t attached = animation_allocations(); ASSERT_GT(attached, 0u);
    DomNodeRef ref = dom_node_ref(animation);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_EXTERNAL));
    ASSERT_TRUE(svg->remove_child(rect));
    dom_retire_sweep(&doc);
    ASSERT_NE(dom_node_ref_validate(&doc, ref), nullptr);
    svg_animation_set_time(svg, 1.25); svg_animation_value(rect, "x");
    uint64_t detached = animation_allocations();
    ASSERT_TRUE(svg->append_child(rect));
    // native synthetic links have no JS mutation notifier; a clock sample rebuilds their paint facts.
    svg_animation_set_time(svg, 1.5);
    ASSERT_STREQ(svg_animation_value(rect, "x"), "85");
    ASSERT_TRUE(svg->remove_child(rect));
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_EXTERNAL));
    EXPECT_GT(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    svg_animation_set_time(svg, 1.75);
    svg_animation_value(svg, "width");
    EXPECT_LT(animation_allocations(), detached);
}

TEST_F(SvgAnimationLifetimeTest, ExcessiveSyncbaseHistoryDoesNotPublishPartialSamplesAndCanRecover) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("id", "cycle"));
    ASSERT_TRUE(animation->set_attribute("begin", "0;cycle.end"));
    ASSERT_TRUE(animation->set_attribute("dur", "0.001s"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "10");
}

TEST_F(SvgAnimationLifetimeTest, ReferenceAnimationScopesReachGeometryWithoutLeakingIntoStaticResources) {
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("rect").attr("x", "10").attr("width", "20").attr("height", "30").final();
    DomElement* rect = element("rect", svg, source.element); ASSERT_NE(rect, nullptr);
    source = builder.element("animate").attr("attributeName", "x").attr("attributeType", "XML")
        .attr("from", "10").attr("to", "110").attr("dur", "2s").attr("fill", "freeze").final();
    ASSERT_NE(element("animate", rect, source.element), nullptr);
    svg_animation_mark_reference(svg);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    {
        SvgAnimationSourceScope instance(&doc, rect);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
        float left, top, right, bottom;
        ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
        EXPECT_FLOAT_EQ(left, 60); EXPECT_FLOAT_EQ(right, 80);
    }
    SvgAnimationSourceScope resource(&doc);
    EXPECT_EQ(svg_animation_source_value(dom_element_to_element(rect), "x"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, AttributeNamespaceIdentityUsesAnimationBindingsAndRejectsUnrelatedNamespaces) {
    DomElement* scope = element("g", svg); ASSERT_NE(scope, nullptr);
    ASSERT_TRUE(scope->set_attribute("xmlns:a", "http://www.w3.org/1999/xlink"));
    DomElement* target_scope = element("g", svg); ASSERT_NE(target_scope, nullptr);
    ASSERT_TRUE(target_scope->set_attribute("xmlns:b", "http://www.w3.org/1999/xlink"));
    ASSERT_TRUE(target_scope->set_attribute("xmlns:xlink", "urn:unrelated"));
    DomElement* target = element("use", target_scope); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("id", "target")); ASSERT_TRUE(target->set_attribute("b:href", "#red"));
    DomElement* animation = animate(scope, "a:href", "#red", "#blue"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("href", "#target"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1.5);
    EXPECT_STREQ(svg_animation_value(target, "xlink:href"), "#blue");
    EXPECT_STREQ(dom_element_attribute_ns(target, "http://www.w3.org/1999/xlink", "href"), "#red");
    ASSERT_TRUE(scope->set_attribute("xmlns:a", "urn:unrelated"));
    svg_animation_set_time(svg, 1.75);
    EXPECT_EQ(svg_animation_value(target, "xlink:href"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, HtmlAnimationBindingsRequireActualXmlnsAttributes) {
    doc.page_kind = DOM_PAGE_KIND_HTML;
    DomElement* scope = element("g", svg); ASSERT_NE(scope, nullptr);
    ASSERT_TRUE(scope->set_attribute("xmlns:a", "http://www.w3.org/1999/xlink"));
    DomElement* target = element("use", svg); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("id", "target"));
    DomElement* animation = animate(scope, "a:href", "#red", "#blue"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("href", "#target"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1.5);
    EXPECT_EQ(svg_animation_value(target, "xlink:href"), nullptr);
    ASSERT_TRUE(dom_element_record_namespaced_attribute(scope, "http://www.w3.org/2000/xmlns/",
        "xmlns:a", "http://www.w3.org/1999/xlink"));
    svg_animation_set_time(svg, 1.75);
    EXPECT_STREQ(svg_animation_value(target, "xlink:href"), "#blue");
}

TEST_F(SvgAnimationLifetimeTest, NonAnimatableAndInapplicableAttributesDoNotProduceSamples) {
    struct Case { const char* tag; const char* name; const char* from; const char* to; bool transform; };
    const Case cases[] = {
        {"feFlood", "result", "first", "second", false}, {"rect", "r", "10", "20", false},
        {"rect", "transform", "translate(0)", "translate(100)", false},
        {"rect", "fill", "0", "100", true}, {"linearGradient", "gradientUnits", "userSpaceOnUse", "invalid", false},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        DomElement* animation = cases[i].transform ? element("animateTransform", targets[i])
            : animate(targets[i], cases[i].name, cases[i].from, cases[i].to);
        ASSERT_NE(animation, nullptr);
        if (cases[i].transform) {
            ASSERT_TRUE(animation->set_attribute("attributeName", cases[i].name));
            ASSERT_TRUE(animation->set_attribute("from", cases[i].from));
            ASSERT_TRUE(animation->set_attribute("to", cases[i].to));
            ASSERT_TRUE(animation->set_attribute("dur", "2s"));
        }
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_EQ(svg_animation_value(targets[i], cases[i].name), nullptr) << cases[i].name;
}

TEST(SvgAnimationTest, PictureDuplicatesRetainPrivateTimeWithoutChangingTheParsedCacheOwner) {
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='60' height='60'>"
        "<rect width='40' height='40'><set attributeName='fill' to='blue' begin='2s'/></rect></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    EXPECT_TRUE(render_svg_picture_has_animation(picture));
    rdt_picture_set_animation_time(picture, 2.25);
    RdtPicture* duplicate = rdt_picture_dup(picture); ASSERT_NE(duplicate, nullptr);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(duplicate), 2.25);
    rdt_picture_set_animation_time(duplicate, .5);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(picture), 2.25);
    RdtPicture* cached = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(cached, nullptr);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(cached), 0);
    rdt_picture_free(cached); rdt_picture_free(duplicate); rdt_picture_free(picture);
}

// M6 gate: a picture held the way a display list holds one (a counted
// duplicate) outlives the picture cache evicting its parsed original.
TEST(SvgPictureCacheTest, EvictionKeepsHeldDuplicatesAlive) {
    const char held_source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='30' height='20'>"
        "<rect width='30' height='20' fill='red'/></svg>";
    RdtPicture* original = rdt_picture_load_data(held_source, sizeof(held_source) - 1, "svg");
    ASSERT_NE(original, nullptr);
    RdtPicture* held = rdt_picture_dup(original);
    ASSERT_NE(held, nullptr);
    rdt_picture_free(original);
    // distinct documents push the original's entry out of the LRU cache
    char source[160];
    for (int i = 0; i < 160; i++) {
        int length = snprintf(source, sizeof(source),
            "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='10'><rect width='%d' height='10'/></svg>",
            i + 1, i + 1);
        RdtPicture* filler = rdt_picture_load_data(source, length, "svg");
        ASSERT_NE(filler, nullptr);
        rdt_picture_free(filler);
    }
    float width = 0.0f, height = 0.0f;
    rdt_picture_get_size(held, &width, &height);
    EXPECT_FLOAT_EQ(width, 30.0f);
    EXPECT_FLOAT_EQ(height, 20.0f);
    EXPECT_NE(rdt_picture_get_svg_root(held), nullptr);
    rdt_picture_free(held);
}

TEST(RenderCompositeTest, MultiplyPreservesSurfaceAlphaRepresentation) {
    const struct {
        ImageAlphaMode mode;
        uint32_t backdrop, source, expected;
    } cases[] = {
        {IMAGE_ALPHA_STRAIGHT, 0xff808080u, 0x800000ffu, 0xff404080u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0xff808080u, 0x80000080u, 0xff404080u},
        {IMAGE_ALPHA_STRAIGHT, 0x8000ff00u, 0x800000ffu, 0xc0005555u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0x80008000u, 0x80000080u, 0xc0004040u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0u, 0x80000080u, 0x80000080u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0x80008000u, 0u, 0x80008000u},
    };
    for (const auto& sample : cases) {
        uint32_t pixel = sample.source;
        ImageSurface surface = {};
        surface.width = surface.height = 1; surface.pitch = sizeof(pixel);
        surface.pixels = &pixel; surface.alpha_mode = sample.mode;
        render_composite_blend_surface(&surface, &sample.backdrop, 0, 0, 1, 1, CSS_VALUE_MULTIPLY);
        EXPECT_EQ(pixel, sample.expected) << "alpha mode " << sample.mode;
    }
}

TEST(RenderFilterTest, ChannelTablesMatchScalarStepsForEveryByteAndAlpha) {
    uint32_t pixels[256 * 256];
    const FilterFunctionType types[] = {FILTER_BRIGHTNESS, FILTER_CONTRAST, FILTER_INVERT};
    const float amounts[] = {-0.25f, 0.0f, 0.37f, 1.0f, 1.75f};
    for (ImageAlphaMode mode : {IMAGE_ALPHA_STRAIGHT, IMAGE_ALPHA_PREMULTIPLIED}) {
        for (FilterFunctionType type : types) for (float amount : amounts) {
            for (unsigned alpha = 0; alpha < 256; alpha++) for (unsigned value = 0; value < 256; value++) {
                uint32_t pixel = render_pixel_pack_abgr(value, 255 - value, (value * 37) & 255, alpha);
                pixels[alpha * 256 + value] = mode == IMAGE_ALPHA_PREMULTIPLIED ? render_pixel_premultiply_abgr(pixel) : pixel;
            }
            FilterFunction function = {}; function.type = type; function.params.amount = amount;
            FilterProp filter = {}; filter.functions = lam::own(&function); filter.functions_borrowed = true;
            ImageSurface surface = {}; surface.width = surface.height = 256;
            surface.pitch = 256 * sizeof(uint32_t); surface.pixels = pixels; surface.alpha_mode = mode;
            Rect rect = {0, 0, 256, 256}; Bound clip = {0, 0, 256, 256};
            apply_css_filters(nullptr, &surface, &filter, &rect, &clip);
            for (unsigned alpha = 0; alpha < 256; alpha++) for (unsigned value = 0; value < 256; value++) {
                uint32_t expected = render_pixel_pack_abgr(value, 255 - value, (value * 37) & 255, alpha);
                if (mode == IMAGE_ALPHA_PREMULTIPLIED) expected = render_pixel_premultiply_abgr(expected);
                ImageSurface scalar = {}; scalar.width = scalar.height = 1;
                scalar.pitch = sizeof(uint32_t); scalar.pixels = &expected; scalar.alpha_mode = mode;
                Rect unit = {0, 0, 1, 1}; Bound unit_clip = {0, 0, 1, 1};
                apply_css_filters(nullptr, &scalar, &filter, &unit, &unit_clip);
                ASSERT_EQ(pixels[alpha * 256 + value], expected)
                    << "filter " << type << " amount " << amount << " alpha " << alpha << " byte " << value << " mode " << mode;
            }
        }
    }
}

TEST(RenderFilterTest, BlurDoesNotSampleOrOverwriteOutsideItsSourceClip) {
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    ScratchArena scratch = {}; scratch_init(&scratch, arena);
    for (bool native : {false, true}) {
        uint32_t pixels[32 * 32];
        for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 32; x++)
            pixels[y * 32 + x] = x < 8 || x >= 24 || y < 8 || y >= 24
                ? 0xff00ff00u : x >= 12 && x < 20 && y >= 12 && y < 20 ? 0xff0000ffu : 0;
        ImageSurface surface = {}; surface.width = surface.height = 32;
        surface.pitch = 32 * sizeof(uint32_t); surface.pixels = pixels;
        FilterFunction function = {}; function.type = FILTER_BLUR; function.params.blur_radius = 2;
        FilterProp filter = {}; filter.functions = lam::own(&function); filter.functions_borrowed = true;
        Rect rect = {8, 8, 16, 16}; Bound clip = {8, 8, 24, 24};
        RenderBackendCaps caps = {}; caps.gaussian_blur = native;
        ASSERT_TRUE(render_filter_apply_with_backend(&caps, &scratch, &surface, &filter, &rect, &clip));
        for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 32; x++) {
            uint32_t pixel = pixels[y * 32 + x];
            if (x < 8 || x >= 24 || y < 8 || y >= 24) EXPECT_EQ(pixel, 0xff00ff00u);
            else EXPECT_EQ(pixel & 0x00ffff00u, 0u); // the green backdrop never enters the red source.
        }
        EXPECT_GT(pixels[16 * 32 + 11] >> 24, 0u);
    }
    scratch_release(&scratch); arena_destroy(arena);
}

TEST(RasterPaintTest, TranslucentFillPreservesAnIsolatedSourceAlpha) {
    for (ImageAlphaMode mode : {IMAGE_ALPHA_STRAIGHT, IMAGE_ALPHA_PREMULTIPLIED}) {
        uint32_t pixel = 0;
        ImageSurface surface = {};
        surface.width = surface.height = 1; surface.pitch = sizeof(pixel);
        surface.pixels = &pixel; surface.alpha_mode = mode;
        RasterPaintContext context = raster_paint_context(&surface, nullptr, nullptr, 0);
        raster_fill_rect(&context, nullptr, 0x800000ffu);
        EXPECT_EQ(pixel, mode == IMAGE_ALPHA_STRAIGHT ? 0x800000ffu : 0x80000080u);
        raster_fill_rect(&context, nullptr, 0x800000ffu);
        EXPECT_EQ(pixel, mode == IMAGE_ALPHA_STRAIGHT ? 0xc00000ffu : 0xc00000c0u);
    }
}

TEST(SvgExportTest, SmallRetainedFragmentsLeaveRoomForPngEncoding) {
    bool owns_tracking = memtrack_get_mode() == MEMTRACK_MODE_OFF;
    if (owns_tracking) ASSERT_TRUE(memtrack_init(MEMTRACK_MODE_DEBUG));
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    Arena* replay_arena = arena_create_default();
    DisplayList source = {}; dl_init(&source, arena);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    for (uint32_t view_id = 1; view_id <= 64; view_id++) {
        int begin = dl_begin_element(&source, view_id, 0.0f, 0.0f, 1.0f, 1.0f);
        DisplayItem* rect = dl_alloc_item(&source);
        ASSERT_NE(rect, nullptr);
        rect->op = DL_FILL_RECT;
        rect->bounds[2] = rect->bounds[3] = 1.0f;
        rect->fill_rect.w = rect->fill_rect.h = 1.0f;
        rect->fill_rect.color.c = 0x80000080u;
        dl_end_element(&source, begin);
    }
    uint32_t pixel = 0x80000080u;
    ImageSurface surface = {};
    surface.width = surface.height = 1; surface.pitch = sizeof(pixel);
    surface.pixels = &pixel; surface.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
    size_t soft, hard, critical;
    memtrack_get_limits(&soft, &hard, &critical);
    size_t usage = memtrack_get_current_usage();
    // small retained ranges must not consume a full frame's reserve per element.
    memtrack_set_limits(usage, usage, usage + 16 * 1024 * 1024);
    retained_dl_cache_capture(cache, &source);
    StrBuf* encoded = render_encode_surface_png(&surface);
    memtrack_set_limits(soft, hard, critical);
    EXPECT_NE(encoded, nullptr);
    if (encoded) {
        const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        EXPECT_GE(encoded->length, sizeof(signature));
        if (encoded->length >= sizeof(signature))
            EXPECT_EQ(memcmp(encoded->str, signature, sizeof(signature)), 0);
        strbuf_free(encoded);
    }
    EXPECT_EQ(pixel, 0x80000080u);
    EXPECT_EQ(retained_dl_cache_stats(cache).captured, 64);
    DisplayList replay = {}; dl_init(&replay, replay_arena);
    for (uint32_t view_id = 1; view_id <= 64; view_id++) {
        const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, view_id);
        EXPECT_NE(fragment, nullptr);
        if (!fragment) continue;
        EXPECT_EQ(retained_dl_fragment_item_count(fragment), 3);
        dl_clear(&replay);
        bool appended = retained_dl_append_fragment(&replay, fragment);
        EXPECT_TRUE(appended);
        EXPECT_EQ(replay.size(), 1u);
        if (!appended || replay.size() != 1) continue;
        EXPECT_EQ(test_radiant_command(&replay, 0).op, DL_BEGIN_ELEMENT);
        EXPECT_EQ(test_radiant_command(&replay, 0).element_marker.view_id, view_id);
        EXPECT_EQ(test_radiant_command(&replay, 0).element_marker.matching_index, 2);
        EXPECT_EQ(test_radiant_command(&replay, 1).op, DL_FILL_RECT);
        EXPECT_EQ(test_radiant_command(&replay, 1).fill_rect.color.c, 0x80000080u);
        EXPECT_EQ(test_radiant_command(&replay, 2).op, DL_END_ELEMENT);
        EXPECT_EQ(test_radiant_command(&replay, 2).element_marker.matching_index, 0);
    }
    dl_destroy(&replay); retained_dl_cache_destroy(cache);
    dl_destroy(&source); arena_destroy(arena); arena_destroy(replay_arena); pool_destroy(pool);
    if (owns_tracking) EXPECT_EQ(memtrack_shutdown(), 0u);
}

TEST(SvgExportTest, EncodingRowRespectsMemoryBudgetAndRecovers) {
    uint32_t pixels[64 * 64] = {};
    ImageSurface surface = {};
    surface.width = surface.height = 64; surface.pitch = 64 * sizeof(uint32_t);
    surface.pixels = pixels; surface.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
    size_t soft, hard, critical;
    memtrack_get_limits(&soft, &hard, &critical);
    // encoding needs one 256-byte row, which cannot fit this critical budget.
    memtrack_set_limits(0, 0, 128);
    StrBuf* rejected = render_encode_surface_data_uri(&surface);
    memtrack_set_limits(soft, hard, critical);
    EXPECT_EQ(rejected, nullptr);
    if (rejected) strbuf_free(rejected);
    StrBuf* recovered = render_encode_surface_data_uri(&surface);
    EXPECT_NE(recovered, nullptr);
    if (recovered) strbuf_free(recovered);
}

TEST(SvgExportTest, PngEncodingPreservesAlphaAndDoesNotMutatePaddedRows) {
    for (ImageAlphaMode mode : {IMAGE_ALPHA_STRAIGHT, IMAGE_ALPHA_PREMULTIPLIED}) {
        uint32_t pixels[] = {0x80000080u, 0xff00ff00u, 0xdeadbeefu,
                             0x40004000u, 0x00ffffffu, 0xdeadbeefu};
        const uint32_t original[] = {0x80000080u, 0xff00ff00u, 0xdeadbeefu,
                                     0x40004000u, 0x00ffffffu, 0xdeadbeefu};
        ImageSurface surface = {};
        surface.width = surface.height = 2; surface.pitch = 3 * sizeof(uint32_t);
        surface.pixels = pixels; surface.alpha_mode = mode;
        StrBuf* encoded = render_encode_surface_png(&surface);
        ASSERT_NE(encoded, nullptr);
        int width = 0, height = 0, channels = 0;
        uint8_t* decoded = image_load_from_memory((const unsigned char*)encoded->str,
            encoded->length, &width, &height, &channels);
        ASSERT_NE(decoded, nullptr);
        EXPECT_EQ(width, 2); EXPECT_EQ(height, 2);
        EXPECT_EQ(decoded[0], mode == IMAGE_ALPHA_STRAIGHT ? 128 : 255);
        EXPECT_EQ(decoded[3], 128);
        EXPECT_EQ(decoded[9], mode == IMAGE_ALPHA_STRAIGHT ? 64 : 255);
        EXPECT_EQ(decoded[11], 64); EXPECT_EQ(decoded[15], 0);
        EXPECT_EQ(memcmp(pixels, original, sizeof(pixels)), 0);
        image_free(decoded); strbuf_free(encoded);
    }
}

TEST(SvgTextTest, PlainGlyphsKeepNativeCoverageAndOwnPixelsAfterFontContextDies) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (float font_ratio : {1.0f, 2.0f}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(font_ratio);
        FontContextConfig config = {}; config.pixel_ratio = font_ratio;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        OffscreenRenderArenas arenas;
        ASSERT_TRUE(arenas.init("test.svg.native_text", "test.svg.native_text.list", "test.svg.native_text.scratch_arena"));
        DisplayList dl = {}; dl_init(&dl, arenas.list_arena);
        PaintList paint = {}; paint_list_init(&paint, arenas.scratch_arena);
        ScratchArena scratch = {}; mem_scratch_init(nullptr, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "test.svg.native_text.scratch");
        RasterRenderContext context = {}; context.raster_scale = density;
        context.dl = lam::up(&dl); context.paint_list = lam::up(&paint);
        const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40' viewBox='0 0 5000 2500'>"
            "<text x='625' y='1875' font-size='1000' font-family='Times New Roman' font-style='italic' "
            "fill='currentColor' color='#204080'>x</text></svg>";
        RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg"); ASSERT_NE(picture, nullptr);
        RdtMatrix transform = {density, 0, 0, 0, density, 0, 0, 0, 1};
        render_svg_record_picture(&paint, &dl, &scratch, fonts, &context, picture, 255, &transform);
        size_t images = 0;
        for (int i = 0; i < dl.item_count(); i++) if (dl.data()[i].op == DL_DRAW_IMAGE) images++;
        ASSERT_EQ(images, 1u);
        font_context_destroy(fonts); rdt_picture_free(picture); paint_list_destroy(&paint);
        scratch_release(&scratch);

        // independent native font bitmap is the coverage reference; recording owners are already gone.
        config.pixel_ratio = density; fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc style = {}; style.family = "Times New Roman"; style.size_px = 16;
        style.weight = FONT_WEIGHT_NORMAL; style.slant = FONT_SLANT_ITALIC;
        FontHandle* font = font_resolve(fonts, &style); ASSERT_NE(font, nullptr);
        const GlyphBitmap* bitmap = font_render_glyph(font, 'x', GLYPH_RENDER_NORMAL); ASSERT_NE(bitmap, nullptr);
        ImageSurface* expected = image_surface_create(lroundf(80 * density), lroundf(40 * density));
        ASSERT_NE(expected, nullptr);
        ImageSurface* actual = image_surface_create(expected->width, expected->height);
        ASSERT_NE(actual, nullptr);
        memset(expected->pixels, 255, (size_t)expected->pitch * expected->height);
        memset(actual->pixels, 255, (size_t)actual->pitch * actual->height);
        DlDrawGlyph glyph = {}; glyph.bitmap = *bitmap;
        glyph.x = lroundf(10 * density + bitmap->bearing_x); glyph.y = lroundf(30 * density - bitmap->bearing_y);
        glyph.color.r = 32; glyph.color.g = 64; glyph.color.b = 128; glyph.color.a = 255;
        glyph.clip = {0, 0, (float)actual->width, (float)actual->height};
        dl_replay_draw_glyph(expected, &glyph);
        RdtVector vector = {}; rdt_vector_init(&vector, (uint32_t*)actual->pixels, actual->width, actual->height, actual->width);
        mem_scratch_init(nullptr, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "test.svg.native_text.replay");
        dl_replay(&dl, &vector, actual, &glyph.clip, &scratch, density, nullptr); rdt_vector_destroy(&vector);
        int max_error = 0;
        for (size_t i = 0; i < (size_t)actual->pitch * actual->height; i++) {
            int difference = abs(((const uint8_t*)actual->pixels)[i] - ((const uint8_t*)expected->pixels)[i]);
            if (difference > max_error) max_error = difference;
        }
        // the owned transparent image adds one 8-bit compositing round trip.
        EXPECT_LE(max_error, 1);
        image_surface_destroy(expected); image_surface_destroy(actual);
        const char* vector_styles[] = {"transform='rotate(20)'", "transform='skewX(15)'",
            "transform='scale(2 1)'", "rotate='15'", "stroke='blue' stroke-width='.5'", "fill='url(#ink)'"};
        for (const char* attributes : vector_styles) {
            SCOPED_TRACE(attributes);
            dl_clear(&dl); paint_list_init(&paint, arenas.scratch_arena);
            StrBuf* svg = strbuf_create("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40'>"
                "<defs><linearGradient id='ink'><stop stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs>");
            strbuf_append_format(svg, "<text x='10' y='30' font-family='Times New Roman' font-size='16' %s>x</text></svg>", attributes);
            picture = rdt_picture_load_data(svg->str, svg->length, "svg"); ASSERT_NE(picture, nullptr);
            strbuf_free(svg);
            render_svg_record_picture(&paint, &dl, &scratch, fonts, &context, picture, 255, &transform);
            for (int i = 0; i < dl.item_count(); i++) EXPECT_NE(dl.data()[i].op, DL_DRAW_IMAGE);
            EXPECT_GT(dl.item_count(), 0);
            rdt_picture_free(picture); paint_list_destroy(&paint);
        }
        font_handle_release(font); font_context_destroy(fonts);
        scratch_release(&scratch); dl_destroy(&dl); arenas.destroy();
    }
    rdt_engine_term();
}

TEST(SvgExportTest, RasterSnapshotRetainsTransparencyAndLogicalBoundsAtBothDensities) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
        "<rect x='2.25' y='2.25' width='8' height='8' fill='red' opacity='.5'/></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    for (float density : {1.0f, 2.0f}) {
        PaintSvgSubscene subscene = {};
        render_svg_build_subscene(&subscene, rdt_picture_get_svg_root(picture), 20, 20,
            rdt_picture_get_pool(picture), density, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 1, false, nullptr, true, -1);
        subscene.image_document = true;
        Bound bounds = {};
        ImageSurface* surface = render_svg_subscene_rasterize(&subscene, &bounds);
        ASSERT_NE(surface, nullptr);
        EXPECT_FLOAT_EQ(bounds.left, 0); EXPECT_FLOAT_EQ(bounds.right, 20);
        EXPECT_EQ(surface->width, 20 * density); EXPECT_EQ(surface->height, 20 * density);
        EXPECT_EQ(surface->alpha_mode, IMAGE_ALPHA_STRAIGHT);
        const uint32_t* pixels = (const uint32_t*)surface->pixels;
        EXPECT_EQ(pixels[0], 0u);
        size_t coordinate = density == 1 ? 4 : 8;
        uint32_t red = pixels[coordinate * (size_t)surface->width + coordinate];
        EXPECT_EQ(red & 255u, 255u); EXPECT_NEAR(red >> 24, 128u, 1);
        // the fractional left edge covers 3/4 of a 1x pixel, or 1/2 of a 2x pixel, at half opacity.
        size_t edge_x = density == 1 ? 2 : 4;
        uint32_t edge = pixels[coordinate * (size_t)surface->width + edge_x];
        EXPECT_NEAR(edge & 255u, 255u, 1);
        EXPECT_NEAR(edge >> 24, density == 1 ? 96u : 64u, 3);
        image_surface_destroy(surface);
    }
    rdt_picture_free(picture);
    rdt_engine_term();
}

TEST(SvgExportTest, FinalPaintRetainsSampledVectorsAndOwnsDeferredClips) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
        "<rect width='10' height='10' fill='red'><set attributeName='fill' to='blue' begin='0s'/></rect></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    PaintSvgSubscene scene = {};
    render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 20, 20,
        rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, 1, false, nullptr, true, -1);
    scene.image_document = true;
    StrBuf* svg = strbuf_new();
    auto consume = [](PaintList* paint, void* context) {
        PaintSvgLoweringStats stats = {};
        paint_ir_lower_svg(paint, (StrBuf*)context, nullptr, &stats);
        return stats.unsupported_count == 0;
    };
    ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, svg));
    EXPECT_NE(strstr(svg->str, "<path"), nullptr);
    EXPECT_NE(strstr(svg->str, "rgb(0,0,255)"), nullptr);
    EXPECT_EQ(strstr(svg->str, "<set"), nullptr);
    strbuf_reset(svg);
    PaintList paint = {};
    RdtPath* path = rdt_path_new(); rdt_path_add_rect(path, 0, 0, 20, 20, 0, 0);
    paint_push_clip(&paint, path, nullptr); rdt_path_free(path);
    Color red = {}; red.r = red.a = 255;
    paint_fill_rect(&paint, 0, 0, 20, 20, red);
    paint_pop_clip(&paint);
    EXPECT_TRUE(consume(&paint, svg));
    EXPECT_NE(strstr(svg->str, "<clipPath"), nullptr);
    paint_list_destroy(&paint); strbuf_free(svg);
    rdt_picture_free(picture); rdt_engine_term();
}

TEST(SvgExportTest, FinalPaintRetainsNestedMetadataEmptyGroupsAndOutlinedText) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40' data-root='scene'>"
        "<g data-graph-role='edge' data-edge-id='a&amp;&quot;b' data-route='0,0 40,20'>"
        "<rect width='20' height='20' fill='red'/><g data-empty='kept'/>"
        "<text x='0' y='35' font-size='12'>A &amp; B</text></g></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    PaintSvgSubscene scene = {};
    render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 80, 40,
        rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, 1, false, nullptr, true, -1);
    scene.image_document = true;
    StrBuf* svg = strbuf_new();
    auto consume = [](PaintList* paint, void* context) {
        PaintSvgLoweringStats stats = {};
        paint_ir_lower_svg(paint, (StrBuf*)context, nullptr, &stats);
        return stats.unsupported_count == 0;
    };
    ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, svg));
    EXPECT_NE(strstr(svg->str, "data-root=\"scene\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-edge-id=\"a&amp;&quot;b\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-route=\"0,0 40,20\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-empty=\"kept\""), nullptr);
    EXPECT_NE(strstr(svg->str, "<title>A &amp; B</title>"), nullptr);
    EXPECT_NE(strstr(svg->str, "<path"), nullptr);
    strbuf_free(svg); rdt_picture_free(picture); rdt_engine_term();
}

TEST(SvgExportTest, SemanticSnapshotsOwnValuesAndDoNotChangeRasterPaint) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "test.svg.export.semantics");
    ASSERT_NE(arena, nullptr);
    DisplayList dl = {}; dl_init(&dl, arena);
    char name[] = "data-node-id", value[] = "n0", title[] = "label";
    RenderSemanticAttribute attribute = {lam::up(name), lam::up(value)};
    RenderSemanticGroup source = {lam::up(&attribute), 1, lam::up(title)}, snapshot = {};
    ASSERT_TRUE(dl_copy_semantic_group(&dl, &snapshot, &source));
    name[0] = value[0] = title[0] = '!';
    EXPECT_STREQ(snapshot.attributes[0].name, "data-node-id");
    EXPECT_STREQ(snapshot.attributes[0].value, "n0");
    EXPECT_STREQ(snapshot.title, "label");
    PaintList paint = {};
    paint_begin_semantic_group(&paint, &snapshot);
    Color red = {}; red.r = red.a = 255;
    paint_fill_rect(&paint, 0, 0, 20, 20, red);
    paint_end_semantic_group(&paint);
    EXPECT_TRUE(paint_ir_validate(&paint, nullptr));
    paint_ir_lower_raster(&paint, &dl);
    ASSERT_EQ(dl.item_count(), 1);
    EXPECT_EQ(dl.data()[0].op, DL_FILL_RECT);
    paint_list_clear(&paint);
    paint_end_semantic_group(&paint);
    EXPECT_FALSE(paint_ir_validate(&paint, nullptr));
    paint_list_destroy(&paint); dl_destroy(&dl); mem_arena_destroy(arena);
}

TEST(SvgExportTest, RasterFallbackRetainsSemanticGroupsAndTitles) {
    ASSERT_TRUE(radiant_register_dom_engine_providers());
    rdt_engine_init(0);
    const char* effects[] = {"opacity='.5'", "filter='url(#blur)'"};
    for (const char* effect : effects) {
        StrBuf* source = strbuf_create("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40'>"
            "<defs><filter id='blur'><feGaussianBlur stdDeviation='1'/></filter></defs>");
        strbuf_append_format(source,
            "<g data-node-id='n0' %s><g data-node-id='n1'><rect width='20' height='20' fill='red'/>"
            "<text x='0' y='35' font-size='12'>fallback label</text></g></g>"
            "<text x='40' y='20' font-size='12'>vector label</text></svg>", effect);
        RdtPicture* picture = rdt_picture_load_data(source->str, source->length, "svg");
        strbuf_free(source);
        ASSERT_NE(picture, nullptr);
        PaintSvgSubscene scene = {};
        render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 80, 40,
            rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 1, false, nullptr, true, -1);
        scene.image_document = true;
        StrBuf* svg = strbuf_new();
        struct Output { StrBuf* svg; int calls; } output = {svg, 0};
        auto consume = [](PaintList* paint, void* context) {
            Output* output = (Output*)context;
            output->calls++;
            // reject vector geometry to exercise the whole-subscene fallback after private effect capture.
            for (int i = 0; i < paint->item_count(); i++)
                if (paint->data()[i].op == PAINT_FILL_PATH) return false;
            PaintSvgLoweringStats stats = {};
            paint_ir_lower_svg(paint, output->svg, nullptr, &stats);
            return stats.unsupported_count == 0;
        };
        ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, &output, true));
        EXPECT_EQ(output.calls, 2);
        EXPECT_NE(strstr(svg->str, "data-node-id=\"n0\""), nullptr);
        EXPECT_NE(strstr(svg->str, "data-node-id=\"n1\""), nullptr);
        EXPECT_NE(strstr(svg->str, "<title>fallback label</title>"), nullptr);
        EXPECT_NE(strstr(svg->str, "data:image/png;base64,"), nullptr);
        strbuf_free(svg); rdt_picture_free(picture);
    }
    rdt_engine_term();
}

static RdtSvgFilterProgram* svg_filter_test_program(DomDocument* document, Element* element, size_t count, float width, float height) {
    RdtSvgFilterProgram* program = render_svg_filter_program_acquire(document, element);
    if (!program) return nullptr;
    program->compiled = program->valid = true; program->count = count;
    program->nodes = lam::own_arr((RdtSvgFilterNode*)arena_calloc(program->arena, count * sizeof(RdtSvgFilterNode)));
    program->region[0] = program->region[1] = lam::own(arena_strdup(program->arena, "0"));
    program->region[2] = lam::own(arena_sprintf(program->arena, "%g", (double)width));
    program->region[3] = lam::own(arena_sprintf(program->arena, "%g", (double)height));
    return program;
}

// filter-run host doubles: call counters and a declaring resource
struct SvgFilterTestCalls : RdtSvgFilterHost { unsigned count[3]; };
struct SvgFilterTestResource : RdtSvgFilterHost { Element* resource; };

static bool svg_filter_test_input(RdtSvgFilterHost* host, int input, const RdtSvgFilterRun*, Bound, ImageSurface* output) {
    unsigned slot = input == RDT_SVG_FILTER_FILL ? 0 : input == RDT_SVG_FILTER_STROKE ? 1 : 2;
    const uint32_t colors[] = {0x80000080u, 0x40004000u, 0x80800000u};
    static_cast<SvgFilterTestCalls*>(host)->count[slot]++;
    for (size_t index = 0, count = (size_t)output->width * (size_t)output->height; index < count; index++)
        ((uint32_t*)output->pixels)[index] = colors[slot];
    return true;
}

static bool svg_filter_test_source(RdtSvgFilterHost* host, const RdtSvgFilterRun* run, Bound, ImageSurface* output) {
    static_cast<SvgFilterTestCalls*>(host)->count[0]++;
    if (!render_svg_filter_spend_work(run, 16)) return false;
    for (size_t index = 0, count = (size_t)output->width * (size_t)output->height; index < count; index++)
        ((uint32_t*)output->pixels)[index] = 0xff808080u;
    return true;
}

TEST(SvgFilterTest, ComponentTransferUsesStraightChannelsAndRemappedAlpha) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 1.0f, 1.0f);
    ASSERT_NE(program, nullptr);
    RdtSvgFilterNode* node = program->nodes;
    node->kind = RDT_SVG_FILTER_TRANSFER; node->valid = true; node->input = RDT_SVG_FILTER_SOURCE;
    ImageSurface* source = image_surface_create(1, 1); ASSERT_NE(source, nullptr);
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source);
    run.source_bounds = run.geometry = {0.0f, 0.0f, 1.0f, 1.0f};
    run.lengths = {1.0f, 1.0f, 16.0f, 8.0f}; run.frame = rdt_matrix_identity(); run.density = 1.0f;
    node->transfer[0].table = lam::own_arr((float*)arena_calloc(program->arena, 3 * sizeof(float)));
    ASSERT_NE(node->transfer[0].table, nullptr);
    const struct { RdtSvgTransferKind kind; float values[3], table[3]; size_t count; unsigned red; } cases[] = {
        {RDT_SVG_TRANSFER_IDENTITY, {}, {}, 0, 102},
        {RDT_SVG_TRANSFER_TABLE, {}, {0.0f, .5f, 1.0f}, 3, 102},
        {RDT_SVG_TRANSFER_DISCRETE, {}, {.1f, .6f, 1.0f}, 3, 153},
        {RDT_SVG_TRANSFER_LINEAR, {.5f, .25f}, {}, 0, 115},
        {RDT_SVG_TRANSFER_GAMMA, {2.0f, 2.0f, .1f}, {}, 0, 107},
        {RDT_SVG_TRANSFER_TABLE, {}, {}, 0, 102},
        {RDT_SVG_TRANSFER_TABLE, {}, {.25f}, 1, 64}
    };
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    for (const auto& item : cases) {
        SCOPED_TRACE(item.kind);
        ((uint32_t*)source->pixels)[0] = 0xffcc3366u;
        node->transfer[0].kind = item.kind; node->transfer[0].count = item.count;
        memcpy(node->transfer[0].values, item.values, sizeof(item.values));
        memcpy(node->transfer[0].table.get(), item.table, sizeof(item.table));
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        uint32_t pixel = ((uint32_t*)result->pixels)[0];
        EXPECT_NEAR(pixel & 255u, item.red, 1);
        EXPECT_EQ((pixel >> 8) & 255u, 51u); EXPECT_EQ((pixel >> 16) & 255u, 204u);
        EXPECT_EQ(pixel >> 24, 255u);
        image_surface_destroy(result);
    }
    node->transfer[0].kind = RDT_SVG_TRANSFER_LINEAR;
    node->transfer[0].values[0] = .5f; node->transfer[0].values[1] = .25f;
    node->transfer[3].kind = RDT_SVG_TRANSFER_LINEAR; node->transfer[3].values[0] = .5f;
    ((uint32_t*)source->pixels)[0] = 0x80661933u;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    uint32_t pixel = ((uint32_t*)result->pixels)[0];
    EXPECT_NEAR(pixel & 255u, 29u, 1); EXPECT_NEAR((pixel >> 8) & 255u, 13u, 1);
    EXPECT_NEAR((pixel >> 16) & 255u, 51u, 1); EXPECT_EQ(pixel >> 24, 64u);
    image_surface_destroy(result);
    node->transfer[3].values[0] = 0.0f; node->transfer[3].values[1] = .5f;
    ((uint32_t*)source->pixels)[0] = 0;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    pixel = ((uint32_t*)result->pixels)[0];
    EXPECT_NEAR(pixel & 255u, 32u, 1); EXPECT_EQ((pixel >> 8) & 65535u, 0u); EXPECT_EQ(pixel >> 24, 128u);
    image_surface_destroy(result);
    node->transfer[3] = {};
    node->transfer[0].kind = RDT_SVG_TRANSFER_GAMMA;
    node->transfer[0].values[0] = 1.0f; node->transfer[0].values[1] = 2.0f; node->transfer[0].values[2] = 0.0f;
    node->linear = true; ((uint32_t*)source->pixels)[0] = 0xffcc3366u;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    // the shared filter pipeline quantizes linear intermediates to eight-bit surfaces.
    EXPECT_NEAR(((uint32_t*)result->pixels)[0] & 255u, 38u, 1);
    image_surface_destroy(result); image_surface_destroy(source);
    scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, ResourceFontsAndReferencingViewportHaveDistinctLengthBases) {
    DomDocument document; Element filter = {}, primitive = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &filter, 1, 20, 80);
    ASSERT_NE(program, nullptr);
    program->region[0] = lam::own(arena_strdup(program->arena, "1em"));
    program->region[2] = lam::own(arena_strdup(program->arena, "2em"));
    program->region[3] = lam::own(arena_strdup(program->arena, "50%"));
    RdtSvgFilterNode* node = program->nodes;
    node->kind = RDT_SVG_FILTER_FLOOD; node->valid = true; node->element = lam::up(&primitive);
    node->color.r = node->color.a = 255;
    node->region[0] = lam::own(arena_strdup(program->arena, "2em"));
    node->region[1] = lam::own(arena_strdup(program->arena, "25%"));
    node->region[2] = lam::own(arena_strdup(program->arena, "1em"));
    node->region[3] = lam::own(arena_strdup(program->arena, "10%"));
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.geometry = {0,0,20,80};
    run.lengths = {200,160,100,50}; run.frame = rdt_matrix_identity(); run.density = 1;
    SvgFilterTestResource declaring = {}; declaring.resource = &filter;
    run.image_context = lam::up(&declaring);
    run.resolve_lengths = [](RdtSvgFilterHost* host, Element* resource, SvgLengthContext* lengths) {
        lengths->font_size = resource == static_cast<SvgFilterTestResource*>(host)->resource ? 10.0f : 5.0f;
        lengths->x_height = lengths->font_size * .5f;
    };
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr);
    EXPECT_FLOAT_EQ(bounds.left, 10); EXPECT_FLOAT_EQ(bounds.right, 30);
    EXPECT_FLOAT_EQ(bounds.bottom, 80);
    EXPECT_EQ(((uint32_t*)result->pixels)[40 * 20], 0xff0000ffu);
    EXPECT_EQ(((uint32_t*)result->pixels)[40 * 20 + 5], 0u);
    EXPECT_EQ(((uint32_t*)result->pixels)[39 * 20], 0u);
    EXPECT_EQ(((uint32_t*)result->pixels)[56 * 20], 0u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, CaptureAndFinalColorConversionShareTheGraphWorkBudget) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 2, 2);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_OFFSET;
    node->valid = true; node->input = RDT_SVG_FILTER_SOURCE;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    SvgFilterTestCalls calls = {}; size_t used = 0;
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.geometry = {0,0,2,2}; run.lengths = {2,2,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1; run.draw_source = svg_filter_test_source;
    run.source_context = lam::up(&calls); run.work_used = lam::up(&used); run.work_limit = 23;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    EXPECT_EQ(calls.count[0], 1u); EXPECT_EQ(used, 20u);
    used = 0; run.work_limit = 24;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    EXPECT_EQ(used, 24u); EXPECT_EQ(calls.count[0], 2u); EXPECT_EQ(((uint32_t*)result->pixels)[0], 0xff808080u);
    image_surface_destroy(result);
    node->kind = RDT_SVG_FILTER_MATRIX; node->linear = true;
    node->values[0] = node->values[6] = node->values[12] = node->values[18] = 1;
    used = 0; run.work_limit = 123;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    EXPECT_EQ(used, 120u);
    used = 0; run.work_limit = 124;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(used, 124u);
    uint32_t pixel = ((uint32_t*)result->pixels)[0];
    for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, 128u, 1);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, StandardInputsAreCapturedLazilyAndBackgroundAlphaSharesItsImage) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 2, 2);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_MERGE; node->valid = true;
    const int inputs[] = {RDT_SVG_FILTER_FILL, RDT_SVG_FILTER_STROKE, RDT_SVG_FILTER_BACKGROUND, RDT_SVG_FILTER_BACKGROUND_ALPHA};
    node->merge_count = 4; node->merge_inputs = lam::own_arr((int*)arena_alloc(program->arena, sizeof(inputs)));
    ASSERT_NE(node->merge_inputs, nullptr); memcpy(node->merge_inputs, inputs, sizeof(inputs));
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    SvgFilterTestCalls calls = {};
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.geometry = {0,0,2,2}; run.lengths = {2,2,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1; run.draw_input = svg_filter_test_input; run.input_context = lam::up(&calls);
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned slot = 0; slot < 3; slot++) EXPECT_EQ(calls.count[slot], 1u);
    for (unsigned index = 0; index < 4; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0xe8401018u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, GaussianExtensionSamplesTheInputBordersAndOppositeEdge) {
    for (unsigned axis = 0; axis < 2; axis++) {
        DomDocument document; Element element = {};
        unsigned width = axis ? 1 : 6, height = axis ? 6 : 1;
        RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, (float)width, (float)height);
        ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
        RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_BLUR; node->valid = true;
        node->input = RDT_SVG_FILTER_SOURCE; node->values[axis] = 1;
        ImageSurface* source = image_surface_create(width, height); ASSERT_NE(source, nullptr);
        for (unsigned index = 0; index < 6; index++) ((uint32_t*)source->pixels)[index] = index < 3 ? 0xff0000ffu : 0xffff0000u;
        Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
        RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source); run.geometry = run.source_bounds = {0,0,(float)width,(float)height};
        run.lengths = {(float)width,(float)height,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
        // normalized Gaussian taps at sigma 1 yield distinct transparent, duplicated and wrapped edge colors.
        const uint32_t expected[] = {0xb20100b1u, 0xff0100feu, 0xff4e00b1u};
        for (unsigned mode = 0; mode < 3; mode++) {
            node->variant = mode; ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
            ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
            uint32_t pixel = ((uint32_t*)result->pixels)[0];
            for (unsigned channel = 0; channel < 4; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, (expected[mode] >> (channel * 8)) & 255u, 1);
            image_surface_destroy(result);
        }
        image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
        render_svg_filter_program_release(program); document.destroy();
    }
}

TEST(SvgFilterTest, FractionalTilesInterpolateAcrossBothPeriodicBorders) {
    for (unsigned axis = 0; axis < 2; axis++) {
        DomDocument document; Element element = {};
        float width = axis ? 1 : 12, height = axis ? 12 : 1;
        RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, width, height);
        ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
        RdtSvgFilterNode* input = &program->nodes[0]; input->kind = RDT_SVG_FILTER_OFFSET;
        input->valid = true; input->input = RDT_SVG_FILTER_SOURCE;
        input->region[axis + 2] = lam::own(arena_strdup(program->arena, "5.5"));
        RdtSvgFilterNode* tile = &program->nodes[1]; tile->kind = RDT_SVG_FILTER_TILE;
        tile->valid = true; tile->input = 0;
        ImageSurface* source = image_surface_create(axis ? 1 : 6, axis ? 6 : 1); ASSERT_NE(source, nullptr);
        for (unsigned index = 0; index < 6; index++) ((uint32_t*)source->pixels)[index] = 0xff00ff00u;
        Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
        RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source);
        run.geometry = {0,0,width,height}; run.source_bounds = {0,0,(float)source->width,(float)source->height};
        run.lengths = {width,height,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
        ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        // a constant opaque input stays opaque across fractional repeat seams in either axis.
        for (unsigned index = 0; index < 12; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0xff00ff00u);
        image_surface_destroy(result); image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
        render_svg_filter_program_release(program); document.destroy();
    }
}

TEST(SvgFilterTest, LightingUsesSobelSlopesAtAllImageEdgesAndSpecularPremultiplication) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 5, 5);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->valid = true; node->input = RDT_SVG_FILTER_ALPHA;
    node->kind = RDT_SVG_FILTER_DIFFUSE; node->color.r = node->color.g = node->color.b = node->color.a = 255;
    node->light.surface_scale = node->light.constant = 1; node->light.kind = 1;
    node->light.position[0] = .6f; node->light.position[2] = .8f;
    ImageSurface* source = image_surface_create(5, 5); ASSERT_NE(source, nullptr);
    for (unsigned row = 0; row < 5; row++) for (unsigned column = 0; column < 5; column++)
        ((uint32_t*)source->pixels)[row * 5 + column] = (32u + 16u * column + 8u * row) << 24;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source); run.geometry = run.source_bounds = {0,0,5,5};
    run.lengths = {5,5,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
    // the affine height field has the same analytically known normal at corners, edges and interiors.
    double nx = -32.0 / 255.0, ny = -16.0 / 255.0;
    unsigned expected = clamp_byte_round((float)((.6 * nx + .8) / sqrt(nx * nx + ny * ny + 1.0) * 255.0));
    Bound bounds = {}; RdtMatrix placement; ImageSurface* result = nullptr;
    for (unsigned kernel = 1; kernel <= 2; kernel++) {
        node->light.kernel[0] = node->light.kernel[1] = (float)kernel;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        for (unsigned index = 0; index < 25; index++) {
            uint32_t pixel = ((uint32_t*)result->pixels)[index];
            EXPECT_EQ(pixel >> 24, 255u);
            for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, expected, 1);
        }
        image_surface_destroy(result); result = nullptr;
    }
    for (unsigned index = 0; index < 25; index++) ((uint32_t*)source->pixels)[index] = 0xff000000u;
    node->kind = RDT_SVG_FILTER_SPECULAR; node->light.constant = .5f; node->light.exponent = 4;
    node->light.position[0] = sqrtf(.75f); node->light.position[2] = .5f; node->color.g = node->color.b = 0;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned index = 0; index < 25; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0x48000048u);
    image_surface_destroy(result); result = nullptr;
    node->light.constant = 0;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned index = 0; index < 25; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0u);
    image_surface_destroy(result); result = nullptr;
    run.work_limit = 50;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}


TEST(SvgFilterTest, NamedGraphUsesPremultipliedSourceAndLeavesSourcePixelsOwned) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    program->nodes[0].kind = RDT_SVG_FILTER_OFFSET; program->nodes[0].valid = true;
    program->nodes[0].input = RDT_SVG_FILTER_SOURCE; program->nodes[0].input2 = RDT_SVG_FILTER_EMPTY;
    program->nodes[0].values[0] = 2.0f; program->nodes[0].result = lam::own(arena_strdup(program->arena, "moved"));
    RdtSvgFilterNode* matrix = &program->nodes[1]; matrix->kind = RDT_SVG_FILTER_MATRIX; matrix->valid = true;
    matrix->input = render_svg_filter_input(program, 1, "moved"); matrix->input2 = RDT_SVG_FILTER_EMPTY;
    matrix->values[5] = matrix->values[18] = 1.0f;
    EXPECT_EQ(matrix->input, 0);
    EXPECT_EQ(render_svg_filter_input(program, 1, "unknown"), 0);
    EXPECT_EQ(render_svg_filter_input(program, 0, "forward"), RDT_SVG_FILTER_SOURCE);
    EXPECT_EQ(render_svg_filter_input(program, 1, nullptr), 0);
    ImageSurface* source = image_surface_create(2, 2);
    ASSERT_NE(source, nullptr);
    for (size_t index = 0; index < 4; index++) ((uint32_t*)source->pixels)[index] = 0x80000080u;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source);
    run.source_bounds = {0,0,2,2}; run.geometry = {0,0,2,2}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(((uint32_t*)result->pixels)[2], 0x80008000u);
    EXPECT_EQ(((uint32_t*)result->pixels)[0], 0u);
    EXPECT_EQ(((uint32_t*)source->pixels)[0], 0x80000080u);
    ImageSurface* retained = result; result = nullptr;
    run.work_limit = 1;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    EXPECT_EQ(result, nullptr);
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
    // deferred paint owns its output after input, execution scratch and program teardown.
    EXPECT_EQ(((uint32_t*)retained->pixels)[2], 0x80008000u);
    EXPECT_EQ(((uint32_t*)retained->pixels)[0], 0u);
    image_surface_destroy(retained);
}

TEST(SvgFilterTest, CoordinatorPreservesPinnedProgramsAndMutationRetiresOldFacts) {
    DomDocument document; Element element = {};
    document.services.mem_ctx = mem_context_create(nullptr, MEM_ROLE_RENDER, "test.svg.filter");
    MemContext* memory = (MemContext*)document.services.mem_ctx;
    ASSERT_NE(memory, nullptr);
    RdtSvgFilterProgram* original = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(original, nullptr);
    original->compiled = original->valid = true;
    EXPECT_EQ(render_svg_filter_program_acquire(&document, &element), original);
    render_svg_filter_program_release(original);
    EXPECT_EQ(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    document.mutation_epoch++;
    RdtSvgFilterProgram* changed = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(changed, nullptr); EXPECT_NE(changed, original);
    EXPECT_FALSE(changed->compiled); EXPECT_TRUE(original->compiled);
    render_svg_filter_program_release(original); render_svg_filter_program_release(changed);
    EXPECT_GT(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    RdtSvgFilterProgram* fresh = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(fresh, nullptr); EXPECT_FALSE(fresh->compiled);
    render_svg_filter_program_release(fresh); document.destroy();
    EXPECT_EQ(mem_context_live_count(memory), 0u);
    mem_context_destroy(memory);
}

TEST(SvgFilterTest, MorphologySeparatesAxesAndPreservesPremultipliedExtrema) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 9, 7);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_MORPHOLOGY;
    node->valid = true; node->input = RDT_SVG_FILTER_SOURCE;
    ImageSurface* source = image_surface_create(9, 7); ASSERT_NE(source, nullptr);
    for (unsigned row = 1; row < 6; row++) for (unsigned column = 2; column < 7; column++)
        ((uint32_t*)source->pixels)[row * 9 + column] = 0x80000080u;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.source = lam::up(source);
    run.source_bounds = run.geometry = {0,0,9,7}; run.lengths = {9,7,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    for (unsigned dilate = 0; dilate < 2; dilate++) for (unsigned axis = 0; axis < 2; axis++) {
        node->variant = dilate; node->values[0] = axis ? 0 : 2; node->values[1] = axis ? 1 : 0;
        ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        for (unsigned row = 0; row < 7; row++) for (unsigned column = 0; column < 9; column++) {
            bool inside = axis ? (column >= 2 && column < 7 && row >= (dilate ? 0u : 2u) && row < (dilate ? 7u : 5u)) :
                (row >= 1 && row < 6 && column >= (dilate ? 0u : 4u) && column < (dilate ? 9u : 5u));
            EXPECT_EQ(((uint32_t*)result->pixels)[row * 9 + column], inside ? 0x80000080u : 0u);
        }
        image_surface_destroy(result);
    }
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, NonseparableBlendsPreserveLuminosityAndHandleGray) {
    float mixed[3];
    render_composite_blend_rgb(0xffff0000u, 0xff0000ffu, CSS_VALUE_HUE_BLEND, mixed);
    EXPECT_NEAR(.3f * mixed[0] + .59f * mixed[1] + .11f * mixed[2], .11f, .00001f);
    EXPECT_GT(mixed[0], mixed[2]); EXPECT_NEAR(mixed[1], 0.0f, .00001f);
    render_composite_blend_rgb(0xff808080u, 0xff0000ffu, CSS_VALUE_SATURATION_BLEND, mixed);
    for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR(mixed[channel], 128.0f / 255.0f, .00001f);
    EXPECT_EQ(render_composite_blend_pixel(0, 0x80000080u, CSS_VALUE_COLOR_BLEND), 0x80000080u);
    EXPECT_EQ(render_composite_blend_pixel(0xff123456u, 0, CSS_VALUE_LUMINOSITY_BLEND), 0xff123456u);
}

TEST(SvgFilterTest, SurfaceBudgetReclaimsUnpinnedGraphsAndRejectsOversizedGrid) {
    DomDocument document; Element element = {};
    document.services.mem_ctx = mem_context_create(nullptr, MEM_ROLE_RENDER, "test.svg.filter.budget");
    MemContext* memory = (MemContext*)document.services.mem_ctx; ASSERT_NE(memory, nullptr);
    RdtSvgFilterProgram* program = render_svg_filter_program_acquire(&document, &element); ASSERT_NE(program, nullptr);
    render_svg_filter_program_release(program);
    size_t soft, hard, critical; memtrack_get_limits(&soft, &hard, &critical);
    size_t usage = memtrack_get_current_usage();
    memtrack_set_limits(usage, usage, usage + 256);
    EXPECT_EQ(render_surface_create_budgeted(memory, 1024, 1024), nullptr);
    EXPECT_EQ(document.services.svg_filter_registry != nullptr, true);
    EXPECT_EQ(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    memtrack_set_limits(soft, hard, critical);
    ImageSurface* surface = render_surface_create_budgeted(memory, 8, 8);
    EXPECT_NE(surface, nullptr); if (surface) image_surface_destroy(surface);
    document.destroy(); EXPECT_EQ(mem_context_live_count(memory), 0u); mem_context_destroy(memory);
}

TEST(SvgFilterTest, UnusedTreeDoesNotSpendWorkOrCaptureSource) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* blur = &program->nodes[0]; blur->kind = RDT_SVG_FILTER_BLUR; blur->valid = true;
    blur->input = RDT_SVG_FILTER_SOURCE; blur->values[0] = blur->values[1] = 1e20f;
    RdtSvgFilterNode* flood = &program->nodes[1]; flood->kind = RDT_SVG_FILTER_FLOOD; flood->valid = true;
    flood->color.r = 255; flood->color.a = 255;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.geometry = {0,0,8,8}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f; run.work_limit = 64;
    SvgFilterTestCalls calls = {}; run.draw_source = svg_filter_test_source; run.source_context = lam::up(&calls);
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr); EXPECT_EQ(((uint32_t*)result->pixels)[0], 0xff0000ffu);
    EXPECT_EQ(calls.count[0], 0u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, NoiseSeedTruncationAndRegenerationAreDeterministic) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_TURBULENCE; node->valid = true;
    node->variant = 1; node->values[0] = .08f; node->values[1] = .1f; node->values[2] = 2;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = lam::up(&scratch); run.geometry = {0,0,8,8}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    ImageSurface* original = nullptr; Bound bounds = {}; RdtMatrix placement;
    node->noise = lam::own(render_svg_filter_noise_create(program->arena, nullptr, -4.8f)); ASSERT_NE(node->noise, nullptr);
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &original, &bounds, &placement));
    for (unsigned repetition = 0; repetition < 2; repetition++) {
        node->noise = lam::own(render_svg_filter_noise_create(program->arena, nullptr, -4.0f)); ASSERT_NE(node->noise, nullptr);
        ImageSurface* result = nullptr; ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        EXPECT_EQ(memcmp(original->pixels, result->pixels, 8 * 8 * 4), 0);
        for (size_t index = 0; index < 64; index++) {
            uint32_t pixel = ((uint32_t*)result->pixels)[index], alpha = pixel >> 24;
            for (unsigned channel = 0; channel < 3; channel++) EXPECT_LE((pixel >> (channel * 8)) & 255u, alpha);
        }
        image_surface_destroy(result);
    }
    image_surface_destroy(original); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(RdtPathMetricsTest, SubpathsDoNotCountMoveGapsAndEndpointsExtrapolate) {
    RdtPath* path = svg_parse_path_d("M0 0L30 40M100 100L100 140");
    ASSERT_NE(path, nullptr);
    RdtPathMetrics metrics = {};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    EXPECT_NEAR(metrics.length, 90.0f, .001f);
    EXPECT_EQ(metrics.subpath_count, 2u);
    EXPECT_FALSE(metrics.closed);
    float x, y, tx, ty;
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 25.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 15.0f, .001f); EXPECT_NEAR(y, 20.0f, .001f);
    EXPECT_NEAR(tx, .6f, .001f); EXPECT_NEAR(ty, .8f, .001f);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 70.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 100.0f, .001f); EXPECT_NEAR(y, 120.0f, .001f);
    float distance, normal;
    ASSERT_TRUE(render_path_metrics_project(&metrics, 110.0f, 120.0f, 50.0f, 90.0f, &distance, &normal));
    EXPECT_NEAR(distance, 70.0f, .001f); EXPECT_NEAR(normal, -10.0f, .001f);
    EXPECT_FALSE(render_path_metrics_sample(&metrics, -10.0f, &x, &y, &tx, &ty));
    ASSERT_TRUE(render_path_metrics_sample(&metrics, -10.0f, &x, &y, &tx, &ty, true));
    EXPECT_NEAR(x, -6.0f, .001f); EXPECT_NEAR(y, -8.0f, .001f);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 100.0f, &x, &y, &tx, &ty, true));
    EXPECT_NEAR(x, 100.0f, .001f); EXPECT_NEAR(y, 150.0f, .001f);
    render_path_metrics_destroy(&metrics); rdt_path_free(path);
}

TEST(RdtPathMetricsTest, CubicArcLengthAndAffineCompactContours) {
    RdtPath* path = svg_parse_path_d("M0 0C0 100 100 100 100 0");
    ASSERT_NE(path, nullptr);
    RdtPathMetrics metrics = {};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    EXPECT_NEAR(metrics.length, 200.0f, .02f);
    float x, y, tx, ty;
    ASSERT_TRUE(render_path_metrics_sample(&metrics, metrics.length * .5f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 50.0f, .02f); EXPECT_NEAR(y, 75.0f, .02f);
    EXPECT_NEAR(tx, 1.0f, .01f); EXPECT_NEAR(ty, 0.0f, .02f);
    rdt_path_free(path);
    path = rdt_path_new(); rdt_path_add_rect(path, 0, 0, 10, 20, 0, 0);
    RdtMatrix mapping = {2, 0, 30, 0, 3, 40, 0, 0, 1};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path, &mapping));
    EXPECT_NEAR(metrics.length, 160.0f, .001f);
    EXPECT_TRUE(metrics.closed);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 50.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 50.0f, .001f); EXPECT_NEAR(y, 70.0f, .001f);
    float distance, normal;
    ASSERT_TRUE(render_path_metrics_project(&metrics, 35.0f, 35.0f, 140.0f, 190.0f, &distance, &normal, true));
    EXPECT_NEAR(distance, 165.0f, .001f); EXPECT_NEAR(normal, -5.0f, .001f);
    rdt_path_free(path);
    path = rdt_path_new(); rdt_path_add_circle(path, 10, 20, 30, 15);
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 0.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 40.0f, .001f); EXPECT_NEAR(y, 20.0f, .001f);
    EXPECT_GT(ty, .99f);
    render_path_metrics_destroy(&metrics); rdt_path_free(path);
}

TEST(RdtImageTest, AlphaModeSurvivesOwnedImageReleaseAndSeparatesCachedUploads) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    ImageSurface* image = image_surface_create(1, 1);
    ASSERT_NE(image, nullptr);
    image->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    ((uint32_t*)image->pixels)[0] = 0x80000064u;
    paint_draw_image_resource(&paint, image, 0, 0, 8, 8, 255, nullptr, image_surface_destroy);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy();
    ASSERT_EQ(dl.item_count(), 1);
    EXPECT_TRUE(dl.data()[0].draw_image.straight_alpha);
    uint32_t pixels[64];
    for (uint32_t& pixel : pixels) pixel = 0xffffffffu;
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 8, 8, 8);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
    EXPECT_NEAR((float)(pixels[36] & 255u), 177.0f, 1.0f);
    EXPECT_NEAR((float)((pixels[36] >> 8) & 255u), 127.0f, 1.0f);
    const uint32_t source = 0x80000064u;
    for (bool straight : {false, true, false}) {
        for (uint32_t& pixel : pixels) pixel = 0xffffffffu;
        rdt_draw_image(&vector, &source, 1, 1, 1, 0, 0, 8, 8, 255, nullptr, 1, straight);
        EXPECT_NEAR((float)(pixels[36] & 255u), straight ? 177.0f : 227.0f, 1.0f);
        EXPECT_EQ(source, 0x80000064u);
    }
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena);
}

TEST(GradientImagePaintTest, SourceReplacementAndRecordingReleasePreserveDeferredPixels) {
    // the process-lifetime surface handle registry precedes per-recording leak tracking.
    ImageSurface* warm = image_surface_create(1, 1);
    ASSERT_NE(warm, nullptr);
    image_surface_destroy(warm);
    bool owns_tracking = memtrack_get_mode() == MEMTRACK_MODE_OFF;
    if (owns_tracking) ASSERT_TRUE(memtrack_init(MEMTRACK_MODE_DEBUG));
    rdt_engine_init(0);
    MemtrackCategoryStats before = {};
    memtrack_get_category_stats(MEM_CAT_IMAGE, &before);
    auto replay = [](PaintList* paint, bool retire_recording, uint32_t* pixels) {
        Arena* arena = arena_create_default();
        DisplayList dl = {}; dl_init(&dl, arena);
        paint_ir_lower_raster(paint, &dl);
        if (retire_recording) paint->destroy();
        ASSERT_EQ(dl.item_count(), 1);
        ASSERT_TRUE(dl_validate(&dl, nullptr));
        RdtVector vector = {}; rdt_vector_init(&vector, pixels, 16, 16, 16);
        EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
        rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena);
    };
    for (GradientType type : {GRADIENT_LINEAR, GRADIENT_RADIAL, GRADIENT_CONIC})
        for (bool repeating : {false, true})
            for (unsigned iteration = 0; iteration < 256; iteration++) {
                lam::Temp<GradientStop> stops = lam::temp_array<GradientStop>(2, MEM_CAT_RENDER);
                ASSERT_TRUE(stops);
                stops[0] = {Color{0xff0000ffu}, 0, false, nullptr};
                stops[1] = {Color{iteration % 2 ? 0xffff0000u : 0xff0000ffu}, 1, false, nullptr};
                // outside CSS stops still produce recording-owned backend geometry.
                if (iteration > 1 && iteration % 4 < 2) stops[0].position = -.5f;
                if (iteration > 1 && iteration % 4 > 0) stops[1].position = 2.0f;
                if (iteration > 1 && iteration % 8 == 7) stops[0].position = 1.5f;
                Pool* expression_pool = iteration < 2 ? pool_create() : nullptr;
                if (expression_pool) {
                    const char* source = "width:calc(100%)";
                    CssDeclaration* declaration = css_parse_declaration_text(source, strlen(source), expression_pool);
                    ASSERT_NE(declaration, nullptr);
                    stops[1].position_expression = lam::own(css_value_clone_owned(declaration->value, expression_pool));
                    ASSERT_NE(stops[1].position_expression, nullptr);
                    stops[1].position = NAN;
                }
                LinearGradient linear = {}; linear.angle = 90;
                linear.stops = lam::own_arr(stops.get()); linear.stop_count = 2; linear.is_repeating = repeating;
                RadialGradient radial = {}; radial.cx = radial.cy = .5f;
                radial.stops = lam::own_arr(stops.get()); radial.stop_count = 2; radial.is_repeating = repeating;
                ConicGradient conic = {}; conic.cx = conic.cy = .5f;
                conic.stops = lam::own_arr(stops.get()); conic.stop_count = 2; conic.is_repeating = repeating;
                ListStyleImage image = {}; image.gradient_type = type;
                if (type == GRADIENT_LINEAR) image.linear_gradient = lam::shared(&linear);
                if (type == GRADIENT_RADIAL) image.radial_gradient = lam::shared(&radial);
                if (type == GRADIENT_CONIC) image.conic_gradient = lam::shared(&conic);
                PaintList paint;
                RdtMatrix transform = rdt_matrix_translate(4, 4);
                ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 8, 8}, 2, &transform));
                uint32_t expected[16 * 16] = {};
                replay(&paint, false, expected);
                // recording owns the resolved paint after its style source is overwritten.
                memset(stops.get(), 0, sizeof(GradientStop) * 2);
                stops.reset();
                // the retained paint must outlive the complete computed-expression owner.
                if (expression_pool) pool_destroy(expression_pool);
                linear.stops = radial.stops = conic.stops = nullptr;
                linear.stop_count = radial.stop_count = conic.stop_count = 0;
                ASSERT_TRUE(paint_ir_validate(&paint, nullptr));
                StrBuf* svg = strbuf_new(); ASSERT_NE(svg, nullptr);
                PaintSvgLoweringStats stats = {};
                paint_ir_lower_svg(&paint, svg, nullptr, &stats);
                EXPECT_EQ(stats.unsupported_count, 0);
                // opaque constant images now use a solid rectangle inside the retained transform pair.
                bool solid = strstr(svg->str, "<rect ") != nullptr;
                EXPECT_EQ(stats.emitted_count, solid ? 3 : 1);
                EXPECT_EQ(stats.command_count, solid ? 3 : 1);
                if (solid) EXPECT_NE(strstr(svg->str, "<g transform="), nullptr);
                EXPECT_GT(svg->length, 0u);
                strbuf_free(svg);
                uint32_t pixels[16 * 16] = {};
                replay(&paint, true, pixels);
                EXPECT_EQ(memcmp(pixels, expected, sizeof(pixels)), 0);
                EXPECT_EQ(pixels[8 * 16 + 8] >> 24, 255u);
                if (!(iteration % 2)) EXPECT_EQ(pixels[8 * 16 + 8], 0xff0000ffu);
                EXPECT_EQ(pixels[2 * 16 + 2], 0u);
            }
    MemtrackCategoryStats after = {};
    memtrack_get_category_stats(MEM_CAT_IMAGE, &after);
    EXPECT_EQ(after.current_bytes, before.current_bytes);
    EXPECT_EQ(after.current_count, before.current_count);
    rdt_engine_term();
    if (owns_tracking) EXPECT_EQ(memtrack_shutdown(), 0u);
}

TEST(GradientImagePaintTest, RepeatExpansionAveragesUnresolvedPeriodsAndChecksCapacity) {
    rdt_engine_init(0);
    GradientStop stops[] = {{Color{0xff0000ffu}, 0, false, nullptr}, {Color{0xffff0000u}, 1e-12f, false, nullptr}};
    LinearGradient linear = {}; linear.angle = 90; linear.is_repeating = true;
    linear.stops = lam::own_arr(stops); linear.stop_count = 2;
    ListStyleImage image = {}; image.gradient_type = GRADIENT_LINEAR;
    image.linear_gradient = lam::shared(&linear);
    PaintList paint;
    EXPECT_FALSE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1e12f));
    EXPECT_EQ(paint_list_count(&paint), 0);
    ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1));
    ASSERT_EQ(paint_list_count(&paint), 1);
    const PaintFillLinearGradient& average = paint.data()[0].fill_linear_gradient;
    ASSERT_EQ(average.stop_count, 2);
    EXPECT_EQ(average.stops[0].r, 128); EXPECT_EQ(average.stops[0].g, 0);
    EXPECT_EQ(average.stops[0].b, 128); EXPECT_EQ(average.stops[0].a, 255);
    paint.clear();
    stops[1].position = .0005f;
    ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1e4f));
    ASSERT_EQ(paint_list_count(&paint), 1);
    EXPECT_GT(paint.data()[0].fill_linear_gradient.stop_count, 2000);
    EXPECT_TRUE(paint_ir_validate(&paint, nullptr));
    paint.clear();
    GradientStop coincident[] = {{Color{0xff0000ffu}, 0, false, nullptr},
        {Color{0xffffffffu}, 0, false, nullptr}, {Color{0xffff0000u}, 0, false, nullptr}};
    linear.stops = lam::own_arr(coincident); linear.stop_count = 3;
    ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1));
    const RdtGradientStop& equal_average = paint.data()[0].fill_linear_gradient.stops[0];
    EXPECT_EQ(equal_average.r, 191); EXPECT_EQ(equal_average.g, 128);
    EXPECT_EQ(equal_average.b, 191); EXPECT_EQ(equal_average.a, 255);
    paint.clear();
    coincident[1].position = .00025f; coincident[2].position = .001f;
    ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1));
    const RdtGradientStop& weighted_average = paint.data()[0].fill_linear_gradient.stops[0];
    EXPECT_EQ(weighted_average.r, 159); EXPECT_EQ(weighted_average.g, 128);
    EXPECT_EQ(weighted_average.b, 223); EXPECT_EQ(weighted_average.a, 255);
    paint.clear();
    stops[0] = {Color{0x00ff0000u}, 0, false, nullptr}; stops[1] = {Color{0xff0000ffu}, 0, false, nullptr};
    linear.stops = lam::own_arr(stops); linear.stop_count = 2;
    ASSERT_TRUE(render_paint_gradient_image(&paint, &image, {0, 0, 20, 20}, 1));
    const RdtGradientStop& transparent_average = paint.data()[0].fill_linear_gradient.stops[0];
    EXPECT_EQ(transparent_average.r, 255); EXPECT_EQ(transparent_average.g, 0);
    EXPECT_EQ(transparent_average.b, 0); EXPECT_EQ(transparent_average.a, 128);
    paint.destroy();
    rdt_engine_term();
}

TEST(PaintIrRasterTest, DensityKeepsRectangleCoverageAndOpaqueBackdrop) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList backdrop = {}; dl_init(&backdrop, arena);
    dl_fill_rect(&backdrop, 0, 0, 32, 32, Color{0xffff0000u});
    for (float density : {1.0f, 2.0f}) for (bool grouped : {false, true}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(grouped);
        PaintList paint;
        PaintEffectGroup group = {}; group.bounds = {0, 0, 8, 8};
        group.opacity = 1.0f; group.backdrop = true;
        if (grouped) paint_begin_effect_group(&paint, &group);
        paint_fill_rect(&paint, 0, 0, 8, 8, Color{0x59ffffffu});
        if (grouped) paint_end_effect_group(&paint);
        RdtMatrix scale = rdt_matrix_scale(density, density);
        scale.e13 = scale.e23 = 4.0f;
        Arena* foreground_arena = arena_create_default();
        DisplayList foreground = {}; dl_init(&foreground, foreground_arena);
        paint_ir_lower_raster_transformed(&paint, &foreground, &scale);
        ImageSurface* result = render_display_list_snapshot_with_backdrop(&foreground,
            mem_context_process(MEM_ROLE_RENDER), {0, 0, 32, 32}, density, Color{0}, &backdrop);
        ASSERT_NE(result, nullptr);
        uint32_t pixel = ((uint32_t*)result->pixels)[6 * 32 + 6];
        EXPECT_EQ(pixel >> 24, 255u);
        EXPECT_NEAR((float)(pixel & 255u), 89.0f, 1.0f);
        EXPECT_NEAR((float)((pixel >> 8) & 255u), 89.0f, 1.0f);
        EXPECT_EQ((pixel >> 16) & 255u, 255u);
        EXPECT_EQ(((uint32_t*)result->pixels)[0], 0xffff0000u);
        image_surface_destroy(result); dl_destroy(&foreground); arena_destroy(foreground_arena);
    }
    dl_destroy(&backdrop); arena_destroy(arena);
    rdt_engine_term();
}

TEST(ImagePaintTest, RasterBlitsRespectAlphaModesOpacityAndTransparentTargets) {
    const ScaleMode modes[] = {SCALE_MODE_NEAREST, SCALE_MODE_LINEAR, SCALE_MODE_PIXELATED};
    const unsigned destination_alphas[] = {0, 128, 255};
    const struct Size { unsigned width, height; } sizes[] = {{1, 1}, {2, 1}, {4, 2}};
    for (ScaleMode mode : modes) for (Size size : sizes)
        for (ImageAlphaMode source_mode : {IMAGE_ALPHA_STRAIGHT, IMAGE_ALPHA_PREMULTIPLIED})
            for (ImageAlphaMode destination_mode : {IMAGE_ALPHA_STRAIGHT, IMAGE_ALPHA_PREMULTIPLIED})
                for (uint8_t opacity : {uint8_t(255), uint8_t(128)})
                    for (unsigned destination_alpha : destination_alphas) {
                        uint32_t source_pixel = render_pixel_pack_abgr(
                            source_mode == IMAGE_ALPHA_STRAIGHT ? 255 : 128, 0, 0, 128);
                        uint32_t source_pixels[2] = {source_pixel, source_pixel};
                        uint32_t destination_pixels[8];
                        uint32_t background = render_pixel_pack_abgr(0, 0,
                            destination_mode == IMAGE_ALPHA_STRAIGHT ? 255 : destination_alpha, destination_alpha);
                        for (uint32_t& pixel : destination_pixels) pixel = background;
                        ImageSurface source = {}, destination = {};
                        source.width = 2; source.height = 1; source.pitch = 8;
                        source.pixels = source_pixels; source.alpha_mode = source_mode;
                        destination.width = size.width; destination.height = size.height;
                        destination.pitch = destination.width * 4;
                        destination.pixels = destination_pixels; destination.alpha_mode = destination_mode;
                        RasterPaintContext context = raster_paint_context(&destination, nullptr, nullptr, 0);
                        Rect rectangle = {0, 0, float(destination.width), float(destination.height)};
                        raster_blit_surface_scaled(&context, &source, nullptr, &rectangle, mode, opacity);
                        // Hand-computed source-over values cover clear, translucent and opaque targets.
                        unsigned red = opacity == 255 ? 128 : 64;
                        unsigned blue = destination_alpha == 0 ? 0 : destination_alpha == 255
                            ? opacity == 255 ? 127 : 191 : opacity == 255 ? 64 : 96;
                        unsigned alpha = destination_alpha == 255 ? 255 : destination_alpha == 0
                            ? red : opacity == 255 ? 192 : 160;
                        if (destination_mode == IMAGE_ALPHA_STRAIGHT) {
                            red = destination_alpha == 0 ? 255 : destination_alpha == 255 ? red
                                : opacity == 255 ? 170 : 102;
                            blue = destination_alpha == 0 ? 0 : destination_alpha == 255 ? blue
                                : opacity == 255 ? 85 : 153;
                        }
                        uint32_t expected = render_pixel_pack_abgr(red, 0, blue, alpha);
                        for (unsigned i = 0; i < size.width * size.height; ++i)
                            EXPECT_EQ(destination_pixels[i], expected) << mode << ":" << size.width << ":" << size.height << ":"
                                << source_mode << ":" << destination_mode << ":" << unsigned(opacity) << ":" << destination_alpha;
                        EXPECT_EQ(source_pixels[0], source_pixel);
                        EXPECT_EQ(source_pixels[1], source_pixel);
                    }
}

TEST(ImagePaintTest, PixelatedAffineFootprintsPreserveDevicePixelBlocks) {
    const struct { float xx, xy, yx, yy, weight; unsigned quadrants[4]; } cases[] = {
        {4, 0, 0, 4, 1, {0, 1, 2, 3}},
        {4, 0, 0, 3, 1, {0, 1, 2, 3}},
        {-4, 0, 0, 4, 1, {1, 0, 3, 2}},
        {4, 0, 0, -4, 1, {2, 3, 0, 1}},
        {0, -4, 4, 0, 1, {2, 0, 3, 1}},
        {8, 0, 0, 8, 2, {0, 1, 2, 3}}
    };
    for (float density : {1.0f, 2.0f}) for (const auto& item : cases)
        for (bool straight : {false, true}) for (unsigned alpha : {128u, 255u}) {
            uint32_t source[] = {
                render_pixel_pack_abgr(255, 0, 0, alpha),
                render_pixel_pack_abgr(0, 0, 255, alpha),
                render_pixel_pack_abgr(0, 255, 0, alpha),
                render_pixel_pack_abgr(255, 255, 255, alpha)
            };
            uint32_t expected[4];
            for (unsigned index = 0; index < 4; index++) {
                expected[index] = render_pixel_premultiply_abgr(source[index]);
                if (!straight) source[index] = expected[index];
            }
            RdtMatrix transform = {item.xx * density, item.xy * density, 16 * density * item.weight,
                item.yx * density, item.yy * density, 16 * density * item.weight, 0, 0, item.weight};
            Rect bounds = {};
            uint32_t* pixels = nullptr;
            ASSERT_TRUE(render_image_project_pixels(source, 2, 2, 2, {0, 0, 2, 2},
                &transform, {0, 0, 64 * density, 64 * density}, SCALE_MODE_PIXELATED,
                straight, &pixels, &bounds));
            ASSERT_NE(pixels, nullptr);
            unsigned width = (unsigned)bounds.width, height = (unsigned)bounds.height;
            EXPECT_FLOAT_EQ(bounds.width, 2 * density * (fabsf(item.xx) + fabsf(item.xy)) / item.weight);
            EXPECT_FLOAT_EQ(bounds.height, 2 * density * (fabsf(item.yx) + fabsf(item.yy)) / item.weight);
            for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
                unsigned quadrant = (y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0);
                EXPECT_EQ(pixels[y * width + x], expected[item.quadrants[quadrant]]) << x << "," << y;
            }
            mem_free(pixels);
        }
}

TEST(ImagePaintTest, AffineMinificationAveragesTheSourceFootprint) {
    for (bool straight : {false, true}) for (bool translucent : {false, true})
        for (bool flipped : {false, true}) {
            uint32_t source[9];
            for (unsigned i = 0; i < 9; i++) {
                source[i] = translucent ? (i == 4 ? 0u : 0x80ffffffu) : (i == 4 ? 0xff000000u : 0xffffffffu);
                if (!straight) source[i] = render_pixel_premultiply_abgr(source[i]);
            }
            RdtMatrix transform = rdt_matrix_scale(flipped ? -1.0f / 3.0f : 1.0f / 3.0f, 1.0f / 3.0f);
            if (flipped) transform.e13 = 1.0f;
            uint32_t* pixels = nullptr; Rect bounds = {};
            ASSERT_TRUE(render_image_project_pixels(source, 3, 3, 3, {0, 0, 3, 3},
                &transform, {0, 0, 1, 1}, SCALE_MODE_LINEAR, straight, &pixels, &bounds));
            ASSERT_NE(pixels, nullptr);
            EXPECT_EQ(pixels[0], translucent ? 0x72727272u : 0xffe3e3e3u);
            mem_free(pixels);
        }
}

TEST(ImagePaintTest, PixelatedProjectionChoosesClosestPositiveIntegerMultiple) {
    const uint32_t source[] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u};
    // 3 source pixels become 6 nearest pixels, then smooth to 7 device pixels.
    const uint32_t expected[] = {0xff0000ffu, 0xff0000ffu, 0xff00a45bu,
        0xff00ff00u, 0xff5ba400u, 0xffff0000u, 0xffff0000u};
    RdtMatrix transform = rdt_matrix_scale(7.0f / 3.0f, 3.0f);
    uint32_t* pixels = nullptr;
    Rect bounds = {};
    ASSERT_TRUE(render_image_project_pixels(source, 3, 1, 3, {0, 0, 3, 1},
        &transform, {0, 0, 7, 3}, SCALE_MODE_PIXELATED, false, &pixels, &bounds));
    ASSERT_NE(pixels, nullptr);
    ASSERT_FLOAT_EQ(bounds.width, 7); ASSERT_FLOAT_EQ(bounds.height, 3);
    for (unsigned y = 0; y < 3; y++) for (unsigned x = 0; x < 7; x++)
        EXPECT_EQ(pixels[y * 7 + x], expected[x]) << x << "," << y;
    mem_free(pixels);
}

TEST(ImagePaintTest, ProjectedImagesPreserveSourceOverClipsTilesAndBatchOrder) {
    rdt_engine_init(0);
    uint32_t source[256], full[520 * 4], tile[257 * 2];
    RdtPath* clip = rdt_path_new(); ASSERT_NE(clip, nullptr);
    rdt_path_add_rect(clip, 4, 0, 508, 4, 0, 0);
    for (float density : {1.0f, 2.0f})
        for (bool straight : {false, true})
            for (uint8_t opacity : {uint8_t(128), uint8_t(255)})
                for (uint32_t background : {0u, 0x80402010u, 0xffcf7f21u}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(straight);
        SCOPED_TRACE(unsigned(opacity));
        SCOPED_TRACE(background);
        for (unsigned coverage = 0; coverage < 256; coverage++) {
            uint32_t pixel = render_pixel_pack_abgr(173, 31, 124, coverage);
            source[coverage] = straight ? pixel : render_pixel_premultiply_abgr(pixel);
        }
        for (bool tiled : {false, true}) {
            uint32_t* pixels = tiled ? tile : full;
            for (size_t index = 0; index < (tiled ? 257u * 2u : 520u * 4u); index++) pixels[index] = 0;
            RdtVector vector = {};
            rdt_vector_init(&vector, pixels, tiled ? 257 : 520, tiled ? 2 : 4, tiled ? 257 : 520);
            if (tiled) { rdt_vector_set_tile_offset_x(&vector, 7); rdt_vector_set_tile_offset_y(&vector, 1); }
            rdt_vector_begin_batch(&vector);
            // keep a pending vector command before the direct compositor and another after it.
            rdt_fill_rect(&vector, 0, 0, 520, 4, Color{background});
            rdt_push_clip(&vector, clip, nullptr);
            RdtMatrix transform = rdt_matrix_scale(density, density);
            transform.e13 = 2; transform.e23 = 1;
            rdt_draw_image(&vector, source, 256, 1, 256, 0, 0, 256, 1, opacity,
                &transform, 0, straight, 0, SCALE_MODE_NEAREST);
            rdt_pop_clip(&vector);
            rdt_fill_rect(&vector, 10, 1, 2, 1, Color{0xff00ffffu});
            rdt_vector_end_batch(&vector);
            rdt_vector_destroy(&vector);
        }
        for (size_t row = 0; row < 4; row++) for (size_t column = 0; column < 520; column++) {
            uint32_t expected = render_pixel_premultiply_abgr(background);
            if (row >= 1 && float(row) < 1 + density && column >= 4 && column < 512 &&
                float(column) < 2 + 256 * density) {
                size_t index = (size_t)(float(column - 2) / density);
                uint32_t pixel = render_pixel_scale_premultiplied(
                    straight ? render_pixel_premultiply_abgr(source[index]) : source[index], opacity);
                expected = render_pixel_source_over_premultiplied_pair(expected, pixel);
            }
            if (row == 1 && column >= 10 && column < 12) expected = 0xff00ffffu;
            EXPECT_EQ(full[row * 520 + column], expected) << row << ',' << column;
        }
        for (size_t row = 0; row < 2; row++) for (size_t column = 0; column < 257; column++)
            EXPECT_EQ(tile[row * 257 + column], full[(row + 1) * 520 + column + 7]) << row << ',' << column;
    }
    rdt_path_free(clip); rdt_engine_term();
}

TEST(ImagePaintTest, RecycledPixelsFromDifferentSurfaceIdentitiesNeverReuseOldPaint) {
    rdt_engine_init(0);
    uint32_t source=0xff0000ffu,pixels[64]={};
    RdtVector vector={};rdt_vector_init(&vector,pixels,8,8,8);
    rdt_draw_image(&vector,&source,1,1,1,0,0,8,8,255,nullptr,1,false,41);
    EXPECT_EQ(pixels[36],0xff0000ffu);
    source=0xff00ff00u;
    rdt_draw_image(&vector,&source,1,1,1,0,0,8,8,255,nullptr,1,false,42);
    EXPECT_EQ(pixels[36],0xff00ff00u);
    rdt_vector_destroy(&vector);
}

TEST(SvgStrokeTest, MiterLimitSurvivesRecordingAndChangesCachedStrokeGeometry) {
    rdt_engine_init(0);
    RdtPath* path = svg_parse_path_d("M20 100L50 20L80 100");
    ASSERT_NE(path, nullptr);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    Color blue = {}; blue.b = blue.a = 255;
    float dashes[] = {1000.0f, 10.0f};
    paint_stroke_path(&paint, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        dashes, 2, 0.0f, nullptr, 1.0f);
    dashes[0] = 0.0f;
    ASSERT_TRUE(paint_ir_validate(&paint, nullptr));
    EXPECT_FLOAT_EQ(paint.data()[0].stroke_path.dash_array[0], 1000.0f);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy();
    EXPECT_FLOAT_EQ(dl.data()[0].stroke_path.miter_limit, 1.0f);
    uint32_t pixels[100 * 120] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 100, 120, 100);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 100 + 50], 0u);
    memset(pixels, 0, sizeof(pixels));
    rdt_stroke_path(&vector, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        nullptr, 0, 0, nullptr, 10.0f);
    EXPECT_EQ(pixels[10 * 100 + 50], 0xffff0000u);
    memset(pixels, 0, sizeof(pixels));
    rdt_stroke_path(&vector, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        nullptr, 0, 0, nullptr, 1.0f);
    EXPECT_EQ(pixels[10 * 100 + 50], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena); rdt_path_free(path);
}

TEST(RdtPathTransformTest, CompactEllipseAndRoundedRectangleExpandBeforeAffineStroke) {
    RdtPath* source = rdt_path_new();
    RdtPath* transformed = rdt_path_new();
    rdt_path_add_circle(source, 0, 0, 10, 4);
    RdtMatrix matrix = {2, 1, 10, 0, 3, 20, 0, 0, 1};
    ASSERT_TRUE(render_path_append_transformed(transformed, source, &matrix));
    float left, top, right, bottom;
    ASSERT_TRUE(rdt_path_get_bounds(transformed, &left, &top, &right, &bottom));
    EXPECT_NEAR(left, 10.0f - sqrtf(416.0f), .02f);
    EXPECT_NEAR(right, 10.0f + sqrtf(416.0f), .02f);
    EXPECT_FLOAT_EQ(top, 8.0f); EXPECT_FLOAT_EQ(bottom, 32.0f);
    rdt_path_free(source); rdt_path_free(transformed);
    source = rdt_path_new(); transformed = rdt_path_new();
    rdt_path_add_rect(source, 0, 0, 20, 10, 3, 2);
    RdtMatrix rotation = {0, -1, 50, 1, 0, 20, 0, 0, 1};
    ASSERT_TRUE(render_path_append_transformed(transformed, source, &rotation));
    ASSERT_TRUE(rdt_path_get_bounds(transformed, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(left, 40); EXPECT_FLOAT_EQ(right, 50);
    EXPECT_FLOAT_EQ(top, 20); EXPECT_FLOAT_EQ(bottom, 40);
    rdt_path_free(source); rdt_path_free(transformed);
}

TEST(SvgGradientTest, RecordingOwnsStrokeDashesAndReplayPreservesSpread) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, 10.0f, 10.0f, 60.0f, 30.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.5f, 255, 0, 0, 255}, {.5f, 0, 0, 255, 255}};
    float dashes[] = {10.0f, 10.0f};
    RdtGradientOptions options = {};
    options.spread = RDT_GRADIENT_REPEAT;
    options.stroke_width = 8.0f; options.miter_limit = 6.0f;
    options.dash_array = lam::up(dashes); options.dash_count = 2;
    paint_fill_linear_gradient(&paint, path, 10.0f, 10.0f, 30.0f, 10.0f,
        stops, 2, RDT_FILL_WINDING, nullptr, nullptr, &options);
    dashes[0] = 0.0f;
    ASSERT_TRUE(paint_ir_validate(&paint, nullptr));
    EXPECT_FLOAT_EQ(paint.data()[0].fill_linear_gradient.options.dash_array[0], 10.0f);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy(); rdt_path_free(path);
    const DlFillLinearGradient* gradient = &dl.data()[0].fill_linear_gradient;
    EXPECT_EQ(gradient->options.spread, RDT_GRADIENT_REPEAT);
    EXPECT_FLOAT_EQ(gradient->options.miter_limit, 6.0f);
    EXPECT_FLOAT_EQ(gradient->options.dash_array[0], 10.0f);
    uint32_t pixels[80 * 50] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 80, 50, 80);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 80 + 15], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 35], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 25], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena);
    rdt_engine_term();
}

TEST(SvgGradientTest, RetainedFragmentOwnsStrokeFactsAfterRecordingArenaDies) {
    rdt_engine_init(0);
    Pool* pool = pool_create(); Arena* arena = arena_create_default();
    DisplayList source = {}; dl_init(&source, arena);
    int begin = dl_begin_element(&source, 42, 0.0f, 0.0f, 80.0f, 50.0f);
    RdtPath* path = rdt_path_new(); rdt_path_add_rect(path, 10.0f, 10.0f, 60.0f, 30.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.5f, 255, 0, 0, 255}, {.5f, 0, 0, 255, 255}};
    float dashes[] = {10.0f, 10.0f};
    RdtGradientOptions options = {}; options.spread = RDT_GRADIENT_REPEAT;
    options.stroke_width = 8.0f; options.miter_limit = 6.0f; options.dash_array = lam::up(dashes); options.dash_count = 2;
    PaintList paint;
    paint_fill_linear_gradient(&paint, path, 10.0f, 10.0f, 30.0f, 10.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    StrBuf* svg = strbuf_new(); PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&paint, svg, nullptr, &stats);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_NE(strstr(svg->str, "spreadMethod=\"repeat\""), nullptr);
    EXPECT_NE(strstr(svg->str, "stroke=\"url(#paint-ir-linear-"), nullptr);
    EXPECT_NE(strstr(svg->str, "stroke-miterlimit=\"6.000\""), nullptr);
    strbuf_free(svg);
    paint_ir_lower_raster(&paint, &source); dl_end_element(&source, begin);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);
    paint.destroy(); dl_destroy(&source); arena_destroy(arena); rdt_path_free(path);
    memset(stops, 0, sizeof(stops)); memset(dashes, 0, sizeof(dashes));
    arena = arena_create_default(); DisplayList replay = {}; dl_init(&replay, arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_TRUE(dl_validate(&replay, nullptr));
    const DisplayItem command = test_radiant_command(&replay, 1);
    const DlFillLinearGradient* gradient = &command.fill_linear_gradient;
    EXPECT_EQ(gradient->options.spread, RDT_GRADIENT_REPEAT);
    EXPECT_FLOAT_EQ(gradient->options.miter_limit, 6.0f);
    EXPECT_FLOAT_EQ(gradient->options.dash_array[0], 10.0f);
    uint32_t pixels[80 * 50] = {}; RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 80, 50, 80);
    EXPECT_EQ(dl_replay_vector_item(&vector, &command), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 80 + 15], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 35], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 25], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&replay); arena_destroy(arena);
    retained_dl_cache_destroy(cache); pool_destroy(pool); rdt_engine_term();
}

TEST(SvgGradientTest, FocalAndSpreadFactsParticipateInBackendCacheKeys) {
    rdt_engine_init(0);
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, 0.0f, 0.0f, 60.0f, 60.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.2f, 255, 0, 0, 255}, {.2f, 0, 0, 255, 255}};
    uint32_t pixels[60 * 60] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 60, 60, 60);
    RdtGradientOptions options = {};
    options.has_focal = true; options.fx = 20.0f; options.fy = 30.0f;
    rdt_fill_radial_gradient(&vector, path, 30.0f, 30.0f, 25.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    EXPECT_EQ(pixels[30 * 60 + 20], 0xff0000ffu);
    EXPECT_EQ(pixels[30 * 60 + 40], 0xffff0000u);
    memset(pixels, 0, sizeof(pixels));
    options.fx = 40.0f;
    rdt_fill_radial_gradient(&vector, path, 30.0f, 30.0f, 25.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    EXPECT_EQ(pixels[30 * 60 + 40], 0xff0000ffu);
    EXPECT_EQ(pixels[30 * 60 + 20], 0xffff0000u);
    options = {};
    for (RdtGradientSpread spread : {RDT_GRADIENT_PAD, RDT_GRADIENT_REPEAT, RDT_GRADIENT_REFLECT}) {
        memset(pixels, 0, sizeof(pixels)); options.spread = spread;
        rdt_fill_linear_gradient(&vector, path, 0.0f, 0.0f, 20.0f, 0.0f, stops, 2,
            RDT_FILL_WINDING, nullptr, nullptr, &options);
        EXPECT_EQ(pixels[30 * 60 + 22], spread == RDT_GRADIENT_REPEAT ? 0xff0000ffu : 0xffff0000u);
    }
    rdt_vector_destroy(&vector); rdt_path_free(path); rdt_engine_term();
}

TEST(SvgPatternTest, NestedOffscreenClipsPreserveSuspendedPathsAcrossStackGrowth) {
    rdt_engine_init(0);
    uint32_t pixels[20 * 10] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 20, 10, 20);
    RdtPath* outer = rdt_path_new(); rdt_path_add_rect(outer, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
    RdtPath* inner = rdt_path_new(); rdt_path_add_rect(inner, 10.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
    rdt_push_clip(&vector, outer, nullptr);
    int saved = rdt_clip_save_depth();
    for (int i = 0; i < 9; i++) rdt_push_clip(&vector, inner, nullptr);
    Color blue = {}; blue.b = blue.a = 255;
    rdt_fill_rect(&vector, 0.0f, 0.0f, 20.0f, 10.0f, blue);
    EXPECT_EQ(pixels[5 * 20 + 5], 0u);
    EXPECT_EQ(pixels[5 * 20 + 15], 0xffff0000u);
    for (int i = 0; i < 9; i++) rdt_pop_clip(&vector);
    rdt_clip_restore_depth(saved);
    Color red = {}; red.r = red.a = 255;
    rdt_fill_rect(&vector, 0.0f, 0.0f, 20.0f, 10.0f, red);
    EXPECT_EQ(pixels[5 * 20 + 5], 0xff0000ffu);
    EXPECT_EQ(pixels[5 * 20 + 15], 0xffff0000u);
    rdt_pop_clip(&vector);
    rdt_path_free(inner); rdt_path_free(outer); rdt_vector_destroy(&vector); rdt_engine_term();
}

TEST(SvgImageTest, SharedGifAndWebPDecodersMatchPngAndRejectTruncation) {
    const char* paths[] = {"test/ui/svg_image_assets/split.png",
        "test/ui/svg_image_assets/split.gif", "test/ui/svg_image_assets/split.webp"};
    for (const char* path : paths) {
        int width = 0, height = 0, channels = 0;
        ASSERT_TRUE(image_get_dimensions(path, &width, &height));
        EXPECT_EQ(width, 20); EXPECT_EQ(height, 10);
        unsigned char* pixels = image_load(path, &width, &height, &channels, 4);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(channels, 4);
        EXPECT_EQ(pixels[0], 255); EXPECT_EQ(pixels[1], 0); EXPECT_EQ(pixels[2], 0);
        EXPECT_EQ(pixels[4 * 19], 0); EXPECT_EQ(pixels[4 * 19 + 2], 255);
        image_free(pixels);
        char* bytes = nullptr;
        size_t size = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_IMAGE, &bytes, &size));
        pixels = image_load_from_memory((unsigned char*)bytes, size, &width, &height, &channels);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(pixels[0], 255); EXPECT_EQ(pixels[4 * 19 + 2], 255);
        image_free(pixels);
        if (strstr(path, ".webp")) {
            EXPECT_FALSE(image_get_dimensions_from_memory((unsigned char*)bytes, 12, &width, &height));
            EXPECT_EQ(image_load_from_memory((unsigned char*)bytes, 12, &width, &height, &channels), nullptr);
        }
        mem_free(bytes);
    }
}

TEST(SvgImageTest, ReplayRefreshesPixelsAfterSharedDecodePromotion) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {};
    dl_init(&dl, arena);
    uint32_t old_pixels[] = {0xff0000ffu};
    uint32_t new_pixels[] = {0xffff0000u, 0xffff0000u};
    // shared decode owners now register the handle used by deferred replay.
    ImageSurface* image = image_surface_alloc();
    ASSERT_NE(image, nullptr); ASSERT_FALSE(image->self.is_null());
    image->width = image->height = 1;
    image->pitch = 4;
    image->pixels = old_pixels;
    image->generation = 1;
    dl_draw_image(&dl, old_pixels, 1, 1, 1, 0.0f, 0.0f, 8.0f, 8.0f,
        255, nullptr, image, image->generation);
    image->pixels = new_pixels;
    image->decoded_width = 2;
    image->decoded_height = 1;
    image->pitch = 8;
    image->generation++;
    uint32_t pixels[8 * 8] = {};
    RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 8, 8, 8);
    rdt_vector_begin_batch(&vector);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(pixels[4 * 8 + 4], 0xffff0000u);
    image_surface_detach_pixels(image); image_surface_destroy(image);
    memset(pixels, 0, sizeof(pixels));
    rdt_vector_begin_batch(&vector);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0]), DL_REPLAY_VECTOR_DREW);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(pixels[4 * 8 + 4], 0u);
    rdt_vector_destroy(&vector);
    dl_destroy(&dl);
    arena_destroy(arena);
    rdt_engine_term();
}

TEST(SvgCascadeTest, InlineDefaultVisibilityUsesVisibilityDomain) {
    EXPECT_EQ(INLINE_PROP_DEFAULT.visibility, VIS_VISIBLE);
}

TEST(BorderRadiusMath, RepeatedPaintAndResizeStartFromComputedRadii) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    CssValue* terms[] = {css_value_create_percentage(pool, 50.0),
        css_value_create_keyword(pool, "+"), css_value_create_length(pool, 30.0, CSS_UNIT_PX)};
    CssValue sum = {};
    sum.type = CSS_VALUE_TYPE_LIST;
    sum.data.list.values = terms;
    sum.data.list.count = 3;
    CssValue* arguments[] = {&sum};
    CssValue* expression = css_value_create_function(pool, "calc", arguments, 1);
    ASSERT_NE(expression, nullptr);
    CornerExpressions source = {};
    source.computed.top_right = source.computed.top_right_y = 80.0f;
    source.horizontal[0] = source.vertical[0] = lam::up(expression);
    Corner used = source.computed;
    used.expressions = lam::up(&source);
    for (int paint = 0; paint < 4; paint++) {
        resolve_border_radius_percentages(&used, 100.0f, 100.0f);
        constrain_corner_radii(&used, 100.0f, 100.0f);
        EXPECT_FLOAT_EQ(used.top_left, 50.0f);
        EXPECT_FLOAT_EQ(used.top_right, 50.0f);
        EXPECT_FLOAT_EQ(used.top_left_y, 50.0f);
    }
    resolve_border_radius_percentages(&used, 200.0f, 100.0f);
    constrain_corner_radii(&used, 200.0f, 100.0f);
    EXPECT_NEAR(used.top_left, 130.0f * 200.0f / 210.0f, 0.001f);
    EXPECT_NEAR(used.top_right, 80.0f * 200.0f / 210.0f, 0.001f);
    EXPECT_FLOAT_EQ(source.computed.top_right, 80.0f);
    pool_destroy(pool);
}

TEST(SvgCascadeTest, FontShorthandProjectsResetsAndPreservesDeferredVariables) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* source = "font:italic 700 24px/1.5 Missing, SVG-Ahem; font:20px SVG-Ahem; font:var(--face); font:20px";
    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text(source, strlen(source), pool, &count);
    ASSERT_EQ(count, 3u); ASSERT_NE(declarations, nullptr);
    const CssValue* size = css_font_shorthand_longhand(declarations[0]->value, "font-size", pool);
    ASSERT_NE(size, nullptr); EXPECT_EQ(size->type, CSS_VALUE_TYPE_LENGTH); EXPECT_EQ(size->data.length.value, 24);
    const CssValue* weight = css_font_shorthand_longhand(declarations[0]->value, "font-weight", pool);
    ASSERT_NE(weight, nullptr); EXPECT_EQ(weight->data.number.value, 700);
    const CssValue* families = css_font_shorthand_longhand(declarations[0]->value, "font-family", pool);
    ASSERT_NE(families, nullptr); EXPECT_EQ(families->data.list.count, 2); EXPECT_TRUE(families->data.list.comma_separated);
    const CssValue* reset = css_font_shorthand_longhand(declarations[1]->value, "font-weight", pool);
    ASSERT_NE(reset, nullptr); EXPECT_EQ(reset->type, CSS_VALUE_TYPE_KEYWORD); EXPECT_EQ(reset->data.keyword, CSS_VALUE_NORMAL);
    EXPECT_EQ(declarations[2]->value->type, CSS_VALUE_TYPE_FUNCTION);
    pool_destroy(pool);
}

TEST(CssVariableSubstitutionTest, CommasRegroupFunctionArgumentsAndAdjacentTokens) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    const char* text =
        "--value:linear-gradient(to right,red var(--missing,0%,blue) 100%)";
    CssDeclaration* declaration = css_parse_declaration_text(text, strlen(text), pool);
    ASSERT_NE(declaration, nullptr);
    const CssValue* value = css_resolve_var_value(pool, declaration->value, nullptr, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type, CSS_VALUE_TYPE_FUNCTION);
    const CssFunction* function = value->data.function;
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->arg_count, 3);
    for (int i = 1; i < 3; i++) {
        ASSERT_NE(function->args[i], nullptr);
        ASSERT_EQ(function->args[i]->type, CSS_VALUE_TYPE_LIST);
        EXPECT_FALSE(function->args[i]->data.list.comma_separated);
        EXPECT_EQ(function->args[i]->data.list.count, 2);
    }
    EXPECT_EQ(function->args[1]->data.list.values[1]->data.percentage.value, 0);
    EXPECT_EQ(function->args[2]->data.list.values[1]->data.percentage.value, 100);
    pool_destroy(pool);
}

TEST(CssVariableSubstitutionTest, EmptyFallbackIsRemovedButMissingValueIsInvalid) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    const char* sources[] = {"--value:var(--missing,) 4px", "--value:var(--missing) 4px"};
    for (int i = 0; i < 2; i++) {
        CssDeclaration* declaration = css_parse_declaration_text(sources[i], strlen(sources[i]), pool);
        ASSERT_NE(declaration, nullptr);
        const CssValue* value = css_resolve_var_value(pool, declaration->value, nullptr, nullptr);
        if (i == 0) {
            ASSERT_NE(value, nullptr);
            ASSERT_EQ(value->type, CSS_VALUE_TYPE_LENGTH);
            EXPECT_EQ(value->data.length.value, 4);
        } else {
            EXPECT_EQ(value, nullptr);
        }
    }
    pool_destroy(pool);
}

TEST(CssVariableSubstitutionTest, ComputedNamesUseWholeFirstArgumentAndKeepFallbackLazy) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    const char* source = "--name:--target;--target:12px;--pointer:--name;--empty: ;"
        "--cycle:var(--cycle);--fragment:--tar";
    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text(source, strlen(source), pool, &count);
    ASSERT_EQ(count, 6u);
    struct Lookup {CssDeclaration** declarations; size_t count;} lookup = {declarations, count};
    auto lookup_value = [](void* context, DomElement*, const char* name,
        DomElement** owner) -> const CssValue* {
        *owner = nullptr;
        Lookup* lookup = (Lookup*)context;
        for (size_t index = 0; index < lookup->count; index++) {
            CssDeclaration* declaration = lookup->declarations[index];
            if (strcmp(declaration->property_name, name) == 0) return declaration->value;
        }
        return nullptr;
    };
    struct Sample {const char* source; double pixels;} samples[] = {
        {"var(var(--name),9px)", 12},
        {"var(var(var(--pointer)),9px)", 12},
        {"var(var(--missing,--target),9px)", 12},
        {"var(--target var(--empty),9px)", 12},
        {"var(--target,var(--cycle))", 12},
        {"var(\"--target\",9px)", 9},
        {"var(--target --name,9px)", 9},
        {"var(var(--empty),9px)", 9},
        {"var(var(--cycle),9px)", 9},
        {"var(var(--fragment)get,9px)", 9}
    };
    for (const Sample& sample : samples) {
        CssDeclaration* declaration = css_parse_property_declaration("--result", 8,
            sample.source, strlen(sample.source), pool);
        ASSERT_NE(declaration, nullptr) << sample.source;
        const CssValue* value = css_resolve_var_value(pool, declaration->value,
            lookup_value, &lookup);
        ASSERT_NE(value, nullptr) << sample.source;
        ASSERT_EQ(value->type, CSS_VALUE_TYPE_LENGTH) << sample.source;
        EXPECT_EQ(value->data.length.value, sample.pixels) << sample.source;
    }
    pool_destroy(pool);
}

TEST(CssVariableSubstitutionTest, DocumentLimitsBoundBytesTokensAndAuthoredText) {
    Input input = {};
    DomDocument doc = {};
    ASSERT_TRUE(doc.init(&input));
    struct Cleanup {
        DomDocument* doc;
        CssEngine* engine = nullptr;
        ~Cleanup() {
            if (engine) css_engine_destroy(engine);
            doc->services.cached_css_engine = nullptr;
            css_property_system_cleanup();
            doc->destroy();
        }
    } cleanup = {&doc};
    DomElement* node = DomElement::create(&doc, "div", nullptr);
    ASSERT_NE(node, nullptr);
    doc.root = lam::up(node);
    CssEngine* engine = css_engine_create(doc.document_pool);
    ASSERT_NE(engine, nullptr);
    cleanup.engine = engine;
    doc.services.cached_css_engine = engine;
    EXPECT_EQ(engine->limits.max_substitution_bytes, CSS_SUBSTITUTION_DEFAULT_MAX_BYTES);
    EXPECT_EQ(engine->limits.max_substitution_tokens, CSS_SUBSTITUTION_DEFAULT_MAX_TOKENS);
    EXPECT_EQ(engine->limits.max_substitution_depth, CSS_SUBSTITUTION_DEFAULT_MAX_DEPTH);
    struct Sample {const char* text; size_t bytes, tokens; bool valid;} samples[] = {
        {"abcdefghijklmnop", 16, 4, true}, {"abcdefghijklmnopq", 16, 4, false},
        {"éééééééé", 16, 4, true}, {"ééééééééé", 16, 4, false},
        {"a b c d", 16, 4, true}, {"a b c d e", 16, 4, false},
        {"f(a,b)", 16, 5, true}, {"f(a,b)", 16, 4, false},
        {"/*123456789*/ x", 16, 4, true}, {"/*123456789012*/ x", 16, 4, false},
        {"var(--missing,abcdefghijklmnop)", 16, 4, true},
        {"var(--missing,abcdefghijklmnopq)", 16, 4, false}
    };
    for (const Sample& sample : samples) {
        css_engine_set_substitution_limits(engine, sample.bytes, sample.tokens);
        CssDeclaration* declaration = css_parse_property_declaration("--value", 7,
            sample.text, strlen(sample.text), doc.document_pool);
        ASSERT_NE(declaration, nullptr) << sample.text;
        CssCustomProp entry = {};
        entry.name = lam::up(declaration->property_name);
        entry.value = lam::up(declaration->value);
        entry.value_text = lam::up(declaration->value_text);
        entry.value_text_len = declaration->value_text_len;
        // the entry borrows stack storage only for this resolver call (D4.5.1v4).
        node->css_variables = lam::own(&entry);
        StrView text = {};
        const CssValue* value = css_compute_element_custom_property_text(doc.document_pool,
            node, "--value", 7, &text);
        EXPECT_EQ(value != nullptr, sample.valid) << sample.text;
        node->css_variables = nullptr;
        if (css_value_contains_var_reference(declaration->value)) {
            const CssValue* substituted = css_resolve_var_value(doc.document_pool,
                declaration->value, nullptr, nullptr, node);
            EXPECT_EQ(substituted != nullptr, sample.valid) << sample.text;
        }
        // owned token values must obey the same document budgets as typed custom properties.
        CssValue* owned = css_value_create_token_sequence(doc.document_pool, declaration->value,
            strview_from_cstr(sample.text));
        ASSERT_NE(owned, nullptr);
        const CssValue* substituted = css_resolve_var_value(doc.document_pool, owned, nullptr, nullptr, node);
        EXPECT_EQ(substituted != nullptr, sample.valid) << sample.text;
    }
    css_engine_set_substitution_limits(engine, 0, 0);
    EXPECT_EQ(engine->limits.max_substitution_bytes, CSS_SUBSTITUTION_DEFAULT_MAX_BYTES);
    EXPECT_EQ(engine->limits.max_substitution_tokens, CSS_SUBSTITUTION_DEFAULT_MAX_TOKENS);
    css_engine_set_substitution_depth_limit(engine, 4);
    ASSERT_EQ(dom_element_apply_inline_style(node,
        "--a:5px;--b:var(--a);--c:var(--b);--d:var(--c);--e:var(--d)"), 5);
    const CssValue* depth_value = css_compute_element_custom_property(doc.document_pool, node, "--d");
    ASSERT_NE(depth_value, nullptr);
    ASSERT_EQ(depth_value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_EQ(depth_value->data.length.value, 5.0);
    EXPECT_EQ(css_compute_element_custom_property(doc.document_pool, node, "--e"), nullptr);
    css_engine_set_substitution_depth_limit(engine, 0);
    EXPECT_EQ(engine->limits.max_substitution_depth, CSS_SUBSTITUTION_DEFAULT_MAX_DEPTH);
    EXPECT_NE(css_compute_element_custom_property(doc.document_pool, node, "--e"), nullptr);
}

TEST(CssVariableSubstitutionTest, DependencyDepthIncludesSharedCachePathsAndKeepsFallbackLazy) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    struct Cleanup {Pool* pool; ~Cleanup() {pool_destroy(pool);}} cleanup = {pool};
    struct Lookup {CssDeclaration* nodes[CSS_SUBSTITUTION_DEFAULT_MAX_DEPTH + 1];} lookup = {};
    for (int index = 0; index <= CSS_SUBSTITUTION_DEFAULT_MAX_DEPTH; index++) {
        char name[32], text[64];
        snprintf(name, sizeof(name), "--depth%d", index);
        if (index) snprintf(text, sizeof(text), "var(--depth%d)", index - 1);
        else str_copy(text, sizeof(text), "5px", 3);
        lookup.nodes[index] = css_parse_property_declaration(name, strlen(name), text, strlen(text), pool);
        ASSERT_NE(lookup.nodes[index], nullptr);
    }
    auto lookup_value = [](void* data, DomElement*, const char* name,
        DomElement** owner) -> const CssValue* {
        *owner = nullptr;
        Lookup* lookup = (Lookup*)data;
        for (CssDeclaration* node : lookup->nodes)
            if (strcmp(node->property_name, name) == 0) return node->value;
        return nullptr;
    };
    struct Sample {const char* source; const char* expected;} samples[] = {
        {"var(--depth127)", "5px"},
        {"var(--depth128)", nullptr},
        {"var(--depth128,9px)", "9px"},
        {"var(--depth0) var(--depth128,9px)", "5px 9px"},
        {"var(--depth0,var(--depth128))", "5px"}
    };
    for (bool owned : {false, true}) {
        if (owned) {
            for (CssDeclaration* node : lookup.nodes) {
                node->value = css_value_create_token_sequence(pool, node->value,
                    strview_init(node->value_text, node->value_text_len));
                ASSERT_NE(node->value, nullptr);
            }
        }
        for (const Sample& sample : samples) {
            CssDeclaration* declaration = css_parse_property_declaration("--result", 8,
                sample.source, strlen(sample.source), pool);
            ASSERT_NE(declaration, nullptr);
            const CssValue* value = css_resolve_var_value(pool, declaration->value, lookup_value, &lookup);
            if (!sample.expected) {EXPECT_EQ(value, nullptr); continue;}
            ASSERT_NE(value, nullptr) << sample.source;
            CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
            ASSERT_NE(formatter, nullptr);
            css_format_value(formatter, (CssValue*)value);
            EXPECT_STREQ(stringbuf_to_string(formatter->output)->chars, sample.expected) << sample.source;
        }
    }
}

TEST_F(SvgAnimationLifetimeTest, CustomPropertyInheritanceDoesNotConsumeDependencyDepth) {
    ASSERT_TRUE(root->set_attribute("style", "--base:7px;--alias:var(--base)"));
    DomElement* descendant = root;
    for (int index = 0; index < 600; index++) {
        descendant = element("div", descendant);
        ASSERT_NE(descendant, nullptr);
    }
    ASSERT_TRUE(descendant->set_attribute("style", "--base:99px"));
    Pool* scratch = pool_create();
    ASSERT_NE(scratch, nullptr);
    struct Cleanup {Pool* pool; ~Cleanup() {pool_destroy(pool);}} cleanup = {scratch};
    const CssValue* value = css_compute_element_custom_property(scratch, descendant, "--alias");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_EQ(value->data.length.value, 7.0);
    const CssValue* declaration = css_parse_property_declaration("width", 5,
        "var(--alias)", 12, scratch)->value;
    EXPECT_TRUE(css_value_depends_on_custom_property(scratch, descendant, declaration, root, "--base"));
}

TEST(CssVariableSubstitutionTest, RepeatedEmptyDependenciesAreMemoizedWithinEachCall) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    struct Cleanup {Pool* pool; ~Cleanup() {pool_destroy(pool);}} cleanup = {pool};
    struct Lookup {CssDeclaration* nodes[32]; unsigned calls;} lookup = {};
    for (int index = 0; index < 32; index++) {
        char name[24], value[80];
        snprintf(name, sizeof(name), "--empty%d", index);
        if (index) snprintf(value, sizeof(value), "var(--empty%d)var(--empty%d)", index - 1, index - 1);
        else snprintf(value, sizeof(value), "var(--missing,)");
        lookup.nodes[index] = css_parse_property_declaration(name, strlen(name), value, strlen(value), pool);
        ASSERT_NE(lookup.nodes[index], nullptr);
    }
    auto lookup_value = [](void* context, DomElement*, const char* name,
        DomElement** owner) -> const CssValue* {
        *owner = nullptr;
        Lookup* lookup = (Lookup*)context;
        lookup->calls++;
        for (CssDeclaration* node : lookup->nodes)
            if (strcmp(node->property_name, name) == 0) return node->value;
        return nullptr;
    };
    CssDeclaration* declaration = css_parse_property_declaration("--result", 8,
        "var(--empty31)", 14, pool);
    ASSERT_NE(declaration, nullptr);
    for (bool owned : {false, true}) {
        if (owned) {
            for (CssDeclaration* node : lookup.nodes) {
                node->value = css_value_create_token_sequence(pool, node->value,
                    strview_init(node->value_text, node->value_text_len));
                ASSERT_NE(node->value, nullptr);
            }
        }
        for (int call = 0; call < 2; call++) {
            lookup.calls = 0;
            const CssValue* value = css_resolve_var_value(pool, declaration->value, lookup_value, &lookup);
            ASSERT_NE(value, nullptr);
            ASSERT_EQ(value->type, CSS_VALUE_TYPE_LIST);
            EXPECT_EQ(value->data.list.count, 0);
            // repeated paths collapse to the 32 distinct dependencies in either representation.
            EXPECT_GT(lookup.calls, 32u);
            EXPECT_LE(lookup.calls, 64u);
        }
    }
}

static const CssValue* css_token_test_lookup(void* context, DomElement*, const char* name,
                                            DomElement** owner) {
    if (owner) *owner = nullptr;
    return strcmp(name, "--t1") == 0 ? (const CssValue*)context : nullptr;
}

TEST(CssVariableSubstitutionTest, InvalidOwnedTokensDoNotRetainTemporaryStorage) {
    Pool* source = pool_create();
    Pool* retained = pool_create();
    ASSERT_NE(source, nullptr);
    ASSERT_NE(retained, nullptr);
    const char* expression = "var(--missing)";
    const char* declaration_text = "--value:var(--missing)";
    CssDeclaration* declaration = css_parse_declaration_text(declaration_text,
        strlen(declaration_text), source);
    ASSERT_NE(declaration, nullptr);
    CssValue* value = css_value_create_token_sequence(source, declaration->value,
        strview_from_cstr(expression));
    ASSERT_NE(value, nullptr);
    PoolStats before = {};
    pool_get_detailed_stats(retained, &before);
    for (size_t i = 0; i < 128; i++) {
        EXPECT_EQ(css_resolve_var_value(retained, value, nullptr, nullptr), nullptr);
    }
    PoolStats after = {};
    pool_get_detailed_stats(retained, &after);
    EXPECT_EQ(after.live_bytes, before.live_bytes);
    pool_destroy(retained);
    pool_destroy(source);
}

TEST(CssVariableSubstitutionTest, PublicComputedTextRetainsSubstitutedSpelling) {
    Input input = {};
    DomDocument document;
    ASSERT_TRUE(document.init(&input));
    DomElement* element = DomElement::create(&document, "div", nullptr);
    ASSERT_NE(element, nullptr);
    document.root = lam::up(element);
    ASSERT_TRUE(element->set_attribute("style", "--base:12px;--result:var(--base)"));
    Pool* retained = pool_create();
    ASSERT_NE(retained, nullptr);
    StrView text = {};
    const CssValue* value = css_compute_element_custom_property_text(retained,
        element, "--result", 8, &text);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(text.str, nullptr);
    EXPECT_EQ(text.length, 4u);
    EXPECT_STREQ(text.str, "12px");
    EXPECT_TRUE(pool_owns(retained, text.str));
    document.destroy();
    EXPECT_STREQ(text.str, "12px");
    pool_destroy(retained);
}

TEST(CssVariableSubstitutionTest, OwnedTokenSpellingSurvivesSubstitutionBoundaries) {
    struct Case {const char* variable; const char* expression; const char* expected;};
    const Case cases[] = {
        {"a", "var(--t1)b", "a/**/b"},
        {"foo", "var(--t1)()", "foo/**/()"},
        {"a/* edge */", "var(--t1)b", "a/**/b"},
        {"b", "a/* edge */var(--t1)", "a/**/b"},
        {"unused", "a/* interior */b", "a/* interior */b"},
        {"'a/* unfinished '", "var(--t1)b", "'a/* unfinished 'b"},
        {"'a \" '/* edge */", "var(--t1)b", "'a \" 'b"},
        {"1e2", "var(--t1)%", "1e2/**/%"},
        {"REd", "var(--t1)", "REd"},
        {"a", "var(--t1)var(--missing,)b", "a/**/b"},
        {"a", "var(--missing, 'Q',var(--t1))", " 'Q',a"},
        {"a", "fn(var(--t1),  01.00)", "fn(a,  01.00)"},
        {"a", "var(--t1) b", "a b"},
        {"a", "var(--missing,  x )b", "  x b"},
        {"\\61", "var(--t1)b", "\\61/**/b"},
        {"a", "var(--missing,/**/)b", "b"},
    };
    for (const Case& test : cases) {
        SCOPED_TRACE(test.expression);
        Pool* source = pool_create();
        Pool* owned = pool_create();
        ASSERT_NE(source, nullptr); ASSERT_NE(owned, nullptr);
        auto parse = [&](const char* text) {
            StringBuf* declaration = stringbuf_new(source);
            stringbuf_append_all(declaration, 2, "--value:", text);
            CssDeclaration* parsed = css_parse_declaration_text(declaration->str->chars, declaration->length, source);
            if (parsed) parsed->value = css_value_create_token_sequence(source, parsed->value, strview_from_cstr(text));
            return parsed;
        };
        CssDeclaration* variable = parse(test.variable);
        CssDeclaration* expression = parse(test.expression);
        ASSERT_NE(variable, nullptr); ASSERT_NE(expression, nullptr);
        CssValue* retained_variable = css_value_clone_owned(variable->value, owned);
        CssValue* retained_expression = css_value_clone_owned(expression->value, owned);
        ASSERT_NE(retained_variable, nullptr); ASSERT_NE(retained_expression, nullptr);
        pool_destroy(source);
        EXPECT_EQ(css_value_contains_var_reference(retained_expression),
            strstr(test.expression, "var(") != nullptr);
        const CssValue* result = css_resolve_var_value(owned, retained_expression,
            css_token_test_lookup, retained_variable, nullptr, true);
        ASSERT_NE(result, nullptr);
        if (strcmp(test.expected, "a/**/b") == 0) {
            const CssValue* typed = css_value_unwrap(result);
            ASSERT_EQ(typed->type, CSS_VALUE_TYPE_LIST);
            EXPECT_EQ(typed->data.list.count, 2);
        }
        CssFormatter* formatter = css_formatter_create(owned, CSS_FORMAT_COMPACT);
        ASSERT_NE(formatter, nullptr);
        formatter->options.preserve_tokens = true;
        css_format_value(formatter, (CssValue*)result);
        EXPECT_EQ(formatter->output->length, strlen(test.expected));
        EXPECT_STREQ(formatter->output->str->chars, test.expected);
        css_formatter_destroy(formatter);
        css_value_destroy_owned(retained_variable, owned);
        css_value_destroy_owned(retained_expression, owned);
        pool_destroy(owned);
    }
}

TEST(SvgConditionalTest, LanguageMatchingUsesPreferencePrefixesAndRefreshesDocumentEpoch) {
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, nullptr, ""));
    EXPECT_FALSE(dom_svg_conditions_match("", nullptr, "en"));
    EXPECT_FALSE(dom_svg_conditions_match("https://example.invalid/extension", nullptr, "en"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "", "en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, " zz, EN-gb ", "en"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "en", "en-GB"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "english", "en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, "en", "en-GB, en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, "fr-FR", "de, fr"));
    DomDocument document;
    uint64_t epoch = document.mutation_epoch;
    ASSERT_TRUE(dom_document_set_preferred_languages(&document, "fr, de"));
    EXPECT_STREQ(dom_document_preferred_languages(&document), "fr, de");
    EXPECT_EQ(document.mutation_epoch, epoch + 1);
    ASSERT_TRUE(dom_document_set_preferred_languages(&document, "fr, de"));
    EXPECT_EQ(document.mutation_epoch, epoch + 1);
    document.destroy();
}

TEST(SvgCascadeTest, InlineImportanceAndSelectorListSpecificity) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    CssEngine* engine = css_engine_create(pool);
    SelectorMatcher* matcher = selector_matcher_create(pool);
    DomElement node = {};
    node.tag_name = lam::up("rect");
    node.id = lam::up("target");
    const char* classes[] = {"paint"};
    node.class_names = lam::own_arr(classes);
    node.class_count = 1;
    CssStylesheet* sheets[] = {css_parse_stylesheet(engine,
        ".paint { fill:blue !important; } .paint, #target { stroke:red; } .paint { stroke:blue; }"
        ".paint { marker:url(#all); marker-end:url(#end); } #target { marker-start:none !important; }",
        nullptr)};
    size_t inline_count = 0;
    const char* inline_text = "fill:red !important";
    CssDeclaration** declarations = css_parse_declaration_list_text(
        inline_text, strlen(inline_text), pool, &inline_count);
    ASSERT_EQ(inline_count, 1u);
    ASSERT_TRUE(declarations[0]->important);
    CssDeclaration result = {};
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        declarations, inline_count, "fill", &result));
    EXPECT_TRUE(result.specificity.inline_style);
    EXPECT_STREQ(result.value_text, "red");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-start", &result));
    EXPECT_STREQ(result.value_text, "none");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-mid", &result));
    EXPECT_STREQ(result.value_text, "url(#all)");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-end", &result));
    EXPECT_STREQ(result.value_text, "url(#end)");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "stroke", &result));
    EXPECT_EQ(result.specificity.ids, 1u);
    EXPECT_STREQ(result.value_text, "red");
    selector_matcher_destroy(matcher);
    css_engine_destroy(engine);
    css_property_system_cleanup();
    pool_destroy(pool);
}

TEST(SvgCascadeTest, CustomPropertiesDoNotAliasOrdinaryPropertyQueries) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    SelectorMatcher* matcher = selector_matcher_create(pool);
    ASSERT_NE(matcher, nullptr);
    DomElement node = {};
    node.tag_name = lam::up("rect");
    const char* source = "fill:red;--fill:blue!important;--Fill:green";
    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text(source, strlen(source), pool, &count);
    ASSERT_EQ(count, 3u);
    const char* properties[] = {"fill", "--fill", "--Fill"};
    const char* expected[] = {"red", "blue", "green"};
    for (int i = 0; i < 3; i++) {
        CssDeclaration result = {};
        ASSERT_TRUE(css_select_element_declaration(nullptr, matcher, &node, nullptr, 0,
            declarations, count, properties[i], &result));
        EXPECT_STREQ(result.value_text, expected[i]) << properties[i];
    }
    selector_matcher_destroy(matcher);
    css_property_system_cleanup();
    pool_destroy(pool);
}

TEST(SvgCascadeTest, InvalidPaintDoesNotDisplaceValidDeclaration) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    size_t count = 0;
    const char* text = "fill:red;fill:invalid-color;stroke:context-fill";
    CssDeclaration** declarations = css_parse_declaration_list_text(text, strlen(text), pool, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(declarations[0]->value_text, "red");
    EXPECT_STREQ(declarations[1]->value_text, "context-fill");
    css_property_system_cleanup();
    pool_destroy(pool);
}

TEST_F(SvgAnimationLifetimeTest, ResolvedHostPresentationMatchesFreshCascadeAcrossMutation) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("style", "fill:red;stroke:blue;transform-box:fill-box"));
    ASSERT_NE(rect->specified_style, nullptr);
    char fresh[64] = {}, resolved[64] = {};
    ASSERT_NE(svg_get_dom_presentation_property(rect, "fill", true,
        fresh, sizeof(fresh)), nullptr);
    ASSERT_TRUE(rect->set_attribute("data-pass", "resolved"));
    rect->set_styles_resolved(true);
    rect->set_needs_style_recompute(false);
    ASSERT_NE(svg_get_dom_presentation_property(rect, "fill", true,
        resolved, sizeof(resolved)), nullptr);
    EXPECT_STREQ(resolved, fresh);
    ASSERT_NE(svg_get_dom_presentation_property(rect, "transform-box", false,
        resolved, sizeof(resolved)), nullptr);
    EXPECT_STREQ(resolved, "fill-box");

    ASSERT_TRUE(rect->set_attribute("style", "fill:green;stroke:blue"));
    ASSERT_NE(svg_get_dom_presentation_property(rect, "fill", true,
        fresh, sizeof(fresh)), nullptr);
    EXPECT_STREQ(fresh, "green");
    ASSERT_TRUE(rect->set_attribute("data-pass", "updated"));
    rect->set_styles_resolved(true);
    rect->set_needs_style_recompute(false);
    ASSERT_NE(svg_get_dom_presentation_property(rect, "fill", true,
        resolved, sizeof(resolved)), nullptr);
    EXPECT_STREQ(resolved, fresh);
}

TEST_F(SvgAnimationLifetimeTest, TransitionBaseCacheKeepsAnimatedPaintAndRetargetingSeparate) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("fill", "red"));
    ASSERT_TRUE(rect->set_attribute("style", "transition:fill 2s linear"));
    LayoutContext layout = {};
    layout.doc = lam::up(&doc);
    layout.ui_context = lam::up(&ui);
    layout.pool = lam::up(doc.document_pool.get());
    layout.run_mode = radiant::RunMode::PerformLayout;
    AnimationScheduler* scheduler = doc.state->animation_scheduler;
    ASSERT_NE(scheduler, nullptr);
    css_transition_resolve(rect, &layout);
    ASSERT_TRUE(rect->set_attribute("fill", "blue"));
    css_transition_resolve(rect, &layout);
    ASSERT_EQ(scheduler->count, 1);
    AnimationInstance* original = scheduler->first;
    animation_scheduler_tick(scheduler, 1.0, nullptr);
    char paint[64] = {};
    ASSERT_NE(svg_get_dom_presentation_property(rect, "fill", true,
        paint, sizeof(paint)), nullptr);
    EXPECT_STREQ(paint, "rgb(128, 0, 128)");

    // repeated measurements retain the blue target while paint observes its midpoint.
    for (unsigned repeat = 0; repeat < 8; repeat++) {
        css_transition_resolve(rect, &layout);
        ASSERT_EQ(scheduler->count, 1);
        EXPECT_EQ(scheduler->first, original);
        auto* active = static_cast<CssTransitionState*>(scheduler->first->state);
        EXPECT_EQ(active->to.value.color.b, 255);
        EXPECT_EQ(active->to.value.color.r, 0);
    }
    ASSERT_TRUE(rect->set_attribute("fill", "green"));
    css_transition_resolve(rect, &layout);
    ASSERT_EQ(scheduler->count, 1);
    auto* retargeted = static_cast<CssTransitionState*>(scheduler->first->state);
    EXPECT_EQ(retargeted->to.value.color.g, 128);
    EXPECT_EQ(retargeted->to.value.color.b, 0);
}

TEST_F(SvgAnimationLifetimeTest, ComputedPaintOwnsOutputAndRejectsShortDestination) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* group = element("g", svg); ASSERT_NE(group, nullptr);
    DomElement* child = element("rect", group); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(group->set_attribute("fill", "currentColor"));
    ASSERT_TRUE(group->set_attribute("color", "red"));
    ASSERT_TRUE(group->set_attribute("stroke-width", "3em"));
    ASSERT_TRUE(group->set_attribute("font-size", "20"));
    ASSERT_TRUE(child->set_attribute("color", "blue"));
    ASSERT_TRUE(child->set_attribute("font-size", "10"));
    Pool* output = mem_pool_create(nullptr, MEM_ROLE_CSS, "test.svg.computed_output");
    ASSERT_NE(output, nullptr);
    String* color = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_FILL, 0);
    ASSERT_NE(color, nullptr); EXPECT_STREQ(color->chars, "rgb(0, 0, 255)");
    String* width = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_STROKE_WIDTH, 0);
    ASSERT_NE(width, nullptr); EXPECT_STREQ(width->chars, "60px");
    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(child, "fill", "purple", &changed));
    ASSERT_TRUE(changed);
    String* presented = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_FILL, 0);
    ASSERT_NE(presented, nullptr); EXPECT_STREQ(presented->chars, "rgb(128, 0, 128)");
    ASSERT_TRUE(dom_element_clear_presentation_style(child));
    char small[4] = "x";
    EXPECT_FALSE(css_prop_serialize_computed(child, CSS_PROPERTY_FILL, 0, small, sizeof(small)));
    EXPECT_STREQ(small, "");
    ASSERT_TRUE(child->set_attribute("style", "fill:red;fill:var(--bad);--bad:url(#p) potato"));
    String* invalid = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_FILL, 0);
    ASSERT_NE(invalid, nullptr); EXPECT_STREQ(invalid->chars, "rgb(0, 0, 255)");
    // output belongs to the caller's pool after style mutation and element removal.
    ASSERT_TRUE(group->remove_child(child));
    EXPECT_STREQ(color->chars, "rgb(0, 0, 255)"); EXPECT_STREQ(width->chars, "60px");
    mem_pool_destroy(output);
}

TEST_F(SvgAnimationLifetimeTest, SvgPresentationListsOwnUnboundedComputedOutput) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* group = element("g", svg); ASSERT_NE(group, nullptr);
    DomElement* child = element("rect", group); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(group->set_attribute("font-size", "20"));
    ASSERT_TRUE(group->set_attribute("stroke-dasharray", "3 2em 10%"));
    ASSERT_TRUE(child->set_attribute("font-size", "10"));
    Pool* output = mem_pool_create(nullptr, MEM_ROLE_CSS, "test.svg.presentation_output");
    ASSERT_NE(output, nullptr);
    String* inherited = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_STROKE_DASHARRAY, 0);
    ASSERT_NE(inherited, nullptr); EXPECT_STREQ(inherited->chars, "3px, 40px, 10%");
    StrBuf* list = strbuf_new(); ASSERT_NE(list, nullptr);
    for (int index = 0; index < 250; index++) strbuf_append_str(list, index ? " 1" : "1");
    ASSERT_TRUE(child->set_attribute("stroke-dasharray", list->str));
    strbuf_free(list);
    String* full = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_STROKE_DASHARRAY, 0);
    ASSERT_NE(full, nullptr); EXPECT_EQ(full->len, 1248u);
    char small[512] = {};
    EXPECT_FALSE(css_prop_serialize_computed(child, CSS_PROPERTY_STROKE_DASHARRAY, 0, small, sizeof(small)));
    EXPECT_STREQ(small, "");
    ASSERT_TRUE(group->remove_child(child));
    EXPECT_STREQ(inherited->chars, "3px, 40px, 10%"); EXPECT_EQ(full->len, 1248u);
    mem_pool_destroy(output);
}

TEST_F(SvgAnimationLifetimeTest, SvgMarkerProjectionOwnsOutputAcrossMutationAndRemoval) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* group = element("g", svg); ASSERT_NE(group, nullptr);
    DomElement* child = element("path", group); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(group->set_attribute("style", "marker:url(#inherited);text-anchor:END"));
    ASSERT_TRUE(child->set_attribute("marker", "url(#ignored)"));
    ASSERT_TRUE(child->set_attribute("style", "marker:url(#losing);marker:var(--bad);--bad:url(#m) red"));
    Pool* output = mem_pool_create(nullptr, MEM_ROLE_CSS, "test.svg.marker_output");
    ASSERT_NE(output, nullptr);
    String* inherited = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_MARKER, 0);
    ASSERT_NE(inherited, nullptr); EXPECT_STREQ(inherited->chars, "url(\"#inherited\")");
    ASSERT_TRUE(child->set_attribute("style", "marker:url(#m);marker-end:url(#n)"));
    String* mixed = css_prop_serialize_computed_value(output, child, CSS_PROPERTY_MARKER, 0);
    ASSERT_NE(mixed, nullptr); EXPECT_STREQ(mixed->chars, "");
    char small[4] = "x";
    EXPECT_FALSE(css_prop_serialize_computed(child, CSS_PROPERTY_MARKER_START, 0, small, sizeof(small)));
    EXPECT_STREQ(small, "");
    ASSERT_TRUE(group->remove_child(child));
    EXPECT_STREQ(inherited->chars, "url(\"#inherited\")"); EXPECT_STREQ(mixed->chars, "");
    mem_pool_destroy(output);
}

TEST(SvgCascadeTest, SharedCssUrlResolutionKeepsLocalAndEmptyReferences) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    Url* base = url_parse("https://example.test/styles/main.css"); ASSERT_NE(base, nullptr);
    EXPECT_STREQ(radiant_resolve_css_url(pool, "#paint", base), "#paint");
    EXPECT_STREQ(radiant_resolve_css_url(pool, "", base), "");
    EXPECT_STREQ(radiant_resolve_css_url(pool, "assets/paint.svg#g", base),
        "https://example.test/styles/assets/paint.svg#g");
    EXPECT_STREQ(radiant_resolve_css_url(pool, "https://cdn.test/paint.svg#g", base),
        "https://cdn.test/paint.svg#g");
    url_destroy(base); pool_destroy(pool);
}

TEST_F(SvgAnimationLifetimeTest, EmptyNonPaintSubstitutionDoesNotReadPaintListHead) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* text = element("text", svg); ASSERT_NE(text, nullptr);
    ASSERT_TRUE(text->set_attribute("style", "--empty: ;font-size:var(--empty)"));
    char value[32] = {};
    // empty custom tokens are valid input to substitution, even when the consuming property defaults.
    const char* result = svg_get_dom_presentation_property(text, "font-size", true, value, sizeof(value));
    EXPECT_TRUE(!result || !*result);
}

TEST_F(SvgAnimationLifetimeTest, TextHitGeometryRefreshesInheritedStyleAndPosition) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    struct FontOwner {
        FontContext* fonts = font_context_create(nullptr);
        ~FontOwner() {font_context_destroy(fonts);}
    } fonts;
    ASSERT_NE(fonts.fonts, nullptr);
    ui.font_ctx = lam::up(fonts.fonts);
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    doc.input = lam::up(owner);
    MarkBuilder builder(owner);
    Item source = builder.element("svg").attr("xmlns", "http://www.w3.org/2000/svg")
        .attr("width", "200").attr("height", "200")
        .child(builder.element("g").attr("style", "font:20px Arial")
            .child(builder.element("text").attr("x", "10").attr("y", "60").text("W").final()).final()).final();
    svg = build_dom_tree_from_element(source.element, &doc, nullptr); ASSERT_NE(svg, nullptr);
    doc.root = lam::up(svg);
    DomElement* group = svg->first_child->as_element(); ASSERT_NE(group, nullptr);
    DomElement* text = group->first_child->as_element(); ASSERT_NE(text, nullptr);
    SvgLengthContext lengths = {200.0f, 200.0f, 20.0f, 10.0f};
    auto bounds = [&]() {
        Bound result = {};
        RdtPath* path = svg_text_geometry_path(text, &lengths, fonts.fonts);
        EXPECT_NE(path, nullptr);
        EXPECT_TRUE(rdt_path_get_bounds(path, &result.left, &result.top, &result.right, &result.bottom));
        rdt_path_free(path);
        return result;
    };
    Bound initial = bounds();
    EXPECT_GT(initial.right - initial.left, 0.0f);
    RdtPath* retained = svg_text_geometry_path(text, &lengths, fonts.fonts);
    ASSERT_NE(retained, nullptr);
    for (size_t i = 0; i < 3; i++) {
        Bound repeated = bounds();
        EXPECT_FLOAT_EQ(repeated.left, initial.left);
        EXPECT_FLOAT_EQ(repeated.right, initial.right);
    }
    ASSERT_TRUE(group->set_attribute("style", "font:40px Arial"));
    ASSERT_TRUE(text->set_attribute("x", "30"));
    Bound changed = bounds();
    EXPECT_NEAR(changed.left - initial.left, 20.0f, .01f);
    EXPECT_NEAR(changed.right - changed.left, 2.0f * (initial.right - initial.left), .1f);
    Bound original = {};
    ASSERT_TRUE(rdt_path_get_bounds(retained, &original.left, &original.top, &original.right, &original.bottom));
    EXPECT_FLOAT_EQ(original.left, initial.left);
    EXPECT_FLOAT_EQ(original.right, initial.right);
    rdt_path_free(retained);
    ui.font_ctx = nullptr;
}

TEST_F(SvgAnimationLifetimeTest, TextHitGeometrySkipsLayoutOnlyAncestors) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    struct FontOwner {
        FontContext* fonts = font_context_create(nullptr);
        ~FontOwner() {font_context_destroy(fonts);}
    } fonts;
    ASSERT_NE(fonts.fonts, nullptr);
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    doc.input = lam::up(owner);
    MarkBuilder builder(owner);
    Item source = builder.element("svg").attr("xmlns", "http://www.w3.org/2000/svg")
        .attr("width", "200").attr("height", "200")
        .child(builder.element("text").attr("x", "10").attr("y", "60").text("W").final()).final();
    svg = build_dom_tree_from_element(source.element, &doc, nullptr); ASSERT_NE(svg, nullptr);
    doc.root = lam::up(svg);
    ui.font_ctx = lam::up(fonts.fonts);
    root = DomElement::create(&doc, "::anon-block", nullptr); ASSERT_NE(root, nullptr);
    ASSERT_TRUE(root->is_synthetic());
    ASSERT_EQ(root->elmt.type, nullptr);
    DomElement* text = svg->first_child->as_element(); ASSERT_NE(text, nullptr);
    SvgLengthContext lengths = {200.0f, 200.0f, 20.0f, 10.0f};
    RdtPath* path = svg_text_geometry_path(text, &lengths, fonts.fonts);
    ASSERT_NE(path, nullptr);
    rdt_path_free(path);
    // layout wrappers do not invalidate the authored SVG style snapshot.
    root->first_child = lam::own(static_cast<DomNode*>(svg));
    root->last_child = lam::up(static_cast<DomNode*>(svg));
    svg->parent = lam::up(static_cast<DomNode*>(root));
    doc.root = lam::up(root);
    lengths.viewport_width = 201.0f;
    path = svg_text_geometry_path(text, &lengths, fonts.fonts);
    ASSERT_NE(path, nullptr);
    Bound bounds = {};
    EXPECT_TRUE(rdt_path_get_bounds(path, &bounds.left, &bounds.top, &bounds.right, &bounds.bottom));
    EXPECT_GT(bounds.right - bounds.left, 0.0f);
    rdt_path_free(path);
    ui.font_ctx = nullptr;
}

TEST_F(SvgAnimationLifetimeTest, SelectorStateDependencyIncludesFailedHoverQueries) {
    struct MetadataOwner {~MetadataOwner() {css_property_system_cleanup();}} metadata;
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    CssEngine* engine = css_engine_create(doc.document_pool); ASSERT_NE(engine, nullptr);
    SelectorMatcher* matcher = selector_matcher_create(doc.document_pool); ASSERT_NE(matcher, nullptr);
    ASSERT_TRUE(svg->set_attribute("id", "scene"));
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    CssStylesheet* sheets[] = {css_parse_stylesheet(engine,
        "#scene { color:red; & rect { fill:blue; } } rect { font:italic 20px Arial; }", nullptr)};
    ASSERT_NE(sheets[0], nullptr);
    CssDeclaration selected = {};
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, rect, sheets, 1,
        nullptr, 0, "fill", &selected));
    EXPECT_STREQ(selected.value_text, "blue");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, rect, sheets, 1,
        nullptr, 0, "font-size", &selected));
    EXPECT_STREQ(selected.property_name, "font");
    EXPECT_FALSE(matcher->depends_on_state);
    CssSelectorGroup* hover = css_parse_selector_group_text("rect:hover", 10, doc.document_pool);
    ASSERT_NE(hover, nullptr);
    EXPECT_FALSE(selector_matcher_matches_group(matcher, hover, rect, nullptr));
    EXPECT_TRUE(matcher->depends_on_state);
    selector_matcher_destroy(matcher);
    css_engine_destroy(engine);
}

TEST(FontContextTest, PlatformFallbackHandlesStayWithTheirOwningContext) {
    FontContextConfig config = {};
    config.pixel_ratio = 1.0f;
    FontContext* first = font_context_create(&config);
    FontContext* second = font_context_create(&config);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    FontStyleDesc style = {};
    style.family = "sans-serif";
    style.size_px = 16.0f;
    style.weight = FONT_WEIGHT_NORMAL;
    FontHandle* a = font_find_codepoint_fallback(first, &style, 0x2211, nullptr);
    FontHandle* b = font_find_codepoint_fallback(second, &style, 0x2211, nullptr);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->ctx, first);
    EXPECT_EQ(b->ctx, second);
    EXPECT_NE(a, b);
    font_handle_release(a);
    font_handle_release(b);
    // retiring either pool must leave the other's cached fallback usable.
    font_context_destroy(first);
    FontHandle* retained = font_find_codepoint_fallback(second, &style, 0x2211, nullptr);
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->ctx, second);
    EXPECT_GT(font_get_metrics(retained)->line_height, 0.0f);
    font_handle_release(retained);
    font_context_destroy(second);
}

TEST(SvgCascadeTest, FontDescriptorChangesAdvancePaintResourceGeneration) {
    FontContext* context = font_context_create(nullptr);
    ASSERT_NE(context, nullptr);
    uint64_t before = font_context_resource_generation(context);
    FontFaceDesc face = {};
    face.family = "SVG resource generation fixture";
    face.weight = FONT_WEIGHT_NORMAL;
    face.slant = FONT_SLANT_NORMAL;
    ASSERT_TRUE(font_face_register(context, &face));
    uint64_t registered = font_context_resource_generation(context);
    EXPECT_GT(registered, before);
    font_face_clear(context);
    EXPECT_GT(font_context_resource_generation(context), registered);
    font_context_destroy(context);
}

TEST(SvgAnimationTest, ClockValuesUseSmilUnitsAndClockFields) {
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("150ms", -1.0), 0.15);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1.5min", -1.0), 90.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("01:02:03.5", -1.0), 3723.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("02:03.5", -1.0), 123.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-2s", -1.0), -2.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("00:99:00", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("2bogus", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("  150ms  ", -1.0), 0.15);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-01:02.5", -1.0), -62.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-01:02:03.5", -1.0), -3723.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1:02", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("01:2", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1.5:02", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1 s", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1e2s", -1.0), -1.0);
}

TEST(SvgLengthTest, UsesViewportAxesFontMetricsAndRejectsUnknownUnits) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    EXPECT_FLOAT_EQ(svg_resolve_length("25%", &lengths, SVG_LENGTH_X, -1.0f), 50.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("25%", &lengths, SVG_LENGTH_Y, -1.0f), 25.0f);
    EXPECT_NEAR(svg_resolve_length("10%", &lengths, SVG_LENGTH_DIAGONAL, -1.0f), 15.811388f, 0.0001f);
    EXPECT_FLOAT_EQ(svg_resolve_length("2em", &lengths, SVG_LENGTH_X, -1.0f), 60.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("2ex", &lengths, SVG_LENGTH_X, -1.0f), 34.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("1in", &lengths, SVG_LENGTH_X, -1.0f), 96.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("12bogus", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("12%bad", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
}

TEST(SvgLengthTest, ResolvesFontMetricsOnlyWhenExUnitsAreConsumed) {
    FontContextConfig config = {}; config.pixel_ratio = 1.0f;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    FontStyleDesc descriptor = {};
    descriptor.family = "Arial"; descriptor.size_px = 30.0f;
    descriptor.weight = FONT_WEIGHT_NORMAL;
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 15.0f, fonts, descriptor};
    EXPECT_FLOAT_EQ(svg_resolve_length("2em", &lengths, SVG_LENGTH_X, -1.0f), 60.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 10px)", &lengths, SVG_LENGTH_X, -1.0f), 110.0f);
    EXPECT_EQ(font_get_cache_stats(fonts).face_count, 0);
    EXPECT_EQ(font_get_cache_stats(fonts).database_font_count, 0);
    float measured = svg_resolve_length("2ex", &lengths, SVG_LENGTH_X, -1.0f);
    EXPECT_FLOAT_EQ(measured, 2.0f * svg_font_x_height(fonts, &descriptor));
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 2ex)", &lengths, SVG_LENGTH_X, -1.0f), 100.0f + measured);
    EXPECT_GT(font_get_cache_stats(fonts).face_count, 0);
    font_context_destroy(fonts);
}

TEST(SvgLengthTest, AnglesShareCssUnitConversionAndRejectInvalidSuffixes) {
    EXPECT_NEAR(svg_resolve_angle("0.25turn", -1), 90, 0.0001f);
    EXPECT_NEAR(svg_resolve_angle("100grad", -1), 90, 0.0001f);
    EXPECT_NEAR(svg_resolve_angle("1.5707963268rad", -1), 90, 0.0001f);
    EXPECT_FLOAT_EQ(svg_resolve_angle(" 90deg ", -1), 90);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90", -1), 90);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90px", -1), -1);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90deg suffix", -1), -1);
}

TEST(SvgLengthTest, CssMathUsesSvgViewportAndMeasuredFontBases) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(2 * 3)", &lengths, SVG_LENGTH_DIAGONAL, -1.0f), 6.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(2px - 4px)", &lengths, SVG_LENGTH_X, 99.0f), -2.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 2ex)", &lengths, SVG_LENGTH_X, -1.0f), 134.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 2ex)", &lengths, SVG_LENGTH_Y, -1.0f), 84.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("min(2em, 50%)", &lengths, SVG_LENGTH_Y, -1.0f), 50.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("clamp(1in, calc(50% + 2em), 200px)", &lengths, SVG_LENGTH_X, -1.0f), 160.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc((25% + 2ex) * 2)", &lengths, SVG_LENGTH_X, -1.0f), 168.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(2bogus + 10px)", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
}

TEST(SvgLengthTest, DashZerosOddListsAndInvalidLists) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    float dashes[32];
    ASSERT_EQ(svg_resolve_dash_array("0 10% 2em", &lengths, dashes, 32), 6);
    EXPECT_FLOAT_EQ(dashes[0], 0.0f);
    EXPECT_NEAR(dashes[1], 15.811388f, 0.0001f);
    EXPECT_FLOAT_EQ(dashes[2], 60.0f);
    EXPECT_FLOAT_EQ(dashes[3], 0.0f);
    EXPECT_EQ(svg_resolve_dash_array("0 0", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10 -1", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10,", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10bogus 1", &lengths, dashes, 32), 0);
    const char* long_list = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21";
    ASSERT_EQ(svg_resolve_dash_array(long_list, &lengths, nullptr, 0), 42);
    float long_dashes[42];
    ASSERT_EQ(svg_resolve_dash_array(long_list, &lengths, long_dashes, 42), 42);
    EXPECT_FLOAT_EQ(long_dashes[20], 21.0f); EXPECT_FLOAT_EQ(long_dashes[41], 21.0f);
}

TEST(SvgLengthTest, DashMathComputesWithoutTruncatingFunctions) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    float dashes[6];
    ASSERT_EQ(svg_resolve_dash_array("calc(2 * 3), min(2em, 50%), calc(2px - 4px)", &lengths, dashes, 6), 6);
    EXPECT_FLOAT_EQ(dashes[0], 6.0f);
    EXPECT_FLOAT_EQ(dashes[1], 60.0f);
    EXPECT_FLOAT_EQ(dashes[2], 0.0f);
    EXPECT_FLOAT_EQ(dashes[3], 6.0f);
    EXPECT_EQ(svg_resolve_dash_array("calc(2px + 3), 4", &lengths, dashes, 6), 0);
    const char* long_function = "calc(1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px + 1px) 2";
    ASSERT_EQ(svg_resolve_dash_array(long_function, &lengths, dashes, 6), 2);
    EXPECT_FLOAT_EQ(dashes[0], 20.0f);
    EXPECT_FLOAT_EQ(dashes[1], 2.0f);
}

struct SvgPathTrace {
    int count;
    RdtPathCommand commands[16];
    float args[16][6];
};

static bool trace_svg_path(void* context, RdtPathCommand command,
                            const float* args, int arg_count) {
    SvgPathTrace* trace = (SvgPathTrace*)context;
    if (trace->count >= 16 || arg_count > 6) return false;
    trace->commands[trace->count] = command;
    for (int i = 0; i < arg_count; i++) trace->args[trace->count][i] = args[i];
    trace->count++;
    return true;
}

TEST(SvgPathTest, PreservesCompleteSegmentsBeforeMalformedParameters) {
    const char* cases[] = {
        "M10 10L20 20 ?", "M10 10L20 20X30 30", "M10 10L20 20 30",
        "M10 10L20 20C40 40", "M10 10L20 20L", "M10 10L20 20H+",
        "M10 10L20 20A10 10 0 2 0 30 30",
        "M10 10L20 20A10 10 0 0 3 30 30",
        "M10 10L20 20L1e999 40", "M10 10L20 20L30,,40",
        "M10 10L20 20,,L30 30"
    };
    for (const char* d : cases) {
        SCOPED_TRACE(d);
        RdtPath* path = svg_parse_path_d(d);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 2);
        EXPECT_EQ(trace.commands[0], RDT_PATH_MOVE);
        EXPECT_EQ(trace.commands[1], RDT_PATH_LINE);
        EXPECT_FLOAT_EQ(trace.args[1][0], 20.0f);
        EXPECT_FLOAT_EQ(trace.args[1][1], 20.0f);
    }
}

TEST(SvgPathTest, RequiresInitialMovetoAndTerminatesAfterClosepath) {
    const char* invalid[] = {"L10 10L20 20", "M10", "M,10 10", "M0x10 20L30 30"};
    for (const char* d : invalid) {
        SCOPED_TRACE(d);
        RdtPath* path = svg_parse_path_d(d);
        EXPECT_EQ(path, nullptr);
        if (path) rdt_path_free(path);
    }
    RdtPath* path = svg_parse_path_d("M10 10L20 20Z30 30");
    ASSERT_NE(path, nullptr);
    SvgPathTrace trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    ASSERT_EQ(trace.count, 3);
    EXPECT_EQ(trace.commands[2], RDT_PATH_CLOSE);
}

TEST(SvgPathTest, ParsesCompactDecimalExponentAndArcFlagParameters) {
    RdtPath* path = svg_parse_path_d("M.5.5L1e1-2e1");
    ASSERT_NE(path, nullptr);
    SvgPathTrace trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    ASSERT_EQ(trace.count, 2);
    EXPECT_FLOAT_EQ(trace.args[0][0], 0.5f);
    EXPECT_FLOAT_EQ(trace.args[0][1], 0.5f);
    EXPECT_FLOAT_EQ(trace.args[1][0], 10.0f);
    EXPECT_FLOAT_EQ(trace.args[1][1], -20.0f);

    path = svg_parse_path_d("M10 10A10 10 0 0130 30");
    ASSERT_NE(path, nullptr);
    trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    EXPECT_GT(trace.count, 1);
}

TEST(SvgPathTest, SmoothCurvesReflectOnlyTheMatchingControlPointKind) {
    const char* cases[] = {
        "M0 0Q10 10 20 0S30 -10 40 0",
        "M0 0C0 10 10 10 10 0T20 0"
    };
    const float expected_x[] = {20.0f, 10.0f};
    for (int i = 0; i < 2; i++) {
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 3);
        EXPECT_EQ(trace.commands[2], RDT_PATH_CUBIC);
        EXPECT_FLOAT_EQ(trace.args[2][0], expected_x[i]);
        EXPECT_FLOAT_EQ(trace.args[2][1], 0.0f);
    }
}

TEST(SvgPathTest, EitherZeroArcRadiusEmitsALine) {
    const char* cases[] = {"M10 10A0 10 0 0 1 30 30", "M10 10A10 0 0 0 1 30 30"};
    for (const char* d : cases) {
        RdtPath* path = svg_parse_path_d(d);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 2);
        EXPECT_EQ(trace.commands[1], RDT_PATH_LINE);
        EXPECT_FLOAT_EQ(trace.args[1][0], 30.0f);
        EXPECT_FLOAT_EQ(trace.args[1][1], 30.0f);
    }
}

TEST(RdtPathBoundsTest, UsesCurveExtremaAndRestoresClosedSubpathOrigin) {
    const char* cases[] = {
        "M10 170C10 90 110 90 110 170Z",
        "M0 0C100 100 100 -100 0 0",
        "M10 10L20 20ZC10 30 30 30 30 10",
        "M1000 1000M10 20L30 40"
    };
    const float expected[][4] = {
        {10.0f, 110.0f, 110.0f, 170.0f},
        {0.0f, -28.867514f, 75.0f, 28.867514f},
        {10.0f, 10.0f, 30.0f, 25.0f},
        {10.0f, 20.0f, 30.0f, 40.0f}
    };
    for (int i = 0; i < 4; i++) {
        SCOPED_TRACE(cases[i]);
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        for (int j = 0; j < 4; j++) EXPECT_NEAR(bounds[j], expected[i][j], 0.0001f);
    }
}

TEST(RdtPathBoundsTest, HandlesLinearAndDegenerateCubicDerivatives) {
    const char* cases[] = {"M0 0C0 10 0 10 0 0", "M0 0C10 10 20 20 30 30"};
    const float expected[][4] = {{0.0f, 0.0f, 0.0f, 7.5f}, {0.0f, 0.0f, 30.0f, 30.0f}};
    for (int i = 0; i < 2; i++) {
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        for (int j = 0; j < 4; j++) EXPECT_NEAR(bounds[j], expected[i][j], 0.0001f);
    }
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_move_to(path, 10.0f, 20.0f);
    float bounds[4] = {};
    EXPECT_FALSE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
    rdt_path_free(path);
}

TEST(RdtPathBoundsTest, ContinuesFromAppendedClosedPrimitives) {
    for (int i = 0; i < 2; i++) {
        RdtPath* path = rdt_path_new();
        ASSERT_NE(path, nullptr);
        if (i == 0) rdt_path_add_rect(path, 10.0f, 10.0f, 20.0f, 20.0f, 0.0f, 0.0f);
        else rdt_path_add_circle(path, 20.0f, 20.0f, 10.0f, 10.0f);
        rdt_path_cubic_to(path, 30.0f, 50.0f, 70.0f, 50.0f, 70.0f, 10.0f);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        EXPECT_FLOAT_EQ(bounds[0], 10.0f); EXPECT_FLOAT_EQ(bounds[1], 10.0f);
        EXPECT_FLOAT_EQ(bounds[2], 70.0f); EXPECT_FLOAT_EQ(bounds[3], 40.0f);
    }
}

TEST(RdtVectorTest, TranslucentRectanglesKeepOpaqueAlphaWithinNestedRectClips) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (bool batched : {false, true}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(batched);
        uint32_t pixels[32 * 32] = {};
        RdtVector vector = {};
        rdt_vector_init(&vector, pixels, 32, 32, 32);
        if (batched) rdt_vector_begin_batch(&vector);
        rdt_fill_rect(&vector, 0, 0, 32, 32, Color{0xffff0000u});
        RdtPath* outer = rdt_path_new();
        RdtPath* inner = rdt_path_new();
        ASSERT_NE(outer, nullptr); ASSERT_NE(inner, nullptr);
        rdt_path_add_rect(outer, 0, 0, 32, 31.5f, 0, 0);
        rdt_path_add_rect(inner, 2, 2, 5, 5, 0, 0);
        RdtMatrix transform = rdt_matrix_scale(density, density);
        transform.e13 = transform.e23 = 4;
        rdt_push_clip(&vector, outer, nullptr);
        rdt_push_clip(&vector, inner, &transform);
        rdt_fill_rect(&vector, 0, 0, 32, 32, Color{0x59ffffffu});
        rdt_pop_clip(&vector); rdt_pop_clip(&vector);
        if (batched) rdt_vector_end_batch(&vector);
        uint32_t pixel = pixels[10 * 32 + 10];
        EXPECT_EQ(pixel >> 24, 255u);
        EXPECT_NEAR((float)(pixel & 255u), 89.0f, 1.0f);
        EXPECT_NEAR((float)((pixel >> 8) & 255u), 89.0f, 1.0f);
        EXPECT_EQ((pixel >> 16) & 255u, 255u);
        EXPECT_EQ(pixels[1 * 32 + 1], 0xffff0000u);
        EXPECT_EQ(pixels[20 * 32 + 20], 0xffff0000u);
        rdt_path_free(inner); rdt_path_free(outer);
        rdt_vector_destroy(&vector);
    }
    rdt_engine_term();
}

TEST(RdtVectorTest, OpaqueRectanglesUseExactRasterAndPreserveNativeCoverageFallback) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (bool fractional : {false, true}) for (bool negative : {false, true})
    for (float offset : {0.0f, 5.0f, 5.5f}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(fractional);
        SCOPED_TRACE(negative);
        SCOPED_TRACE(offset);
        uint32_t actual[128 * 64] = {}, expected[128 * 64] = {};
        auto draw = [&](uint32_t* pixels, bool native) {
            RdtVector vector = {}; rdt_vector_init(&vector, pixels, 128, 64, 128);
            rdt_vector_set_tile_offset_x(&vector, offset); rdt_vector_set_tile_offset_y(&vector, offset);
            rdt_vector_begin_batch(&vector);
            for (unsigned index = 0; index < 8; index++) {
                float x = ((negative ? -3 : 3) + (fractional ? .25f : 0) + (float)index * 7) * density;
                float y = (4 + (fractional ? .5f : 0)) * density;
                float w = (fractional ? 3.5f : 3) * density, h = (fractional ? 4.5f : 4) * density;
                if (native) test_radiant_fill_native_rect(&vector, x, y, w, h, Color{0xff2040e0u});
                else rdt_fill_rect(&vector, x, y, w, h, Color{0xff2040e0u});
            }
            rdt_vector_end_batch(&vector); rdt_vector_destroy(&vector);
        };
        draw(expected, true);
        RenderPipelineMetrics counters;
        { RenderPipelineMetricsScope probe(&counters); draw(actual, false); }
        if (!fractional && offset != 5.5f) {
            EXPECT_EQ(counters.native_shapes, 0u); EXPECT_EQ(counters.batch_flushes, 0u);
        } else EXPECT_GT(counters.native_shapes, 0u);
        EXPECT_EQ(counters.flush_cpu, 0u);
        test_radiant_expect_pixels_equal(actual, expected, 128 * 64);
        size_t painted = 0;
        for (uint32_t pixel : actual) painted += pixel != 0;
        EXPECT_GT(painted, 0u);
    }
    rdt_engine_term();
}

TEST(RdtVectorTest, OpaqueRectangleRasterKeepsClipAndPendingNativeBatchBoundaries) {
    enum Case { PLAIN, RECT_CLIP, CURVED_CLIP, FRACTIONAL_CLIP, PENDING, SEALED_PENDING };
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (Case kind : {PLAIN, RECT_CLIP, CURVED_CLIP, FRACTIONAL_CLIP, PENDING, SEALED_PENDING}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(kind);
        uint32_t actual[128 * 64] = {}, expected[128 * 64] = {};
        auto draw = [&](uint32_t* pixels, bool native) {
            RdtVector vector = {}; rdt_vector_init(&vector, pixels, 128, 64, 128);
            rdt_vector_begin_batch(&vector);
            if (kind == PENDING || kind == SEALED_PENDING)
                rdt_fill_rounded_rect(&vector, 0, 0, 48 * density, 28 * density, 3 * density, 3 * density, Color{0xffff0000u});
            RdtPath* clip = nullptr;
            if (kind == RECT_CLIP || kind == CURVED_CLIP || kind == FRACTIONAL_CLIP || kind == SEALED_PENDING) {
                clip = rdt_path_new(); ASSERT_NE(clip, nullptr);
                if (kind == CURVED_CLIP) rdt_path_add_circle(clip, 24 * density, 14 * density, 22 * density, 12 * density);
                else rdt_path_add_rect(clip, (kind == FRACTIONAL_CLIP ? 3.25f : 0) * density, 0,
                    48 * density, 28 * density, 0, 0);
                rdt_push_clip(&vector, clip, nullptr);
            }
            for (unsigned index = 0; index < 4; index++) {
                float x = (2 + (float)index * 8) * density;
                if (native) test_radiant_fill_native_rect(&vector, x, 10 * density, 4 * density, 4 * density, Color{0xff0000ffu});
                else rdt_fill_rect(&vector, x, 10 * density, 4 * density, 4 * density, Color{0xff0000ffu});
            }
            if (clip) { rdt_pop_clip(&vector); rdt_path_free(clip); }
            rdt_vector_end_batch(&vector); rdt_vector_destroy(&vector);
        };
        draw(expected, true);
        RenderPipelineMetrics counters;
        { RenderPipelineMetricsScope probe(&counters); draw(actual, false); }
        if (kind == PLAIN || kind == RECT_CLIP) EXPECT_EQ(counters.native_shapes, 0u);
        else EXPECT_GT(counters.native_shapes, 0u);
        EXPECT_EQ(counters.flush_cpu, 0u); // opaque admission must not split an existing native submission.
        test_radiant_expect_pixels_equal(actual, expected, 128 * 64);
        size_t painted = 0;
        for (uint32_t pixel : actual) painted += pixel != 0;
        EXPECT_GT(painted, 0u);
    }
    rdt_engine_term();
}

TEST(RdtVectorTest, RectangleRasterRejectsNonFiniteDisplacementBeforeClamping) {
    rdt_engine_init(0);
    for (float offset : {NAN, INFINITY, -INFINITY}) for (bool vertical : {false, true})
    for (Color color : {Color{0xff2040e0u}, Color{0x802040e0u}}) {
        uint32_t pixels[32 * 32] = {};
        RdtVector vector = {}; rdt_vector_init(&vector, pixels, 32, 32, 32);
        if (vertical) rdt_vector_set_tile_offset_y(&vector, offset);
        else rdt_vector_set_tile_offset_x(&vector, offset);
        rdt_vector_begin_batch(&vector);
        RenderPipelineMetrics counters;
        { RenderPipelineMetricsScope probe(&counters); rdt_fill_rect(&vector, 4, 4, 8, 8, color); }
        EXPECT_EQ(counters.native_shapes, 1u);
        size_t changed = 0;
        for (uint32_t pixel : pixels) changed += pixel != 0;
        EXPECT_EQ(changed, 0u);
        // check admission without passing non-finite placement to the native renderer at submission.
        rdt_vector_set_tile_offset_x(&vector, 0); rdt_vector_set_tile_offset_y(&vector, 0);
        rdt_vector_end_batch(&vector); rdt_vector_destroy(&vector);
        EXPECT_NE(pixels[6 * 32 + 6], 0u);
    }
    rdt_engine_term();
}

TEST(RdtVectorTest, ComposesOneClipMaskPerStableBatch) {
    rdt_engine_init(0);

    uint32_t pixels[24 * 12] = {};
    RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 24, 12, 24);

    RdtPath* outer = rdt_path_new();
    RdtPath* inner = rdt_path_new();
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    rdt_path_add_rect(outer, 2.0f, 2.0f, 16.0f, 8.0f, 0.0f, 0.0f);
    rdt_path_add_rect(inner, 8.0f, 2.0f, 8.0f, 8.0f, 0.0f, 0.0f);

    Color red = {};
    red.r = 255;
    red.a = 255;
    Color blue = {};
    blue.b = 255;
    blue.a = 255;

    rdt_vector_begin_batch(&vector);
    rdt_push_clip(&vector, outer, nullptr);
    // explicit paths keep this a native clip-mask test; exact rectangles can use the CPU fast path.
    for (int x = 0; x < 24; x += 2) {
        test_radiant_fill_native_rect(&vector, (float)x, 0.0f, 2.0f, 12.0f, red);
    }
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 0u);
    rdt_vector_flush_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 1u);
    EXPECT_NE(pixels[4 * 24 + 4], 0u);
    EXPECT_EQ(pixels[4 * 24 + 0], 0u);

    rdt_push_clip(&vector, inner, nullptr);
    test_radiant_fill_native_rect(&vector, 0.0f, 0.0f, 24.0f, 12.0f, blue);
    rdt_pop_clip(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);

    rdt_pop_clip(&vector);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);
    // clip transitions seal their masks; pixels are observable after the batch synchronizes.
    EXPECT_EQ(pixels[4 * 24 + 4], pixels[4 * 24 + 6]);
    EXPECT_NE(pixels[4 * 24 + 10], pixels[4 * 24 + 4]);
    EXPECT_EQ(pixels[4 * 24 + 20], 0u);

    rdt_path_free(inner);
    rdt_path_free(outer);
    rdt_vector_destroy(&vector);
    rdt_engine_term();
}

TEST(RdtVectorTest, NestedTransformedCurvesClipImmediateAndBatchedPaints) {
    rdt_engine_init(0);
    for (int batch = 0; batch < 2; batch++) {
        uint32_t pixels[20 * 20] = {};
        RdtVector vector = {};
        rdt_vector_init(&vector, pixels, 20, 20, 20);
        RdtPath* outer = rdt_path_new();
        RdtPath* inner = rdt_path_new();
        rdt_path_add_rect(outer, 0.0f, 0.0f, 12.0f, 20.0f, 0.0f, 0.0f);
        rdt_path_add_circle(inner, 5.0f, 5.0f, 4.0f, 4.0f);
        RdtMatrix transform = rdt_matrix_translate(5.0f, 5.0f);
        Color red = {}; red.r = 255; red.a = 255;
        if (batch) rdt_vector_begin_batch(&vector);
        rdt_push_clip(&vector, outer, nullptr);
        rdt_push_clip(&vector, inner, &transform);
        rdt_fill_rect(&vector, 0.0f, 0.0f, 20.0f, 20.0f, red);
        rdt_pop_clip(&vector);
        rdt_pop_clip(&vector);
        if (batch) rdt_vector_end_batch(&vector);
        EXPECT_NE(pixels[10 * 20 + 10], 0u);
        EXPECT_EQ(pixels[10 * 20 + 13], 0u); // inside the circle, outside the outer clip.
        EXPECT_EQ(pixels[6 * 20 + 6], 0u); // inside the outer clip, outside the circle.
        EXPECT_EQ(pixels[4 * 20 + 4], 0u); // the untranslated circle must not paint here.
        rdt_path_free(inner); rdt_path_free(outer);
        rdt_vector_destroy(&vector);
    }
    rdt_engine_term();
}

typedef struct SvgFontPathProbe {
    float left, top, right, bottom;
    bool has_point;
    int closes;
    int calls;
    bool abort;
    float area_twice, current_x, current_y, start_x, start_y;
} SvgFontPathProbe;

static bool svg_font_path_probe(void* context, FontPathCommand command,
    const float* args, int count) {
    SvgFontPathProbe* probe = (SvgFontPathProbe*)context;
    probe->calls++;
    if (probe->abort) return false;
    if (command == FONT_PATH_MOVE) {
        probe->start_x = probe->current_x = args[0]; probe->start_y = probe->current_y = args[1];
    } else if (command == FONT_PATH_LINE || command == FONT_PATH_CLOSE) {
        float x = command == FONT_PATH_CLOSE ? probe->start_x : args[0];
        float y = command == FONT_PATH_CLOSE ? probe->start_y : args[1];
        probe->area_twice += probe->current_x * y - x * probe->current_y;
        probe->current_x = x; probe->current_y = y;
    }
    if (command == FONT_PATH_CLOSE) probe->closes++;
    for (int i = 0; i < count; i += 2) {
        if (!probe->has_point) {
            probe->left = probe->right = args[i]; probe->top = probe->bottom = args[i + 1];
            probe->has_point = true;
        } else {
            probe->left = fminf(probe->left, args[i]); probe->right = fmaxf(probe->right, args[i]);
            probe->top = fminf(probe->top, args[i + 1]); probe->bottom = fmaxf(probe->bottom, args[i + 1]);
        }
    }
    return true;
}

TEST(SvgTextTest, GlyphOutlinesStayInLogicalBaselineCoordinates) {
    const float ratios[] = {1.0f, 2.0f};
    for (float ratio : ratios) {
        FontContextConfig config = {};
        config.pixel_ratio = ratio;
        FontContext* context = font_context_create(&config);
        ASSERT_NE(context, nullptr);
        FontStyleDesc style = {};
        style.family = "Ahem"; style.size_px = 20.0f; style.weight = FONT_WEIGHT_NORMAL;
        FontHandle* font = font_load_from_file(context, "test/layout/data/font/Ahem.ttf", &style);
        ASSERT_NE(font, nullptr);
        Arena* arena = arena_create_default();
        const uint32_t codepoints[] = {0x41u, 0xe9u};
        for (uint32_t codepoint : codepoints) {
            SvgFontPathProbe probe = {};
            ASSERT_TRUE(font_visit_glyph_path(font, codepoint, svg_font_path_probe, &probe, arena));
            EXPECT_TRUE(probe.has_point);
            EXPECT_GT(probe.closes, 0);
            EXPECT_NEAR(probe.left, 0.0f, 0.001f);
            EXPECT_NEAR(probe.top, -16.0f, 0.001f);
            EXPECT_NEAR(probe.right, 20.0f, 0.001f);
            EXPECT_NEAR(probe.bottom, 4.0f, 0.001f);
        }
        SvgFontPathProbe absent = {};
        EXPECT_FALSE(font_visit_glyph_path(font, 0x10ffffu, svg_font_path_probe, &absent, arena));
        EXPECT_FALSE(absent.has_point);
        SvgFontPathProbe aborted = {};
        aborted.abort = true;
        EXPECT_FALSE(font_visit_glyph_path(font, 0x41u, svg_font_path_probe, &aborted, arena));
        EXPECT_EQ(aborted.calls, 1);
        arena_destroy(arena);
        font_handle_release(font);
        font_context_destroy(context);
    }
}

TEST(SvgTextTest, BitmapCoverageContoursPreserveHolesAndRemoveInternalEdges) {
    uint8_t gray[] = {255,255,255,0, 255,0,255,0, 255,255,255,0};
    GlyphBitmap bitmap = {};
    bitmap.buffer = gray; bitmap.width = bitmap.height = 3; bitmap.pitch = 4;
    bitmap.bearing_x = 1; bitmap.bearing_y = 3; bitmap.pixel_mode = GLYPH_PIXEL_GRAY;
    Arena* arena = arena_create_default();
    SvgFontPathProbe probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(&bitmap, 0.5f, svg_font_path_probe, &probe, arena));
    EXPECT_EQ(probe.closes, 2);
    EXPECT_FLOAT_EQ(probe.left, 0.5f); EXPECT_FLOAT_EQ(probe.right, 2.0f);
    EXPECT_FLOAT_EQ(probe.top, -1.5f); EXPECT_FLOAT_EQ(probe.bottom, 0.0f);
    EXPECT_FLOAT_EQ(probe.area_twice, 4.0f);
    uint8_t diagonal[] = {0x80, 0x40};
    bitmap.buffer = diagonal; bitmap.width = bitmap.height = 2; bitmap.pitch = 1;
    bitmap.bearing_x = bitmap.bearing_y = 0; bitmap.pixel_mode = GLYPH_PIXEL_MONO;
    probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(&bitmap, 1.0f, svg_font_path_probe, &probe, arena));
    EXPECT_EQ(probe.closes, 2);
    EXPECT_FLOAT_EQ(probe.area_twice, 4.0f);
    arena_destroy(arena);
}

TEST(SvgTextTest, ColorGlyphFallsBackToCoverageGeometry) {
    FontContextConfig config = {}; config.pixel_ratio = 1.0f;
    FontContext* context = font_context_create(&config);
    ASSERT_NE(context, nullptr);
    FontStyleDesc style = {}; style.family = "sans-serif"; style.size_px = 20.0f;
    style.weight = FONT_WEIGHT_NORMAL;
    FontHandle* emoji = font_resolve_for_emoji(context, &style, 0x1f600u);
    if (!emoji) { font_context_destroy(context); GTEST_SKIP() << "no emoji font installed"; }
    Arena* arena = arena_create_default();
    SvgFontPathProbe probe = {};
    bool outlined = font_visit_glyph_path(emoji, 0x1f600u, svg_font_path_probe, &probe, arena);
    EXPECT_FALSE(outlined) << "outline calls=" << probe.calls << " bounds=" << probe.left << ',' << probe.top << ',' << probe.right << ',' << probe.bottom;
    const GlyphBitmap* bitmap = font_render_glyph(emoji, 0x1f600u, GLYPH_RENDER_NORMAL);
    ASSERT_NE(bitmap, nullptr); ASSERT_EQ(bitmap->pixel_mode, GLYPH_PIXEL_BGRA);
#ifdef __APPLE__
    GlyphInfo platform = {};
    ASSERT_TRUE(font_rasterize_ct_metrics(emoji->ct_font_ref, 0x1f600u, emoji->bitmap_scale, &platform));
    // emoji fallback must retain the selected CoreText baseline, including its descender adjustment.
    EXPECT_NEAR((float)bitmap->bearing_y, platform.bearing_y, 2.0f);
#endif
    probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(bitmap, 1.0f, svg_font_path_probe, &probe, arena));
    EXPECT_TRUE(probe.has_point); EXPECT_GT(probe.right - probe.left, 0.0f);
    RdtPath* path = rdt_path_new(); bool color_bitmap = false;
    EXPECT_TRUE(render_path_append_font_glyph(path, emoji, 0x1f600u, 10.0f, 40.0f, 1.0f, arena, &color_bitmap));
    EXPECT_TRUE(color_bitmap);
    float left, top, right, bottom;
    EXPECT_TRUE(rdt_path_get_bounds(path, &left, &top, &right, &bottom));
    EXPECT_GT(right - left, 0.0f); EXPECT_GT(bottom - top, 0.0f);
    rdt_path_free(path); arena_destroy(arena); font_handle_release(emoji); font_context_destroy(context);
}

class FontLocalSourceTest : public ::testing::Test {
protected:
    FontContext* ctx = nullptr;

    void SetUp() override {
        FontContextConfig config = {};
        config.pixel_ratio = 1.0f;
        ctx = font_context_create(&config);
        ASSERT_NE(ctx, nullptr);
        arraylist_remove_range(ctx->database->scan_directories, 0,
            ctx->database->scan_directories->length);
        font_context_add_scan_directory(ctx, "test/ui/svg_font_assets");
    }

    void TearDown() override {
        if (ctx) font_context_destroy(ctx);
    }

    FontHandle* load(FontFaceSource* sources, int count, bool has_pending_sources = false) {
        FontFaceDesc descriptor = {};
        descriptor.family = "Local Source Alias";
        descriptor.weight = FONT_WEIGHT_NORMAL;
        descriptor.slant = FONT_SLANT_NORMAL;
        descriptor.sources = sources;
        descriptor.source_count = count;
        descriptor.has_pending_sources = has_pending_sources;
        if (!font_face_register(ctx, &descriptor)) return nullptr;
        FontStyleDesc style = {};
        style.family = descriptor.family;
        style.size_px = 20.0f;
        style.weight = descriptor.weight;
        const FontFaceDesc* registered = font_face_find(ctx, &style);
        return registered ? font_face_load(ctx, registered, style.size_px) : nullptr;
    }

    void expect_name(FontHandle* handle, const char* name) {
        ASSERT_NE(handle, nullptr);
        NameTable* names = font_tables_get_name(handle->tables);
        ASSERT_NE(names, nullptr);
        EXPECT_STREQ(names->postscript_name, name);
        font_handle_release(handle);
        font_context_reset_document_fonts(ctx);
        EXPECT_EQ(ctx->face_descriptor_count, 0);
    }
};

TEST_F(FontLocalSourceTest, UniqueNamesTakePrecedenceOverLaterUrls) {
    const char* names[] = {"SVG Test Rectangle Regular", "SVGTestRectangle-Regular",
        "svgtestrectangle-regular"};
    for (const char* name : names) {
        FontFaceSource sources[2] = {};
        sources[0].local_name = name;
        sources[1].path = "test/lambda/math/fonts/NotoSansMath-Regular.ttf";
        expect_name(load(sources, 2), "SVGTestRectangle-Regular");
    }
}

TEST_F(FontLocalSourceTest, FamilyNamesFallThroughToTheNextSource) {
    FontFaceSource sources[2] = {};
    sources[0].local_name = "SVG Test Rectangle";
    sources[1].path = "test/lambda/math/fonts/NotoSansMath-Regular.ttf";
    expect_name(load(sources, 2), "NotoSansMath-Regular");
}

TEST_F(FontLocalSourceTest, PendingDownloadUsesFallbackUntilTheHostPublishesItsSource) {
    FontFaceSource source = {};
    source.local_name = "Missing Pending Download Regression Face";
    EXPECT_EQ(load(&source, 1, true), nullptr);
    const FontFaceEntry* pending = font_face_find_internal(ctx, "Local Source Alias",
        FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL);
    ASSERT_NE(pending, nullptr);
    EXPECT_EQ(pending->load_state, FONT_FACE_PENDING);
    uint64_t generation = ctx->resource_generation;
    FontStyleDesc style = {};
    style.family = "Local Source Alias";
    style.weight = FONT_WEIGHT_NORMAL;
    const FontFaceDesc* descriptor = font_face_find(ctx, &style);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->has_pending_sources);
    EXPECT_EQ(font_face_load(ctx, descriptor, 20.0f), nullptr);
    EXPECT_EQ(ctx->resource_generation, generation);

    // resource completion publishes a usable rule and invalidates fallback cache entries.
    source.local_name = nullptr;
    source.path = "test/ui/svg_font_assets/rectangle.ttf";
    FontHandle* loaded = load(&source, 1);
    EXPECT_GT(ctx->resource_generation, generation);
    expect_name(loaded, "SVGTestRectangle-Regular");
}

TEST_F(FontLocalSourceTest, CompletedDownloadStaysAfterLocalNamesInTheOriginalRule) {
    const char* url = "https://example.test/fonts/online-font.ttf";
    size_t font_length = 0;
    lam::Temp<char> font_bytes(read_binary_file(
        "test/ui/svg_font_assets/rectangle.ttf", &font_length));
    ASSERT_TRUE(font_bytes);
    const char* cache_directory = "./temp/test_font_completed_source";
    ASSERT_TRUE(create_dir(cache_directory));
    EnhancedFileCache* cache = enhanced_cache_create(cache_directory, 1024 * 1024, 10);
    ASSERT_NE(cache, nullptr);
    lam::Temp<char> cached_path(enhanced_cache_store(cache, url,
        font_bytes.get(), font_length, nullptr));
    ASSERT_TRUE(cached_path);
    DomDocument document = {};
    NetworkResourceManager* manager = resource_manager_create(&document, nullptr, cache);
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(resource_manager_prefetch(manager, url, PRIORITY_HIGH), nullptr);

    ASSERT_TRUE(css_property_system_init(ctx->pool));
    CssEngine* engine = css_engine_create(ctx->pool);
    ASSERT_NE(engine, nullptr);
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "@font-face { font-family: 'Downloaded Rule'; "
        "src: local('Missing Online Regression Face'), "
        "url('https://example.test/fonts/online-font.ttf') format('truetype'); }", nullptr);
    ASSERT_NE(sheet, nullptr);
    UiContext ui = {};
    ui.font_ctx = lam::up(ctx);
    process_font_face_rules_from_stylesheet(&ui, sheet, "https://example.test/style.css",
        false, manager);

    FontStyleDesc style = {};
    style.family = "Downloaded Rule";
    style.weight = FONT_WEIGHT_NORMAL;
    const FontFaceDesc* registered = font_face_find(ctx, &style);
    ASSERT_NE(registered, nullptr);
    ASSERT_EQ(registered->source_count, 2);
    EXPECT_FALSE(registered->has_pending_sources);
    EXPECT_STREQ(registered->sources[0].local_name, "Missing Online Regression Face");
    EXPECT_STREQ(registered->sources[1].path, cached_path.get());
    // the registry owns its source paths after the layout descriptor is released (D4.5.1v4).
    fontface_cleanup(&ui);
    expect_name(font_face_load(ctx, registered, 20.0f), "SVGTestRectangle-Regular");

    css_engine_destroy(engine);
    css_property_system_cleanup();
    resource_manager_destroy(manager);
    enhanced_cache_destroy(cache);
}

TEST_F(FontLocalSourceTest, RegistrationOwnsLocalNames) {
    char name[] = "SVGTestRectangle-Regular";
    FontFaceSource sources[2] = {};
    sources[0].local_name = "Missing Local Source Test Face";
    sources[1].local_name = name;
    FontHandle* handle = load(sources, 2);
    name[0] = 'X';
    const FontFaceDesc* descriptors[1] = {};
    ASSERT_EQ(font_face_list(ctx, "Local Source Alias", descriptors, 1), 1);
    EXPECT_STREQ(descriptors[0]->sources[1].local_name, "SVGTestRectangle-Regular");
    expect_name(handle, "SVGTestRectangle-Regular");
}

TEST_F(FontLocalSourceTest, DiskCachePreservesFullNames) {
    ASSERT_NE(font_database_find_local_name_internal(ctx->database,
        "SVG Test Rectangle Regular"), nullptr);
    const char* path = "temp/font_local_source_cache.bin";
    ASSERT_TRUE(font_database_save_cache_internal(ctx->database, path));
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    ASSERT_NE(pool, nullptr);
    ASSERT_NE(arena, nullptr);
    FontDatabase* database = font_database_create_internal(pool, arena);
    ASSERT_NE(database, nullptr);
    ASSERT_TRUE(font_database_load_cache_internal(database, path));
    FontEntry* entry = font_database_find_local_name_internal(database,
        "SVG Test Rectangle Regular");
    ASSERT_NE(entry, nullptr);
    EXPECT_STREQ(entry->postscript_name, "SVGTestRectangle-Regular");
    font_database_destroy_internal(database);
    arena_destroy(arena);
    pool_destroy(pool);
    EXPECT_EQ(remove(path), 0);
}

TEST_F(FontLocalSourceTest, CollectionInventoryCannotWinFaceMatchingOrExpandTwice) {
    const char* paths[] = {"test/ui/svg_font_assets/rectangle.ttf",
        "test/lambda/math/fonts/NotoSansMath-Regular.ttf"};
    lam::Temp<char> fonts[2];
    size_t sizes[2] = {};
    size_t offsets[2] = {20, 0};
    for (int i = 0; i < 2; i++) {
        char* bytes = nullptr;
        ASSERT_TRUE(file_read_all(paths[i], MEM_CAT_TEMP, &bytes, &sizes[i]));
        fonts[i].reset(bytes);
    }
    offsets[1] = (offsets[0] + sizes[0] + 3) & ~(size_t)3;
    size_t length = offsets[1] + sizes[1];
    lam::Temp<uint8_t> collection((uint8_t*)mem_calloc(1, length, MEM_CAT_TEMP));
    ASSERT_TRUE(collection);
    write_be32(collection.get(), FONT_TAG('t', 't', 'c', 'f'));
    write_be32(collection.get() + 4, 0x00010000);
    write_be32(collection.get() + 8, 2);
    for (int i = 0; i < 2; i++) {
        write_be32(collection.get() + 12 + i * 4, (uint32_t)offsets[i]);
        uint8_t* face = collection.get() + offsets[i];
        memcpy(face, fonts[i].get(), sizes[i]);
        uint16_t tables = read_be16(face + 4);
        for (uint16_t t = 0; t < tables; t++) {
            uint8_t* record = face + 12 + t * 16;
            write_be32(record + 8, read_be32(record + 8) + (uint32_t)offsets[i]);
        }
    }
    const char* path = "temp/font_local_source_collection.ttc";
    ASSERT_EQ(write_binary_file_atomic(path, collection.get(), length), 0);
    FontEntry* inventory = (FontEntry*)pool_calloc(ctx->pool, sizeof(FontEntry));
    ASSERT_NE(inventory, nullptr);
    inventory->family_name = arena_strdup(ctx->arena, "Noto Sans Math");
    inventory->file_path = arena_strdup(ctx->arena, path);
    inventory->format = FONT_FORMAT_TTC;
    inventory->weight = FONT_WEIGHT_NORMAL;
    inventory->is_placeholder = true;
    arraylist_append(ctx->database->all_fonts, inventory);
    ctx->database->scanned = true;
    FontDatabaseCriteria criteria = {};
    str_copy(criteria.family_name, sizeof(criteria.family_name), "Noto Sans Math", 14);
    criteria.weight = FONT_WEIGHT_NORMAL;
    for (int attempt = 0; attempt < 3; attempt++) {
        FontDatabaseResult match = font_database_find_best_match_internal(ctx->database, &criteria);
        ASSERT_NE(match.font, nullptr);
        EXPECT_TRUE(match.font->is_collection);
        EXPECT_EQ(match.font->collection_index, 1);
        EXPECT_NE(match.font, inventory);
        EXPECT_EQ(ctx->database->all_fonts->length, 3);
    }
    FontEntry* local = font_database_find_local_name_internal(ctx->database, "Noto Sans Math Regular");
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->collection_index, 1);
    EXPECT_TRUE(inventory->is_placeholder);
    EXPECT_TRUE(inventory->collection_expanded);
    EXPECT_EQ(remove(path), 0);
}

TEST(PaintIrRasterTest, SemanticMasksApplyOnceAcrossDensityTransformsAndRejectRetention) {
    rdt_engine_init(0);
    GradientStop stops[] = {{Color{0xff000000u}, 0, false, nullptr},
        {Color{0xff000000u}, .5f, false, nullptr}, {Color{0x00000000u}, .5f, false, nullptr},
        {Color{0x00000000u}, 1, false, nullptr}};
    RadialGradient gradient = {}; gradient.shape = RADIAL_SHAPE_CIRCLE; gradient.size = RADIAL_SIZE_CLOSEST_SIDE;
    gradient.cx = gradient.cy = .5f; gradient.stops = lam::own_arr(stops); gradient.stop_count = 4;
    Arena* arena = arena_create_default();
    DisplayList backdrop = {}; dl_init(&backdrop, arena);
    dl_fill_rect(&backdrop, 0, 0, 32, 32, Color{0xffff0000u});
    for (float density : {1.0f, 2.0f}) for (float opacity : {1.0f, .5f})
        for (bool blend : {false, true}) for (bool nested : {false, true}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(opacity);
        SCOPED_TRACE(blend);
        SCOPED_TRACE(nested);
        PaintEffectGroup group = {}; group.bounds = {0, 0, 8, 8}; group.opacity = opacity;
        group.blend_mode = blend ? CSS_VALUE_MULTIPLY : 0;
        group.mask = {lam::up(&gradient), {0, 0, 8, 8}, rdt_matrix_scale(1, 1), true};
        PaintList paint; paint_begin_effect_group(&paint, &group);
        RdtPath* clip = rdt_path_new(); ASSERT_NE(clip, nullptr);
        rdt_path_add_rect(clip, 0, 0, 5, 8, 0, 0);
        paint_push_clip(&paint, clip, nullptr); rdt_path_free(clip);
        PaintEffectGroup inner = {}; inner.bounds = group.bounds; inner.opacity = .5f;
        if (nested) paint_begin_effect_group(&paint, &inner);
        paint_fill_rect(&paint, 0, 0, 8, 8, Color{0xff0000ffu});
        if (nested) paint_end_effect_group(&paint);
        paint_pop_clip(&paint); paint_end_effect_group(&paint);
        RdtMatrix scale = rdt_matrix_scale(density, density); scale.e13 = scale.e23 = 4;
        Arena* foreground_arena = arena_create_default(); DisplayList foreground = {}; dl_init(&foreground, foreground_arena);
        paint_ir_lower_raster_transformed(&paint, &foreground, &scale);
        ASSERT_TRUE(dl_validate(&foreground, nullptr));
        int masked = 0;
        for (int index = 0; index < foreground.item_count(); index++) {
            const DisplayItem* item = &foreground.data()[index];
            if (item->op == DL_COMPOSITE_OPACITY && item->composite_opacity.mask.gradient) {
                masked++;
                EXPECT_FALSE(dl_item_is_retainable_for_fragment(item));
                EXPECT_FLOAT_EQ(item->composite_opacity.mask.rect.width, 8);
                EXPECT_FLOAT_EQ(item->composite_opacity.mask.inverse.e11, 1 / density);
            }
        }
        EXPECT_EQ(masked, 1);
        ImageSurface* result = render_display_list_snapshot_with_backdrop(&foreground,
            mem_context_process(MEM_ROLE_RENDER), {0, 0, 32, 32}, density, Color{0}, &backdrop);
        ASSERT_NE(result, nullptr);
        unsigned center = density == 1 ? 8 : 12;
        uint32_t pixel = ((uint32_t*)result->pixels)[center * 32 + center];
        EXPECT_EQ(pixel >> 24, 255u);
        unsigned alpha = nested ? (opacity == 1 ? 128u : 64u) : (opacity == 1 ? 255u : 128u);
        EXPECT_EQ(pixel & 255u, blend ? 0u : alpha);
        EXPECT_EQ((pixel >> 16) & 255u, 255u - alpha);
        EXPECT_EQ(((uint32_t*)result->pixels)[4 * 32 + 4], 0xffff0000u);
        // this sample is inside the opaque mask but outside the child clip.
        unsigned clipped = density == 1 ? 9 : 14;
        EXPECT_EQ(((uint32_t*)result->pixels)[center * 32 + clipped], 0xffff0000u);
        image_surface_destroy(result); dl_destroy(&foreground); arena_destroy(foreground_arena);
    }
    dl_destroy(&backdrop); arena_destroy(arena); rdt_engine_term();
}

static unsigned paint_rewind_image_releases;
static void paint_rewind_release_image(ImageSurface* image) {
    paint_rewind_image_releases++;
    image_surface_destroy(image);
}

TEST(PaintOwnership, RewindReleasesDiscardedPayloadsAndRetainsEarlierCommands) {
    paint_rewind_image_releases = 0;
    PaintList paint;
    ImageSurface* retained = image_surface_create(2, 2);
    ImageSurface* discarded = image_surface_create(2, 2);
    ASSERT_NE(retained, nullptr); ASSERT_NE(discarded, nullptr);
    paint_draw_image_resource(&paint, retained, 0, 0, 2, 2, 255, nullptr, paint_rewind_release_image);
    size_t checkpoint = paint.size(); ASSERT_EQ(checkpoint, 1u);
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 0, 0, 2, 2, 0, 0);
    paint_push_clip(&paint, path, nullptr);
    int index = paint_list_count(&paint);
    paint_fill_path(&paint, path, Color{.r=255,.g=0,.b=0,.a=255}, RDT_FILL_WINDING, nullptr);
    ASSERT_TRUE(paint_list_take_path_payload(&paint, index, PAINT_FILL_PATH));
    paint_draw_image_resource(&paint, discarded, 0, 0, 2, 2, 255, nullptr, paint_rewind_release_image);
    paint_pop_clip(&paint);
    EXPECT_FALSE(paint.rewind(paint.size() + 1)); EXPECT_EQ(paint_rewind_image_releases, 0u);
    ASSERT_TRUE(paint.rewind(checkpoint)); EXPECT_EQ(paint_rewind_image_releases, 1u);
    ASSERT_EQ(paint.size(), checkpoint); EXPECT_EQ(paint.data()[0].draw_image_resource.image.get(), retained);
    PaintIrValidationResult validation = {};
    EXPECT_TRUE(paint_ir_validate(&paint, &validation));
    paint.clear(); EXPECT_EQ(paint_rewind_image_releases, 2u);
    paint.clear(); EXPECT_EQ(paint_rewind_image_releases, 2u);
    paint_fill_rect(&paint, 0, 0, 2, 2, Color{.r=0,.g=0,.b=255,.a=255});
    EXPECT_TRUE(paint_ir_validate(&paint, &validation)); paint.destroy();
}

TEST(RdtVectorTest, CardinalGradientAxesRetainExactOwnerBoundaries) {
    Rect rect = {3.25f, 7.5f, 80.0f, 100.0f};
    const struct { float angle, x1, y1, x2, y2; } cases[] = {
        {0, 43.25f, 107.5f, 43.25f, 7.5f},
        {90, 3.25f, 57.5f, 83.25f, 57.5f},
        {180, 43.25f, 7.5f, 43.25f, 107.5f},
        {270, 83.25f, 57.5f, 3.25f, 57.5f},
        {-90, 83.25f, 57.5f, 3.25f, 57.5f},
        {450, 3.25f, 57.5f, 83.25f, 57.5f},
    };
    for (const auto& item : cases) {
        RadiantGradientLine line = radiant_linear_gradient_line(rect, item.angle);
        EXPECT_EQ(line.x1, item.x1); EXPECT_EQ(line.y1, item.y1);
        EXPECT_EQ(line.x2, item.x2); EXPECT_EQ(line.y2, item.y2);
    }
    RadiantGradientLine tilted = radiant_linear_gradient_line(rect, 89.99f);
    EXPECT_NE(tilted.y1, tilted.y2);
}

static uint64_t paint_capture_scratch_bytes(const char* prefix = "render.text.background") {
    MemSnapshot* snapshot = mem_snapshot_capture(mem_context_root());
    uint64_t bytes = 0;
    if (snapshot) for (uint32_t i = 0; i < snapshot->count; i++) {
        const MemStatSample& sample = snapshot->samples[i];
        if (strncmp(sample.label, prefix, strlen(prefix)) == 0) bytes += sample.bytes_in_use;
    }
    mem_snapshot_free(snapshot);
    return bytes;
}

TEST(PaintOwnership, GlyphBackgroundSnapshotOwnsCoverageAfterCacheAndStyleChange) {
    rdt_engine_init(0);
    DomElement owner = {}; owner.node_type = DOM_NODE_ELEMENT; owner.view_type = RDT_VIEW_BLOCK;
    owner.set_synthetic(true); owner.width = 2; owner.height = 1;
    BoundaryProp boundary = {}; BackgroundProp background = {};
    owner.bound = lam::view_prop(&boundary); boundary.background = lam::own(&background);
    background.bg_clip = CSS_VALUE_TEXT; background.color.c = 0xff0000ffu;
    UiContext ui = {};
    OffscreenRenderArenas arenas;
    ASSERT_TRUE(arenas.init("test.text.background", "test.text.background.list", "test.text.background.scratch"));
    DisplayList list = {}; dl_init(&list, arenas.list_arena);
    PaintList paint;
    RasterRenderContext context = {}; context.raster_scale = 1;
    context.block.clip = {0, 0, 2, 1};
    context.ui_context = lam::up(&ui); context.paint_list = lam::up(&paint); context.dl = lam::up(&list);
    uint8_t coverage[] = {128, 255};
    GlyphBitmap bitmap = {}; bitmap.buffer = coverage; bitmap.width = 2; bitmap.height = 1;
    bitmap.pitch = 2; bitmap.pixel_mode = GLYPH_PIXEL_GRAY;
    ASSERT_TRUE(render_paint_text_background_glyph(&context, &owner, {0, 0, 2, 1}, &bitmap, 0, 0));
    ASSERT_EQ(list.item_count(), 1);
    ASSERT_EQ(list.data()[0].op, DL_BLIT_SURFACE_SCALED);
    ImageSurface* lease = list.data()[0].blit_surface_scaled.snapshot_lease;
    ASSERT_NE(lease, nullptr);
    EXPECT_EQ(lease->snapshot_refs, 1u);
    EXPECT_EQ(paint_capture_scratch_bytes(), 0u);
    coverage[0] = coverage[1] = 0; background.color.c = 0xffff0000u;
    ASSERT_TRUE(dl_validate(&list, nullptr));
    for (int replay = 0; replay < 32; replay++) {
        ImageSurface* image = render_display_list_snapshot(&list, mem_context_process(MEM_ROLE_RENDER), {0, 0, 2, 1}, 1);
        ASSERT_NE(image, nullptr);
        const uint32_t* pixels = (const uint32_t*)image->pixels;
        EXPECT_EQ(pixels[0], 0x800000ffu); EXPECT_EQ(pixels[1], 0xff0000ffu);
        image_surface_destroy(image);
    }
    dl_destroy(&list); arenas.destroy();
    EXPECT_EQ(paint_capture_scratch_bytes(), 0u);
}

TEST(RasterPaintTest, GlyphCoverageAndImageSourceOverAgreeForEveryCoverageByte) {
    const uint32_t destinations[] = {0xffffffffu, 0xff204080u, 0xffcf7f21u};
    const uint32_t colors[] = {0xff000000u, 0xffff0000u, 0xff0000ffu, 0xff8e4a93u, 0x807c1fadu};
    for (uint32_t destination : destinations) for (uint32_t value : colors)
        for (uint32_t coverage = 0; coverage <= 255; coverage++) {
        Color color = {.c = value}, sample = color;
        sample.a = render_pixel_premultiply_channel(color.a, (uint8_t)coverage);
        uint32_t actual = destination;
        render_pixel_source_over_coverage((uint8_t*)&actual, color, coverage);
        EXPECT_EQ(actual, render_pixel_source_over_straight(destination, sample.c, 255));
    }
}

template<typename Retire>
static void expect_paint_replay_after_retirement(PaintList* paint, float density,
        Bound bounds, int replays, Retire retire) {
    OffscreenRenderArenas arenas;
    ASSERT_TRUE(arenas.init("test.paint.retirement", "test.paint.retirement.list", "test.paint.retirement.scratch"));
    DisplayList list = {}; dl_init(&list, arenas.list_arena);
    RdtMatrix scale = rdt_matrix_scale(density, density);
    paint_ir_lower_raster_transformed(paint, &list, &scale);
    ImageSurface* expected = render_display_list_snapshot(&list, mem_context_process(MEM_ROLE_RENDER), bounds, density);
    ASSERT_NE(expected, nullptr);
    size_t bytes = (size_t)expected->pitch * (size_t)expected->height;
    bool has_ink = false;
    for (size_t i = 0; i < (size_t)expected->width * (size_t)expected->height; i++)
        has_ink |= ((const uint32_t*)expected->pixels)[i] >> 24;
    ASSERT_TRUE(has_ink);
    retire();
    dl_clear(&list);
    // both deferred recording and display replay must survive source/font retirement.
    paint_ir_lower_raster_transformed(paint, &list, &scale);
    paint_list_destroy(paint);
    for (int replay = 0; replay < replays; replay++) {
        ImageSurface* actual = render_display_list_snapshot(&list, mem_context_process(MEM_ROLE_RENDER), bounds, density);
        ASSERT_NE(actual, nullptr);
        EXPECT_EQ(memcmp(actual->pixels, expected->pixels, bytes), 0);
        image_surface_destroy(actual);
    }
    image_surface_destroy(expected); dl_destroy(&list); arenas.destroy();
}

TEST(PaintOwnership, BlurredTextShadowPixelsSurviveFontAndSourceRetirement) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) {
        SCOPED_TRACE(density);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 32, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT; style.font_size = 32;
        style.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(style.font_handle, nullptr);
        FontBox box = {lam::up(&style), 32};
        UiContext ui = {}; ui.font_ctx = lam::up(fonts); ui.device_scale = density;
        char text[] = "M";
        PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up(text);
        run.text_len = 1; run.x = 16; run.baseline_y = 48;
        TextShadow shadow = {}; shadow.color.c = 0xff00ff00u; shadow.blur_radius = 4;
        PaintList paint;
        ASSERT_TRUE(render_paint_text_shadows(&paint, &ui, &run, &shadow));
        EXPECT_EQ(paint_capture_scratch_bytes("render.effect.raster"), 0u);
        EXPECT_EQ(paint_capture_scratch_bytes("render.text.shadow.bounds"), 0u);
        expect_paint_replay_after_retirement(&paint, density, {0, 0, 80 * density, 80 * density}, 32, [&]() {
            text[0] = 'W'; shadow.color.c = 0xffff0000u; shadow.blur_radius = 20;
            font_handle_release(style.font_handle); style.font_handle = nullptr;
            font_context_destroy(fonts); ui.font_ctx = nullptr;
        });
        EXPECT_EQ(paint_capture_scratch_bytes("render.effect.raster"), 0u);
        EXPECT_EQ(paint_capture_scratch_bytes("render.text.shadow.bounds"), 0u);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, EmphasisAndShadowGeometrySurviveFontSourceAndPaintListRetirement) {
    rdt_engine_init(0);
    const float densities[] = {1.0f, 2.0f};
    for (float density : densities) for (bool shadowed : {false, true}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(shadowed);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 40, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT; style.font_size = 40;
        style.font_handle = lam::counted(font_resolve(fonts, &description)); ASSERT_NE(style.font_handle, nullptr);
        style.text_emphasis_enabled = true; style.text_emphasis_mark = 0x2022;
        style.text_emphasis_under = true; style.text_emphasis_color_current = false;
        style.text_emphasis_color.c = 0xff0000ffu;
        FontBox box = {lam::up(&style), 40};
        UiContext ui = {}; ui.font_ctx = lam::up(fonts); ui.device_scale = density;
        char text[] = "A B";
        PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up(text);
        run.text_len = 3; run.x = 10; run.baseline_y = 40; run.color.c = 0;
        PaintList paint;
        TextShadow shadow = {}; shadow.color.c = 0x8000ff00u; shadow.blur_radius = 4;
        ASSERT_TRUE(shadowed ? render_paint_text_shadows(&paint, &ui, &run, &shadow)
            : render_paint_text_emphasis(&paint, fonts, &run));
        expect_paint_replay_after_retirement(&paint, density, {0, 0, 100 * density, 100 * density}, 16, [&]() {
            memset(text, 0, sizeof(text)); style.text_emphasis_mark = 0x25CB;
            font_handle_release(style.font_handle); style.font_handle = nullptr;
            font_context_destroy(fonts); ui.font_ctx = nullptr;
        });
    }
    rdt_engine_term();
}

TEST(PaintOwnership, EmphasisCapturePressurePreservesEarlierPaintAndRecovers) {
    rdt_engine_init(0);
    FontContext* fonts = font_context_create(nullptr); ASSERT_NE(fonts, nullptr);
    FontStyleDesc description = {"Arial", 40, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontProp style = FONT_PROP_DEFAULT; style.font_size = 40;
    style.font_handle = lam::counted(font_resolve(fonts, &description)); ASSERT_NE(style.font_handle, nullptr);
    style.text_emphasis_enabled = true; style.text_emphasis_mark = 0x2022;
    style.text_emphasis_under = true;
    FontBox box = {lam::up(&style), 40};
    PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up("ABC");
    run.text_len = 3; run.x = 10; run.baseline_y = 40; run.color.c = 0xff0000ffu;
    PaintList paint; paint_fill_rect(&paint, 0, 0, 4, 4, Color{0xff00ff00u});
    size_t checkpoint = paint.size();
    size_t soft, hard, critical; memtrack_get_limits(&soft, &hard, &critical);
    size_t usage = memtrack_get_current_usage(); memtrack_set_limits(usage, usage, usage + 256);
    bool recorded = render_paint_text_emphasis(&paint, fonts, &run);
    memtrack_set_limits(soft, hard, critical);
    EXPECT_FALSE(recorded); EXPECT_EQ(paint.size(), checkpoint);
    PaintIrValidationResult validation = {};
    EXPECT_TRUE(paint_ir_validate(&paint, &validation)) << validation.message;
    EXPECT_EQ(paint_capture_scratch_bytes("render.effect.raster"), 0u);
    EXPECT_EQ(paint_capture_scratch_bytes("render.emphasis.bounds"), 0u);
    ASSERT_TRUE(render_paint_text_emphasis(&paint, fonts, &run));
    expect_paint_replay_after_retirement(&paint, 1, {0, 0, 100, 100}, 16, [&]() {
        font_handle_release(style.font_handle); style.font_handle = nullptr; font_context_destroy(fonts);
    });
    rdt_engine_term();
}


TEST(RdtVectorTest, LinearGradientRangeAliasesKeepPixelCenterPrecision) {
    rdt_engine_init(0);
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 0, 0, 48, 12, 0, 0);
    RdtGradientStop authored[] = {{-.5f, 255, 0, 0, 255}, {1, 0, 0, 255, 255}};
    RdtGradientStop clipped[] = {{0, 170, 0, 85, 255}, {1, 0, 0, 255, 255}};
    uint32_t pixels[2][48 * 12] = {};
    for (size_t variant = 0; variant < 2; variant++) {
        RdtVector vector = {}; rdt_vector_init(&vector, pixels[variant], 48, 12, 48);
        rdt_fill_linear_gradient(&vector, path, 0, 0, 48, 0,
            variant ? clipped : authored, 2, RDT_FILL_WINDING, nullptr, nullptr);
        rdt_vector_destroy(&vector);
    }
    EXPECT_EQ(memcmp(pixels[0], pixels[1], sizeof(pixels[0])), 0);
    EXPECT_EQ(pixels[0][6 * 48], 0xff5700a8u);
    EXPECT_EQ(pixels[0][6 * 48 + 7], 0xff70008fu);
    rdt_path_free(path); rdt_engine_term();
}

TEST(RdtVectorTest, LinearGradientTilesKeepClipsTransformsAndBatchOrder) {
    rdt_engine_init(0);
    RdtPath* path = rdt_path_new(); RdtPath* clip = rdt_path_new();
    ASSERT_NE(path, nullptr); ASSERT_NE(clip, nullptr);
    rdt_path_add_rect(path, 5.25f, 4.5f, 28, 22, 2, 3);
    rdt_path_add_circle(clip, 24.5f, 21, 15, 12);
    RdtMatrix shape = rdt_matrix_identity(); shape.e12 = .25f; shape.e13 = 3.25f;
    RdtMatrix gradient = rdt_matrix_translate(2.5f, 1.25f);
    RdtGradientStop stops[] = {{-.5f, 255, 0, 0, 128}, {.35f, 0, 255, 0, 192},
        {.35f, 0, 0, 255, 255}, {1.5f, 255, 255, 0, 0}};
    for (RdtGradientSpread spread : {RDT_GRADIENT_PAD, RDT_GRADIENT_REPEAT, RDT_GRADIENT_REFLECT}) {
        SCOPED_TRACE(spread);
        uint32_t full[64 * 48] = {}, tile[17 * 13] = {};
        for (bool tiled : {false, true}) {
            RdtVector vector = {};
            rdt_vector_init(&vector, tiled ? tile : full, tiled ? 17 : 64, tiled ? 13 : 48, tiled ? 17 : 64);
            rdt_vector_set_tile_offset_x(&vector, tiled ? 13.25f : .25f);
            rdt_vector_set_tile_offset_y(&vector, tiled ? 11.5f : .5f);
            if (tiled) rdt_vector_begin_batch(&vector);
            rdt_fill_rect(&vector, 0, 0, 64, 48, Color{0xff808080u});
            rdt_push_clip(&vector, clip, nullptr);
            RdtGradientOptions options = {}; options.spread = spread;
            rdt_fill_linear_gradient(&vector, path, 0, 0, 20, 4, stops, 4,
                RDT_FILL_WINDING, &shape, &gradient, &options);
            rdt_pop_clip(&vector);
            rdt_fill_rect(&vector, 15, 14, 3, 2, Color{0xff00ffffu});
            if (tiled) rdt_vector_end_batch(&vector);
            rdt_vector_destroy(&vector);
        }
        for (size_t row = 0; row < 13; row++) for (size_t column = 0; column < 17; column++)
            EXPECT_EQ(tile[row * 17 + column], full[(row + 11) * 64 + column + 13]) << row << ',' << column;
        EXPECT_NE(full[20 * 64 + 24], 0xff808080u);
        EXPECT_EQ(full[2 * 64 + 2], 0xff808080u);
    }
    rdt_path_free(clip); rdt_path_free(path); rdt_engine_term();
}

TEST(RdtVectorTest, ConstantLinearGradientRetainsCurvedCoverageAndOffscreenClipping) {
    rdt_engine_init(0);
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_circle(path, 4.25f, 7.5f, 9, 6.25f);
    uint32_t expected[24 * 20] = {}, actual[24 * 20] = {};
    RdtGradientStop stops[] = {{0, 0, 0, 255, 255}, {1, 0, 0, 255, 255}};
    for (bool gradient : {false, true}) {
        RdtVector vector = {}; rdt_vector_init(&vector, gradient ? actual : expected, 24, 20, 24);
        if (gradient) rdt_fill_linear_gradient(&vector, path, 0, 0, 24, 0, stops, 2, RDT_FILL_WINDING, nullptr, nullptr);
        else rdt_fill_path(&vector, path, Color{0xffff0000u}, RDT_FILL_WINDING, nullptr);
        rdt_vector_destroy(&vector);
    }
    EXPECT_EQ(memcmp(expected, actual, sizeof(actual)), 0);
    EXPECT_EQ(actual[8 * 24 + 4], 0xffff0000u);
    EXPECT_EQ(actual[18 * 24 + 20], 0u);
    rdt_path_free(path); rdt_engine_term();
}


TEST(GradientStopSamplingTest, LargeHardStopListsRetainFollowingColorAndLengthScaling) {
    RdtGradientStop stops[1024];
    for (size_t interval = 0; interval < 512; interval++) {
        stops[interval * 2] = {(float)interval / 512, (uint8_t)(interval % 2 ? 0 : 255),
            0, (uint8_t)(interval % 2 ? 255 : 0), 255};
        stops[interval * 2 + 1] = stops[interval * 2];
        stops[interval * 2 + 1].offset = (float)(interval + 1) / 512;
    }
    for (float scale : {1.0f, 17.0f}) {
        for (size_t interval = 0; interval < 512; interval++) {
            uint32_t expected = interval % 2 ? 0xffff0000u : 0xff0000ffu;
            EXPECT_EQ(render_sample_gradient_stops(stops, 1024, ((float)interval + .5f) / 512 * scale, scale).c,
                expected) << interval;
            EXPECT_EQ(render_sample_gradient_stops(stops, 1024, (float)interval / 512 * scale, scale).c,
                expected) << interval;
        }
        EXPECT_EQ(render_sample_gradient_stops(stops, 1024, -scale, scale).c, 0xff0000ffu);
        EXPECT_EQ(render_sample_gradient_stops(stops, 1024, 2 * scale, scale).c, 0xffff0000u);
    }
}


TEST(PaintOwnership, DecorationPathsSurviveFontSourceAndPaintListRetirement) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f})
        for (CssEnum pattern : {CSS_VALUE_DASHED, CSS_VALUE_DOTTED, CSS_VALUE_DOUBLE, CSS_VALUE_WAVY}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(pattern);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 70, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT; style.font_size = 70;
        style.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(style.font_handle, nullptr);
        FontBox box = {lam::up(&style), 70};
        float bitmap_scale = font_handle_get_size_px(style.font_handle) /
            font_handle_get_physical_size_px(style.font_handle);
        LoadedGlyph* space = font_load_glyph(style.font_handle, &description, ' ', true);
        LoadedGlyph* glyph = font_load_glyph(style.font_handle, &description, 'g', true);
        ASSERT_NE(space, nullptr); ASSERT_NE(glyph, nullptr);
        style.space_width = space->advance_x * bitmap_scale;
        style.text_deco = CSS_VALUE_UNDERLINE;
        style.text_deco_extra = CSS_TEXT_DECO_LINE_THROUGH;
        style.text_deco_color.c = 0xff0000ffu;
        style.text_deco_color_current = false;
        style.text_deco_style = pattern;
        style.text_deco_thickness = 5;
        style.text_underline_offset = -10;
        style.text_deco_skip_ink = CSS_VALUE_ALL;
        unsigned char text[] = "g      g";
        TextRect rect = {}; rect.length = sizeof(text) - 1;
        rect.width = glyph->advance_x * bitmap_scale * 2 + style.space_width * 6;
        PaintList paint;
        ASSERT_TRUE(render_paint_text_decorations(&paint, &box, text, &rect, 10, 10, Color{0xff000000u}, true));
        ASSERT_TRUE(render_paint_text_decorations(&paint, &box, text, &rect, 10, 10, Color{0xff000000u}, false));
        PaintIrValidationResult valid = {}; ASSERT_TRUE(paint_ir_validate(&paint, &valid)) << valid.message;
        EXPECT_EQ(paint_capture_scratch_bytes("render.decoration.scratch"), 0u);
        ASSERT_GT(paint_list_count(&paint), 2);
        expect_paint_replay_after_retirement(&paint, density, {0, 0, 240 * density, 130 * density}, 16, [&]() {
            memset(text, 'W', sizeof(text)); style.text_deco_color.c = 0xffff0000u;
            font_handle_release(style.font_handle); style.font_handle = nullptr;
            font_context_destroy(fonts);
        });
        EXPECT_EQ(paint_capture_scratch_bytes("render.decoration.scratch"), 0u);
    }
    rdt_engine_term();
}


TEST(PaintOwnership, DecoratedShadowResourcesSurviveSourceAndFontRetirement) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (float blur : {0.0f, 4.0f})
        for (uint8_t alpha : {uint8_t(128), uint8_t(255)})
        for (CssEnum pattern : {CSS_VALUE_SOLID, CSS_VALUE_DASHED, CSS_VALUE_DOTTED, CSS_VALUE_DOUBLE, CSS_VALUE_WAVY}) {
        SCOPED_TRACE(density);
        SCOPED_TRACE(blur);
        SCOPED_TRACE(alpha);
        SCOPED_TRACE(pattern);
        FontContextConfig config = {}; config.pixel_ratio = density;
        FontContext* fonts = font_context_create(&config); ASSERT_NE(fonts, nullptr);
        FontStyleDesc description = {"Arial", 32, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
        FontProp style = FONT_PROP_DEFAULT; style.font_size = 32;
        style.font_handle = lam::counted(font_resolve(fonts, &description));
        ASSERT_NE(style.font_handle, nullptr);
        style.text_deco = CSS_VALUE_UNDERLINE;
        style.text_deco_extra = CSS_TEXT_DECO_OVERLINE | CSS_TEXT_DECO_LINE_THROUGH;
        style.text_deco_style = pattern; style.text_deco_thickness = 3;
        style.text_deco_skip_ink = CSS_VALUE_ALL;
        FontBox box = {lam::up(&style), 32};
        UiContext ui = {}; ui.font_ctx = lam::up(fonts); ui.device_scale = density;
        unsigned char text[] = "g  g";
        TextRect rect = {}; rect.length = sizeof(text) - 1; rect.width = 96; rect.height = 48;
        PaintGlyphRun run = {}; run.font = lam::up(&box); run.text = lam::up((char*)text);
        run.text_len = rect.length; run.x = 16;
        run.baseline_y = 16 + font_get_rendering_ascender(font_box_handle(&box));
        RenderTextDecorationSource decoration = {text, &rect, 16, 16};
        TextShadow shadow = {}; shadow.color.c = 0xff008000u; shadow.color.a = alpha;
        shadow.blur_radius = blur; shadow.offset_x = 4; shadow.offset_y = 6;
        PaintList paint;
        ASSERT_TRUE(render_paint_text_shadows(&paint, &ui, &run, &shadow, &decoration));
        size_t checkpoint = paint.size();
        TextShadow invisible = {}; invisible.color.a = 0;
        EXPECT_TRUE(render_paint_text_shadows(&paint, &ui, &run, &invisible, &decoration));
        EXPECT_EQ(paint.size(), checkpoint);
        TextShadow back = {}; back.color.c = 0xff008000u;
        TextShadow front = {}; front.color.c = 0xff008000u; front.blur_radius = 100;
        front.next = lam::own(&back);
        // the back layer records first; a failed front capture must restore the entire compound.
        size_t soft, hard, critical; memtrack_get_limits(&soft, &hard, &critical);
        size_t usage = memtrack_get_current_usage();
        memtrack_set_limits(usage, usage, usage + 256);
        bool recorded = render_paint_text_shadows(&paint, &ui, &run, &front, &decoration);
        memtrack_set_limits(soft, hard, critical);
        EXPECT_FALSE(recorded);
        EXPECT_EQ(paint.size(), checkpoint);
        PaintIrValidationResult validation = {};
        EXPECT_TRUE(paint_ir_validate(&paint, &validation)) << validation.message;
        EXPECT_EQ(paint_capture_scratch_bytes("render.decoration.scratch"), 0u);
        EXPECT_EQ(paint_capture_scratch_bytes("render.effect.raster"), 0u);
        expect_paint_replay_after_retirement(&paint, density, {0, 0, 160 * density, 96 * density}, 16, [&]() {
            memset(text, 'W', sizeof(text) - 1);
            style.text_deco = CSS_VALUE_NONE; style.text_deco_extra = 0;
            rect.width = 1; shadow.color.c = 0xffff0000u;
            font_handle_release(style.font_handle); style.font_handle = nullptr;
            font_context_destroy(fonts); ui.font_ctx = nullptr;
        });
        EXPECT_EQ(paint_capture_scratch_bytes("render.decoration.scratch"), 0u);
        EXPECT_EQ(paint_capture_scratch_bytes("render.effect.raster"), 0u);
        EXPECT_EQ(paint_capture_scratch_bytes("render.text.shadow.bounds"), 0u);
    }
    rdt_engine_term();
}


TEST(PaintOwnership, FailedDisplayRecordingLeavesEarlierCommandsAndRecovers) {
    DisplayList list;
    memtrack_fault_inject(0);
    dl_fill_rect(&list, 0, 0, 8, 8, Color{0xff0000ffu});
    memtrack_fault_clear();
    EXPECT_EQ(list.item_count(), 0);
    dl_fill_rect(&list, 0, 0, 8, 8, Color{0xff0000ffu});
    ASSERT_EQ(list.item_count(), 1);
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 0, 0, 4, 4, 0, 0);
    for (DisplayOp op : {DL_FILL_PATH, DL_STROKE_PATH, DL_PUSH_CLIP}) for (size_t fail_at : {size_t(0), size_t(1)}) {
        SCOPED_TRACE(op);
        memtrack_fault_inject(fail_at);
        if (op == DL_FILL_PATH) dl_fill_path(&list, path, Color{0xff0000ffu}, RDT_FILL_WINDING, nullptr);
        else if (op == DL_STROKE_PATH) dl_stroke_path(&list, path, Color{0xff0000ffu}, 2,
            RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0, nullptr);
        else dl_push_clip(&list, path, nullptr);
        memtrack_fault_clear();
        EXPECT_EQ(list.item_count(), fail_at == 0 ? 1 : 2);
        if (op == DL_PUSH_CLIP && fail_at == 1) dl_pop_clip(&list);
        EXPECT_TRUE(dl_validate(&list, nullptr));
        dl_truncate(&list, 1);
    }
    dl_fill_path(&list, path, Color{0xff0000ffu}, RDT_FILL_WINDING, nullptr);
    EXPECT_EQ(list.item_count(), 2);
    EXPECT_TRUE(dl_validate(&list, nullptr));
    rdt_path_free(path); dl_destroy(&list);
}

TEST(PaintOwnership, FailedClippedImageRecordingReleasesLeasesAndRestoresClipState) {
    rdt_engine_init(0);
    ImageSurface* image = image_surface_create(2, 2); ASSERT_NE(image, nullptr);
    image_surface_snapshot_publish(image);
    UiContext ui = {}; ui.surface = lam::up(image);
    bool saw_failure = false, saw_success = false;
    for (size_t fail_at = 0; fail_at < 12; fail_at++) {
        SCOPED_TRACE(fail_at);
        DisplayList list; PaintList paint;
        dl_fill_rect(&list, 0, 0, 2, 2, Color{0xff0000ffu});
        paint_fill_rect(&paint, 0, 0, 1, 1, Color{0xff00ff00u});
        RasterRenderContext context = {}; context.dl = lam::up(&list); context.paint_list = lam::up(&paint); context.ui_context = lam::up(&ui);
        context.block.clip = {0, 0, 2, 2};
        Rect destination = {0, 0, 2, 2}; Bound clip = {0, 0, 2, 2};
        uint32_t references = image->snapshot_refs;
        memtrack_fault_inject(fail_at);
        bool recorded = render_painter_draw_pixels_rect(&context, (uint32_t*)image->pixels, 2, 2,
            image->pitch / 4, &destination, &clip, 128, image, SCALE_MODE_NEAREST);
        memtrack_fault_clear();
        saw_failure |= !recorded; saw_success |= recorded;
        EXPECT_EQ(context.vector_clip_depth, 0);
        EXPECT_EQ(context.paint_failed, !recorded);
        EXPECT_EQ(list.item_count(), recorded ? 5 : 2);
        ASSERT_GE(list.item_count(), 2);
        EXPECT_EQ(list.data()[1].op, DL_FILL_RECT);
        EXPECT_EQ(list.data()[1].fill_rect.color.c, 0xff00ff00u);
        EXPECT_TRUE(dl_validate(&list, nullptr));
        if (!recorded) EXPECT_EQ(image->snapshot_refs, references);
        dl_destroy(&list);
        EXPECT_EQ(image->snapshot_refs, references);
    }
    EXPECT_TRUE(saw_failure); EXPECT_TRUE(saw_success);
    image_surface_snapshot_release(image); rdt_engine_term();
}

TEST(PaintOwnership, PendingPaintPrecedesImageTransactions) {
    rdt_engine_init(0);
    ImageSurface* image = image_surface_create(2, 2); ASSERT_NE(image, nullptr);
    image_surface_snapshot_publish(image);
    UiContext ui = {}; ui.surface = lam::up(image);
    for (bool transformed : {false, true}) {
        SCOPED_TRACE(transformed);
        PaintList paint; DisplayList list;
        dl_fill_rect(&list, 0, 0, 2, 2, Color{0xff0000ffu});
        paint_fill_rect(&paint, 0, 0, 1, 1, Color{0xff00ff00u});
        RasterRenderContext context = {}; context.dl = lam::up(&list); context.paint_list = lam::up(&paint);
        context.ui_context = lam::up(&ui); context.block.clip = {0, 0, 2, 2};
        context.has_transform = transformed; context.transform = rdt_matrix_translate(1, 0);
        Rect destination = {0, 0, 2, 2}; Bound clip = {0, 0, 2, 2};
        EXPECT_TRUE(render_painter_blit_surface_scaled(&context, image, nullptr, image, &destination,
            &clip, SCALE_MODE_NEAREST, nullptr, 0, 128));
        EXPECT_FALSE(context.paint_failed);
        ASSERT_EQ(list.item_count(), transformed ? 5 : 3);
        EXPECT_EQ(list.data()[0].op, DL_FILL_RECT);
        EXPECT_EQ(list.data()[1].op, DL_FILL_RECT);
        EXPECT_EQ(list.data()[1].fill_rect.color.c, 0xff00ff00u);
        EXPECT_EQ(list.data()[transformed ? 3 : 2].op, transformed ? DL_DRAW_IMAGE : DL_BLIT_SURFACE_SCALED);
        EXPECT_TRUE(dl_validate(&list, nullptr));
        EXPECT_EQ(context.vector_clip_depth, 0);
        EXPECT_EQ(paint.size(), 0u);
        dl_destroy(&list);
    }
    EXPECT_EQ(image->snapshot_refs, 1u);
    image_surface_snapshot_release(image); rdt_engine_term();
}

TEST(PaintOwnership, PrivateCaptureTrimmingRetainsEveryCoveredPixelAndPublishedBuffers) {
    auto make_pattern = []() {
        ImageSurface* surface = image_surface_create(8, 7);
        if (!surface) return surface;
        uint32_t* pixels = (uint32_t*)surface->pixels;
        for (size_t row = 1; row < 6; row++) for (size_t column = 2; column < 6; column++)
            pixels[row * 8 + column] = 0xff204080u + (uint32_t)row;
        pixels[5 * 8 + 6] = 0x01000000u;
        return surface;
    };
    ImageSurface* surface = make_pattern(); ASSERT_NE(surface, nullptr);
    uint32_t expected[25];
    const uint32_t* pixels = (const uint32_t*)surface->pixels;
    for (size_t row = 0; row < 5; row++) for (size_t column = 0; column < 5; column++)
        expected[row * 5 + column] = pixels[(row + 1) * 8 + column + 2];
    float x, y;
    uint64_t generation = surface->generation;
    memtrack_fault_inject(0);
    bool trimmed = image_surface_trim_transparent_margin(surface, &x, &y);
    memtrack_fault_clear();
    EXPECT_TRUE(trimmed); EXPECT_FLOAT_EQ(x, 2); EXPECT_FLOAT_EQ(y, 1);
    EXPECT_EQ(surface->width, 5); EXPECT_EQ(surface->height, 5); EXPECT_EQ(surface->pitch, 20);
    EXPECT_EQ(memcmp(surface->pixels, expected, sizeof(expected)), 0);
    EXPECT_EQ(surface->generation, generation);
    EXPECT_EQ(image_surface_lookup(surface->self), surface);
    EXPECT_FALSE(image_surface_trim_transparent_margin(surface, &x, &y));
    image_surface_destroy(surface);
    surface = make_pattern(); ASSERT_NE(surface, nullptr);
    image_surface_snapshot_publish(surface);
    EXPECT_FALSE(image_surface_trim_transparent_margin(surface, &x, &y));
    EXPECT_EQ(surface->width, 8); EXPECT_EQ(surface->height, 7);
    EXPECT_FLOAT_EQ(x, 0); EXPECT_FLOAT_EQ(y, 0);
    image_surface_snapshot_release(surface);
    surface = image_surface_create(3, 2); ASSERT_NE(surface, nullptr);
    EXPECT_FALSE(image_surface_trim_transparent_margin(surface, &x, &y));
    EXPECT_EQ(surface->width, 3); EXPECT_EQ(surface->height, 2);
    image_surface_destroy(surface);
}

TEST(PaintOwnership, NativePicturesSurviveRepeatedSerialReplayAndSourceRetirement) {
    rdt_engine_init(0);
    Tvg_Paint shape = tvg_shape_new(); ASSERT_NE(shape, nullptr);
    ASSERT_EQ(tvg_shape_append_rect(shape, 4, 4, 20, 18, 0, 0, true), TVG_RESULT_SUCCESS);
    ASSERT_EQ(tvg_shape_set_fill_color(shape, 240, 30, 10, 180), TVG_RESULT_SUCCESS);
    ASSERT_EQ(tvg_paint_translate(shape, 2, 3), TVG_RESULT_SUCCESS);
    RdtPicture* picture = rdt_picture_take_tvg_paint(shape, 32, 32); ASSERT_NE(picture, nullptr);
    DisplayList list;
    dl_draw_picture(&list, rdt_picture_dup(picture), 210, nullptr);
    rdt_picture_free(picture);
    ASSERT_EQ(list.item_count(), 1);
    DisplayItem frozen = list.data()[0];
    uint32_t reference[32 * 32];
    for (uint32_t background : {0xff000000u, 0xffffffffu}) for (int repeat = 0; repeat < 3; repeat++) {
        ImageSurface* surface = image_surface_create(32, 32); ASSERT_NE(surface, nullptr);
        for (size_t i = 0; i < 32 * 32; i++) ((uint32_t*)surface->pixels)[i] = background;
        RdtVector vec = {}; rdt_vector_init(&vec, (uint32_t*)surface->pixels, 32, 32, surface->pitch / 4);
        Bound clip = {0, 0, 32, 32}; ScratchArena scratch = {};
        dl_replay(&list, &vec, surface, &clip, &scratch, 1, nullptr);
        rdt_vector_destroy(&vec); scratch_release(&scratch);
        EXPECT_NE(((uint32_t*)surface->pixels)[10 * 32 + 10], background);
        EXPECT_EQ(((uint32_t*)surface->pixels)[5 * 32 + 5], background);
        if (repeat == 0) memcpy(reference, surface->pixels, sizeof(reference));
        else EXPECT_EQ(memcmp(reference, surface->pixels, sizeof(reference)), 0);
        EXPECT_EQ(memcmp(&frozen, list.data(), sizeof(frozen)), 0);
        image_surface_destroy(surface);
    }
    dl_destroy(&list); rdt_engine_term();
}

TEST(PaintOwnership, PathSnapshotsShareGeometryAndDetachWithoutChangingPublishedPaint) {
    RdtPath* source = rdt_path_new(); ASSERT_NE(source, nullptr);
    rdt_path_move_to(source, 1, 2); rdt_path_line_to(source, 10, 12);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    DisplayList list;
    dl_fill_path(&list, source, Color{0xff0000ffu}, RDT_FILL_WINDING, nullptr);
    dl_stroke_path(&list, source, Color{0xff00ff00u}, 2, RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0, nullptr);
    ASSERT_EQ(list.item_count(), 2);
    RdtPath* fill = list.data()[0].fill_path.path;
    RdtPath* stroke = list.data()[1].stroke_path.path;
    EXPECT_EQ(fill->storage, stroke->storage); EXPECT_EQ(fill->storage, source->storage);
    EXPECT_EQ(counters.path_copy_bytes, 0u);
    uint64_t frozen_hash = rdt_path_hash(fill);
    memtrack_fault_inject(0); rdt_path_line_to(source, 20, 25); memtrack_fault_clear();
    EXPECT_EQ(source->storage, fill->storage); EXPECT_EQ(source->count, 2);
    rdt_path_line_to(source, 20, 25);
    EXPECT_NE(source->storage, fill->storage); EXPECT_EQ(source->count, 3);
    EXPECT_EQ(fill->count, 2); EXPECT_EQ(rdt_path_hash(fill), frozen_hash);
    EXPECT_NE(rdt_path_hash(source), frozen_hash);
    rdt_path_free(source);
    float left, top, right, bottom;
    ASSERT_TRUE(rdt_path_get_bounds(stroke, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right, 10); EXPECT_FLOAT_EQ(bottom, 12);
    dl_destroy(&list);
}

TEST(PaintOwnership, CopiedBorrowedChunksPinSharedPaintArraysAfterAllSourceOwnersRetire) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    DisplayList source;
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 0, 0, 20, 20, 0, 0);
    RdtGradientStop stops[] = {{0, 220, 30, 10, 200}, {1, 10, 30, 220, 100}};
    int marker = dl_begin_element(&source, 4200, 0, 0, 20, 20);
    dl_fill_linear_gradient(&source, path, 0, 0, 20, 20, stops, 2, RDT_FILL_WINDING, nullptr, nullptr);
    dl_end_element(&source, marker); rdt_path_free(path);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 4200); ASSERT_NE(fragment, nullptr);
    DisplayList borrowed, copied;
    ASSERT_TRUE(retained_dl_append_fragment(&borrowed, fragment));
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    ASSERT_TRUE(dl_copy_range(&copied, &borrowed, 0, borrowed.item_count() - 1));
    EXPECT_EQ(counters.payload_copy_bytes, 0u); EXPECT_EQ(counters.shared_array_retains, 0u);
    EXPECT_EQ(test_radiant_command(&copied, 1).fill_linear_gradient.stops, test_radiant_command(&borrowed, 1).fill_linear_gradient.stops);
    retained_dl_cache_destroy(cache); dl_destroy(&source); dl_destroy(&borrowed);
    EXPECT_EQ(test_radiant_command(&copied, 1).fill_linear_gradient.stops[0].r, 220);
    EXPECT_TRUE(dl_validate(&copied, nullptr));
    dl_destroy(&copied); pool_destroy(pool);
}

TEST(PaintOwnership, TransparentCaptureComposesVectorCpuImagesAndGlyphsAcrossBackdrops) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    DisplayList list; dl_init(&list, arena);
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_circle(path, 16, 16, 13, 11);
    dl_push_clip(&list, path, nullptr);
    dl_fill_rect(&list, 3, 4, 18, 16, Color{0x803020e0u});
    dl_fill_rounded_rect(&list, 8, 10, 22, 18, 4, 3, Color{0x9040e030u});
    dl_stroke_path(&list, path, Color{0xa0e04030u}, 3, RDT_CAP_ROUND, RDT_JOIN_ROUND, nullptr, 0, 0, nullptr);
    RdtGradientStop stops[] = {{0, 220, 20, 50, 90}, {1, 10, 80, 230, 170}};
    dl_fill_linear_gradient(&list, path, 0, 0, 32, 32, stops, 2, RDT_FILL_WINDING, nullptr, nullptr);
    dl_pop_clip(&list); rdt_path_free(path);
    Bound cpu_clip = {2, 2, 30, 30};
    dl_fill_surface_rect(&list, 2, 8, 18, 13, 0x8030e070u, &cpu_clip, nullptr, 0);
    ImageSurface* image = image_surface_create(4, 4); ASSERT_NE(image, nullptr);
    image->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    for (size_t i = 0; i < 16; i++) ((uint32_t*)image->pixels)[i] = i % 2 ? 0x806020c0u : 0x40306090u;
    dl_blit_surface_scaled(&list, image, 14, 8, 12, 10, SCALE_MODE_LINEAR, &cpu_clip, nullptr, 0, 190);
    uint8_t coverage[] = {0, 32, 128, 255, 16, 80, 170, 230, 16, 190, 30, 70, 0, 255, 128, 64};
    GlyphBitmap glyph = {}; glyph.buffer = coverage; glyph.width = glyph.height = glyph.pitch = 4;
    glyph.pixel_mode = GLYPH_PIXEL_GRAY;
    dl_draw_glyph(&list, &glyph, 9, 11, Color{0xa0e03010u}, false, &cpu_clip);
    ASSERT_TRUE(dl_supports_premultiplied_capture(&list));
    uint32_t layer[32 * 32] = {}, actual[32 * 32];
    Bound clip = {0, 0, 32, 32}; ScratchArena scratch = {};
    auto replay = [&](uint32_t* pixels) {
        ImageSurface surface = {}; surface.width = surface.height = 32;
        surface.pitch = 32 * 4; surface.pixels = pixels; surface.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
        RdtVector vec = {}; rdt_vector_init(&vec, pixels, 32, 32, 32);
        dl_replay(&list, &vec, &surface, &clip, &scratch, 1, nullptr);
        rdt_vector_destroy(&vec); scratch_release(&scratch);
    };
    replay(layer); EXPECT_NE(layer[16 * 32 + 16], 0u);
    for (uint32_t background : {0xff000000u, 0xffffffffu, 0xffa03020u, 0x80203040u}) {
        for (size_t i = 0; i < 32 * 32; i++) actual[i] = background;
        replay(actual);
        unsigned largest_error = 0;
        for (size_t i = 0; i < 32 * 32; i++) {
            uint32_t composed = render_pixel_source_over_premultiplied_pair(background, layer[i]);
            for (unsigned shift = 0; shift < 32; shift += 8) {
                int delta = (int)((composed >> shift) & 255) - (int)((actual[i] >> shift) & 255);
                unsigned error = (unsigned)(delta < 0 ? -delta : delta);
                if (error > largest_error) largest_error = error;
            }
        }
        EXPECT_LE(largest_error, 2u) << "background=" << background;
    }
    list.back().draw_glyph.is_color_emoji = true;
    EXPECT_FALSE(dl_supports_premultiplied_capture(&list));
    list.back().draw_glyph.is_color_emoji = false;
    dl_save_backdrop(&list, 0, 0, 32, 32);
    EXPECT_FALSE(dl_supports_premultiplied_capture(&list));
    dl_destroy(&list); image_surface_destroy(image); arena_destroy(arena); rdt_engine_term();
}

TEST(PaintOwnership, NestedFramesKeepIndependentPayloadArenasAndReuseOuterCapacity) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session, "<html><body>frame</body></html>", 80, 60, false));
    UiContext* ui = session.ui_context;
    ViewTree* tree = ui->document->view_tree;
    DisplayItem* retained_storage = nullptr;
    RdtVectorImpl* retained_vector = nullptr;
    {
        RenderProfiler profiler = {}; RasterRenderContext first, second;
        RenderFrameScope outer(&first, ui, tree, &profiler);
        retained_vector = first.vec.impl;
        RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
        rdt_path_add_rect(path, 0, 0, 20, 20, 0, 0);
        RdtGradientStop stops[] = {{0, 255, 0, 0, 255}, {1, 0, 0, 255, 255}};
        dl_fill_linear_gradient(outer.list(), path, 0, 0, 20, 20, stops, 2, RDT_FILL_WINDING, nullptr, nullptr);
        ASSERT_TRUE(dl_validate(outer.list(), nullptr));
        retained_storage = outer.list()->data();
        {
            RenderFrameScope inner(&second, ui, tree, &profiler);
            EXPECT_NE(second.vec.impl.get(), first.vec.impl.get());
            EXPECT_NE(inner.list()->arena.arena, outer.list()->arena.arena);
            EXPECT_NE(second.scratch.arena, first.scratch.arena);
            dl_fill_linear_gradient(inner.list(), path, 0, 0, 20, 20, stops, 2, RDT_FILL_WINDING, nullptr, nullptr);
            EXPECT_TRUE(dl_validate(inner.list(), nullptr));
        }
        EXPECT_TRUE(dl_validate(outer.list(), nullptr)); rdt_path_free(path);
    }
    {
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope next(&context, ui, tree, &profiler);
        EXPECT_EQ(next.list()->data(), retained_storage); EXPECT_TRUE(next.list()->empty());
        EXPECT_EQ(context.vec.impl.get(), retained_vector);
    }
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, FreshFrameScopesReuseGradientAndDashValuesWithoutRecopyingThem) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session, "<html><body></body></html>", 40, 30, false));
    UiContext* ui = session.ui_context; ViewTree* tree = ui->document->view_tree;
    ASSERT_EQ(ui->surface->width, 40); ASSERT_EQ(ui->surface->height, 30);
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 4, 4, 24, 20, 0, 0);
    RdtGradientStop stops[] = {{0, 255, 0, 0, 255}, {1, 0, 0, 255, 255}};
    float dashes[] = {2, 3};
    uint32_t expected[40 * 30];
    for (unsigned repeat = 0; repeat < 4; repeat++) {
        memset(ui->surface->pixels, 0, ui->surface->pitch * ui->surface->height);
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        RenderFrameScope frame(&context, ui, tree, &profiler);
        dl_fill_linear_gradient(frame.list(), path, 4, 4, 28, 4, stops, 2,
            RDT_FILL_WINDING, nullptr, nullptr);
        dl_stroke_path(frame.list(), path, Color{0xff000000u}, 2, RDT_CAP_BUTT,
            RDT_JOIN_MITER, dashes, 2, 0, nullptr);
        dl_replay(frame.list(), &context.vec, ui->surface, &context.block.clip,
            &context.scratch, 1, nullptr);
        if (!repeat) {
            EXPECT_GT(counters.payload_copy_bytes, 0u);
            memcpy(expected, ui->surface->pixels, sizeof(expected));
            EXPECT_NE(expected[10 * 40 + 10], 0u);
        } else {
            EXPECT_EQ(counters.payload_copy_bytes, 0u);
            EXPECT_EQ(counters.shared_array_reuses, 2u);
            EXPECT_EQ(memcmp(expected, ui->surface->pixels, sizeof(expected)), 0);
        }
    }
    rdt_path_free(path); render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, FullFrameAdmissionReclaimsUnusedCaptureCapacity) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0'><svg width='60' height='40'>"
        "<path d='M2 2 H30 V30 H2 Z' fill='red'/></svg><span>cache</span></body></html>", 80, 60, false));
    UiContext* ui = session.ui_context;
    ViewTree* tree = ui->document->view_tree;
    {
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope seed(&context, ui, tree, &profiler);
        ScratchMark before = scratch_mark(&seed.list()->arena);
        ASSERT_NE(scratch_alloc(&seed.list()->arena, (size_t)9 << 20), nullptr);
        scratch_restore(&seed.list()->arena, before);
    }
    size_t captured_capacity = arena_total_allocated(tree->display_list_arena);
    ASSERT_GT(captured_capacity, (size_t)8 << 20);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
    for (unsigned i = 0; i < 4; i++) {
        doc_state_mark_dirty(ui->document->state);
        ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    }
    EXPECT_GT(counters.full_frame_reuses, 0u);
    EXPECT_LT(arena_total_allocated(tree->display_list_arena), captured_capacity);
    EXPECT_EQ(((uint32_t*)ui->surface->pixels)[10 * ui->surface->width + 10], 0xff0000ffu);
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, ForcedFramesReuseSettledRecordingAndInvalidateChangedSvgPaint) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0'><svg width='60' height='40'>"
        "<path id='ink' d='M2 2 H30 V30 H2 Z' fill='red'/></svg><span>cache</span></body></html>", 80, 60, false));
    UiContext* ui = session.ui_context;
    ViewTree* tree = ui->document->view_tree;
    DocState* state = ui->document->state;
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
    auto render = [&]() {
        doc_state_mark_dirty(state);
        return render_output_render_view_tree_to_target(ui, tree, &target);
    };
    for (int i = 0; i < 4; i++) ASSERT_EQ(render(), 0);
    EXPECT_GT(counters.layer_recordings, 0u); // frame reuse must allow default layer admission to warm.
    EXPECT_GT(counters.full_frame_reuses, 0u);
    EXPECT_GT(counters.resource_capture_chunks, 0u);
    EXPECT_EQ(((uint32_t*)ui->surface->pixels)[10 * ui->surface->width + 10], 0xff0000ffu);
    DisplayItem* storage = tree->render_list->data();
    ASSERT_FALSE(tree->render_list->empty());
    counters = {};
    ASSERT_EQ(render(), 0);
    EXPECT_EQ(counters.full_frame_reuses, 1u); EXPECT_EQ(counters.command_copy_bytes, 0u);
    EXPECT_EQ(counters.resource_command_checks, 0u);
    EXPECT_EQ(counters.resource_summary_checks, 1u);
    EXPECT_EQ(counters.resource_capture_commands, 0u);
    EXPECT_EQ(counters.validation_command_checks, 0u);
    EXPECT_EQ(counters.glyph_command_checks, 0u);
    EXPECT_EQ(counters.media_command_checks, 0u);
    EXPECT_EQ(counters.path_parses, 0u); EXPECT_EQ(counters.native_canvases, 0u);
    EXPECT_EQ(tree->render_list->data(), storage);
    ImageSurface* layer = nullptr;
    DisplayListCursor layers(tree->render_list);
    while (const DisplayItem* item = layers.next())
        if (item->op == DL_DRAW_IMAGE && !item->draw_image.resource.is_null()) {
            layer = image_surface_lookup(item->draw_image.resource); break;
        }
    ASSERT_NE(layer, nullptr);
    image_surface_bump_generation(layer); // no document key change accompanies a surface edit.
    retained_dl_cache_set_storage_budget(state->retained_dl_cache, 0);
    counters = {};
    ASSERT_EQ(render(), 0);
    EXPECT_EQ(counters.full_frame_reuses, 0u);
    EXPECT_GT(counters.resource_surface_checks, 0u);
    EXPECT_GT(counters.resource_capture_commands, 0u); // failed chunk admission keeps the flat snapshot fallback.
    EXPECT_EQ(((uint32_t*)ui->surface->pixels)[10 * ui->surface->width + 10], 0xff0000ffu);
    DomElement* ink = dom_find_element_by_id(ui->document->root, "ink"); ASSERT_NE(ink, nullptr);
    ASSERT_TRUE(ink->set_attribute("fill", "blue"));
    dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, ink, ink->parent, "fill", "red");
    counters = {};
    ASSERT_EQ(render(), 0); EXPECT_EQ(counters.full_frame_reuses, 0u);
    EXPECT_EQ(((uint32_t*)ui->surface->pixels)[10 * ui->surface->width + 10], 0xffff0000u);
    for (int i = 0; i < 3; i++) ASSERT_EQ(render(), 0);
    counters = {};
    target.transparent_background = true;
    ASSERT_EQ(render(), 0); // background changes cannot reuse backdrop-dependent commands.
    EXPECT_EQ(counters.full_frame_reuses, 0u);
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, ForcedFramesShareCapturedChunksAndKeepThemAfterIndexEviction) {
    rdt_engine_init(0);
    for (unsigned cards : {512u, 1536u}) {
        SCOPED_TRACE(cards);
        StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
        strbuf_append_str(html, "<html><body style='margin:0;font-size:0'>");
        for (unsigned child = 0; child < cards; child++)
            strbuf_append_str(html, "<div style='display:inline-block;width:4px;height:4px;background:red'></div>");
        strbuf_append_str(html, "</body></html>");
        RenderExportSession session = {};
        ASSERT_TRUE(render_export_session_begin_source(&session, html->str, 512, 60, false));
        strbuf_free(html);
        UiContext* ui = session.ui_context; ViewTree* tree = ui->document->view_tree;
        DocState* state = ui->document->state;
        RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
        auto render = [&]() {
            doc_state_mark_dirty(state);
            return render_output_render_view_tree_to_target(ui, tree, &target);
        };
        ASSERT_EQ(render(), 0);
        size_t expanded = 0, references = 0;
        DisplayListCursor cursor(tree->render_list);
        while (cursor.next()) expanded++;
        EXPECT_FALSE(cursor.failed);
        EXPECT_GT(expanded, 1500u);
        for (const DisplayItem& item : *tree->render_list) references += item.op == DL_DRAW_FRAGMENT;
        if (cards == 512u) {
            EXPECT_LT(tree->render_list->size(), 16u);
            EXPECT_LT(tree->render_list->capacity() * sizeof(DisplayItem), 16384u);
            EXPECT_GT(references, 0u);
        } else {
            // the shared graph exceeds frame admission; the smaller flat recording must still reuse.
            EXPECT_EQ(references, 0u);
        }
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        ASSERT_EQ(render(), 0);
        EXPECT_EQ(counters.full_frame_reuses, 1u); EXPECT_EQ(counters.command_copy_bytes, 0u);
        // dropping cache publications must not retire the full frame's independent chunk leases.
        retained_dl_cache_set_storage_budget(state->retained_dl_cache, 0);
        retained_dl_cache_begin_frame(state->retained_dl_cache);
        counters = {};
        ASSERT_EQ(render(), 0);
        EXPECT_EQ(counters.full_frame_reuses, 1u); EXPECT_EQ(counters.command_copy_bytes, 0u);
        EXPECT_TRUE(dl_validate(tree->render_list, nullptr));
        test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
        render_export_session_end(&session);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, SelectiveFramesRefreshUnchangedIndexEntriesWithoutRepublishingThem) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    // child lines change the inline-block baseline; align tops to preserve overlapping sibling damage.
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<html><body style='margin:0;font-size:0'>"
        "<div id='stable-card' style='display:inline-block;vertical-align:top;width:40px;height:40px;background:red'>");
    for (unsigned child = 0; child < 256; child++)
        strbuf_append_str(html, "<div style='display:inline-block;width:1px;height:1px;background:red'></div>");
    strbuf_append_str(html, "</div><div id='damaged-card' style='display:inline-block;vertical-align:top;position:relative;left:-20px;"
        "width:40px;height:40px;background:blue'></div></body></html>");
    ASSERT_TRUE(render_export_session_begin_source(&session, html->str, 80, 50, false));
    strbuf_free(html);
    UiContext* ui = session.ui_context;
    DocState* state = ui->document->state;
    ViewTree* tree = ui->document->view_tree;
    RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
    doc_state_mark_dirty(state);
    ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    DomElement* stable = dom_find_element_by_id(ui->document->root, "stable-card"); ASSERT_NE(stable, nullptr);
    uint32_t stable_view_id = static_cast<View*>(stable)->id;
    const auto* entry = retained_dl_cache_get(state->retained_dl_cache, stable_view_id); ASSERT_NE(entry, nullptr);
    DomElement* damaged = dom_find_element_by_id(ui->document->root, "damaged-card"); ASSERT_NE(damaged, nullptr);
    test_radiant_expect_surface_pixel(ui->surface, 30, 10, 0, 0, 255);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    dirty_mark_element(state, static_cast<View*>(damaged));
    ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    auto stats = retained_dl_cache_stats(state->retained_dl_cache);
    EXPECT_GT(counters.fragment_references, 0u); EXPECT_GT(stats.reused_captures, 0u);
    EXPECT_LT(counters.retained_index_publications, (size_t)stats.captured + stats.reused_captures);
    EXPECT_EQ(counters.retained_index_prune_checks, 1u);
    // the dirty empty sibling also qualifies through the exact solid-box admission.
    EXPECT_EQ(counters.html_box_reuses, 1u);
    EXPECT_EQ(counters.retained_membership_nodes, 2u);
    EXPECT_EQ(counters.retained_membership_groups, 2u);
    EXPECT_EQ(counters.retained_dirty_subtree_searches, 0u);
    EXPECT_GT(counters.retained_dirty_ancestry_queries, 0u);
    EXPECT_EQ(stats.reused_captures, 258u);
    EXPECT_EQ(retained_dl_cache_get(state->retained_dl_cache, stable_view_id), entry);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
    test_radiant_expect_surface_pixel(ui->surface, 30, 10, 0, 0, 255);
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, SelectiveFramesRejectDirtyAncestorsWithValidatedAndMissingSourceHints) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0'><div id='parent' style='width:40px;height:40px;background:red'>"
        "<div id='child' style='width:20px;height:20px;background:blue'></div></div></body></html>",
        60, 50, false));
    UiContext* ui = session.ui_context; ViewTree* tree = ui->document->view_tree;
    DocState* state = ui->document->state;
    RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
    doc_state_mark_dirty(state);
    ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    DomElement* parent = dom_find_element_by_id(ui->document->root, "parent"); ASSERT_NE(parent, nullptr);
    DomElement* child = dom_find_element_by_id(ui->document->root, "child"); ASSERT_NE(child, nullptr);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
    for (bool hint : {true, false}) {
        // change the producer payload directly to test selective damage independently of full-style keys.
        child->ensure_boundary(tree)->background->color.c = hint ? 0xff00ff00u : 0xff0000ffu;
        dirty_mark_element(state, static_cast<View*>(child));
        if (!hint) state->dirty_tracker.dirty_list->source_view_address = nullptr;
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
        EXPECT_GT(retained_dl_cache_stats(state->retained_dl_cache).reuse_rejected_dirty, 0);
        if (hint) {
            EXPECT_GT(counters.retained_dirty_ancestry_queries, 0u);
            EXPECT_EQ(counters.retained_dirty_subtree_searches, 0u);
        } else EXPECT_GT(counters.retained_dirty_subtree_searches, 0u);
        test_radiant_expect_surface_pixel(ui->surface, 10, 10, hint ? 0 : 255, hint ? 255 : 0, 0);
        test_radiant_expect_surface_pixel(ui->surface, 30, 30, 255, 0, 0);
    }
    parent->ensure_boundary(tree)->background->color.c = 0xff00ffffu;
    dirty_mark_element(state, static_cast<View*>(parent));
    ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    test_radiant_expect_surface_pixel(ui->surface, 30, 30, 255, 255, 0);
    render_export_session_end(&session); rdt_engine_term();
}

static const char* TEST_HTML_BOX_STYLE =
    "display:inline-block;vertical-align:top;margin-left:8px;width:24px;height:16px;background:red;";

static StrBuf* test_html_box_source(const char* decoration = "", const char* children = "",
        const char* parent_style = "", const char* spacer_style = "height:8px") {
    StrBuf* html = strbuf_new();
    if (!html) return nullptr;
    strbuf_append_str(html, "<html><body style='margin:0;"); strbuf_append_str(html, parent_style);
    strbuf_append_str(html, "'><div id='spacer' style='"); strbuf_append_str(html, spacer_style);
    strbuf_append_str(html, "'></div><div id='box' style='"); strbuf_append_str(html, TEST_HTML_BOX_STYLE);
    strbuf_append_str(html, decoration); strbuf_append_str(html, "'>");
    strbuf_append_str(html, children); strbuf_append_str(html, "</div></body></html>");
    return html;
}

TEST(PaintOwnership, EmptyHtmlBoxesReuseCanonicalPaintAfterSiblingReflow) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) {
        RenderExportSession session = {};
        StrBuf* html = test_html_box_source(); ASSERT_NE(html, nullptr);
        ASSERT_TRUE(render_export_session_begin_source(&session, html->str, 80, 80, false)); strbuf_free(html);
        UiContext* ui = session.ui_context; auto* doc = ui->document.get(); auto* tree = doc->view_tree.get();
        ui_context_set_device_scale(ui, density, density);
        ui_context_create_surface(ui, (int)(80 * density), (int)(80 * density));
        RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
        auto render = [&]() {
            doc_state_mark_dirty(doc->state);
            return render_output_render_view_tree_to_target(ui, tree, &target);
        };
        ASSERT_EQ(render(), 0);
        auto* box = dom_find_element_by_id(doc->root, "box"); ASSERT_NE(box, nullptr);
        auto* spacer = dom_find_element_by_id(doc->root, "spacer"); ASSERT_NE(spacer, nullptr);
        DisplayList original;
        ASSERT_TRUE(retained_dl_append_fragment(&original, retained_dl_cache_get(doc->state->retained_dl_cache, box->DomNode::id)));
        const auto* canonical = original.front().draw_fragment.owner.get();
        DisplayList placed;
        for (const char* style : {"height:20.5px", "height:32px", "height:8px"}) {
            ASSERT_TRUE(spacer->set_attribute("style", style));
            dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, spacer, spacer->parent, "style", nullptr);
            layout_html_doc(ui, doc, false);
            RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
            ASSERT_EQ(render(), 0);
            EXPECT_EQ(counters.full_frame_reuses, 0u);
            DisplayList current;
            ASSERT_TRUE(retained_dl_append_fragment(&current,
                retained_dl_cache_get(doc->state->retained_dl_cache, box->DomNode::id)));
            EXPECT_EQ(current.front().draw_fragment.owner.get(), canonical); dl_destroy(&current);
            EXPECT_EQ(counters.html_box_reuses, 1u); EXPECT_EQ(counters.html_box_recordings, 0u);
            EXPECT_GT(retained_dl_cache_stats(doc->state->retained_dl_cache).reused_captures, 0u);
            test_radiant_expect_surface_pixel(ui->surface, (unsigned)(10 * density),
                (unsigned)((box->y + 4) * density), 255, 0, 0);
            dl_clear(&placed);
            ASSERT_TRUE(retained_dl_append_solid_box(&placed,
                retained_dl_cache_get(doc->state->retained_dl_cache, box->DomNode::id),
                {box->x * density, box->y * density, box->width * density, box->height * density},
                box->boundary()->background->color));
        }
        ImageSurface* expected = image_surface_create(ui->surface->width, ui->surface->height); ASSERT_NE(expected, nullptr);
        memcpy(expected->pixels, ui->surface->pixels, ui->surface->pitch * ui->surface->height);
        render_export_session_end(&session);
        EXPECT_TRUE(dl_validate(&original, nullptr)); dl_destroy(&original);
        for (unsigned mode = 0; mode < 3; mode++) {
            ImageSurface* replay = image_surface_create(expected->width, expected->height); ASSERT_NE(replay, nullptr);
            memset(replay->pixels, 255, replay->pitch * replay->height);
            DirtyRect dirty = {}; dirty.width = (float)replay->width; dirty.height = (float)replay->height;
            test_radiant_replay_surface(&placed, replay, mode, &dirty);
            EXPECT_EQ(memcmp(replay->pixels, expected->pixels, replay->pitch * replay->height), 0);
            image_surface_destroy(replay);
        }
        image_surface_destroy(expected); dl_destroy(&placed);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, HtmlBoxAdmissionFallsBackForChangedAndUnsupportedPaint) {
    struct Case { const char* style; const char* children = ""; const char* parent_style = ""; bool scale_change = false; };
    const Case cases[] = {{"background:blue"}, {"width:25px"}, {"opacity:.5"}, {"border-radius:4px"},
        {"transform:translateX(.5px)"}, {"box-shadow:2px 2px 2px blue"}, {"clip-path:inset(2px)"},
        {"margin-left:-.5px"}, {"background:linear-gradient(red,blue)"}, {"display:block"},
        {"visibility:hidden"}, {"", "<span>x</span>"},
        {"", "", "overflow:hidden;border-radius:8px;width:30px;height:25px"}, {"", "", "", true}};
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (const Case& test : cases) {
        SCOPED_TRACE(test.style);
        SCOPED_TRACE(test.children);
        SCOPED_TRACE(test.parent_style);
        SCOPED_TRACE(density);
        RenderExportSession candidate = {}, reference = {};
        StrBuf* html = test_html_box_source("", test.children); ASSERT_NE(html, nullptr);
        ASSERT_TRUE(render_export_session_begin_source(&candidate, html->str, 80, 80, false)); strbuf_free(html);
        UiContext* ui = candidate.ui_context; auto* doc = ui->document.get();
        ui_context_set_device_scale(ui, density, density);
        ui_context_create_surface(ui, (int)(80 * density), (int)(80 * density));
        RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
        auto render = [&]() { doc_state_mark_dirty(doc->state); return render_output_render_view_tree_to_target(ui, doc->view_tree, &target); };
        ASSERT_EQ(render(), 0);
        auto* box = dom_find_element_by_id(doc->root, "box"); ASSERT_NE(box, nullptr);
        auto* spacer = dom_find_element_by_id(doc->root, "spacer"); ASSERT_NE(spacer, nullptr);
        StrBuf* style = strbuf_new(); ASSERT_NE(style, nullptr);
        strbuf_append_str(style, TEST_HTML_BOX_STYLE); strbuf_append_str(style, test.style);
        ASSERT_TRUE(box->set_attribute("style", style->str)); strbuf_free(style);
        dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, box, box->parent, "style", nullptr);
        ASSERT_TRUE(spacer->set_attribute("style", "height:20.5px"));
        dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, spacer, spacer->parent, "style", nullptr);
        if (*test.parent_style) {
            style = strbuf_new(); ASSERT_NE(style, nullptr);
            strbuf_append_str(style, "margin:0;"); strbuf_append_str(style, test.parent_style);
            ASSERT_TRUE(box->parent->as_element()->set_attribute("style", style->str)); strbuf_free(style);
            dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, box->parent, box->parent->parent, "style", nullptr);
        }
        float current_density = test.scale_change ? density * 2 : density;
        ui_context_set_device_scale(ui, current_density, current_density);
        ui_context_create_surface(ui, (int)(80 * current_density), (int)(80 * current_density));
        layout_html_doc(ui, doc, false);
        RenderPipelineMetrics counters;
        { RenderPipelineMetricsScope probe(&counters); ASSERT_EQ(render(), 0); }
        EXPECT_EQ(counters.full_frame_reuses, 0u); EXPECT_EQ(counters.html_box_reuses, 0u);
        html = test_html_box_source(test.style, test.children, test.parent_style, "height:20.5px"); ASSERT_NE(html, nullptr);
        ASSERT_TRUE(render_export_session_begin_source(&reference, html->str, 80, 80, false)); strbuf_free(html);
        ui_context_set_device_scale(reference.ui_context, current_density, current_density);
        ui_context_create_surface(reference.ui_context, ui->surface->width, ui->surface->height);
        doc_state_mark_dirty(reference.ui_context->document->state);
        ASSERT_EQ(render_output_render_view_tree_to_target(reference.ui_context,
            reference.ui_context->document->view_tree, &target), 0);
        EXPECT_EQ(memcmp(ui->surface->pixels, reference.ui_context->surface->pixels,
            ui->surface->pitch * ui->surface->height), 0);
        render_export_session_end(&reference); render_export_session_end(&candidate);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, HtmlBoxAdmissionSkipsExternalContentAndHonorsFrameSetting) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    StrBuf* html = test_html_box_source(); ASSERT_NE(html, nullptr);
    ASSERT_TRUE(render_export_session_begin_source(&session, html->str, 80, 80, false)); strbuf_free(html);
    auto* ui = session.ui_context.get(); auto* tree = ui->document->view_tree.get();
    RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
    ASSERT_EQ(render_output_render_view_tree_to_target(ui, tree, &target), 0);
    auto* box = lam::view_require_block(dom_find_element_by_id(ui->document->root, "box")); ASSERT_NE(box, nullptr);
    for (unsigned mode = 0; mode < 4; mode++) {
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope frame(&context, ui, tree, &profiler);
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        context.html_box_cache_enabled = mode != 2;
        DirtyRect dirty = {}; DirtyTracker tracker = {};
        if (mode == 3) {
            // overlapping sibling damage cannot override a known color mismatch in the cached primitive.
            Color blue = {}; blue.b = blue.a = 255;
            box->boundary_mut()->background->color = blue;
            dirty.x = box->x; dirty.y = box->y; dirty.width = box->width; dirty.height = box->height;
            dirty.source_view_id = dom_find_element_by_id(ui->document->root, "spacer")->DomNode::id;
            tracker.dirty_list = &dirty; context.dirty_tracker = lam::up(&tracker);
            context.has_dirty_union = true;
            context.dirty_union = {box->x, box->y, box->x + box->width, box->y + box->height};
        }
        if (mode == 1) {
            render_block_view_content(&context, box, [](RasterRenderContext* raster, ViewBlock*) {
                Color blue = {}; blue.b = blue.a = 255;
                rc_fill_rect(raster, 10, 10, 4, 4, blue);
            });
        } else render_block_view(&context, box);
        EXPECT_TRUE(dl_validate(frame.list(), nullptr));
        EXPECT_EQ(counters.html_box_reuses, mode == 0 ? 1u : 0u);
        bool content = false;
        DisplayListCursor cursor(frame.list());
        while (const auto* item = cursor.next())
            content |= item->op == DL_FILL_RECT && item->fill_rect.color.b == 255;
        EXPECT_EQ(content, mode == 1 || mode == 3); EXPECT_FALSE(cursor.failed);
        if (mode == 2) EXPECT_GT(counters.path_clones, 0u);
    }
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, ClipElisionPreservesFractionalSolidBoxPixels) {
    rdt_engine_init(0);
    for (float density : {1.0f, 2.0f}) for (const char* color : {"red", "rgba(255,0,0,.5)"}) {
        RenderExportSession sessions[2] = {};
        for (unsigned control = 0; control < 2; control++) {
            StrBuf* style = strbuf_new(); ASSERT_NE(style, nullptr);
            strbuf_append_format(style, "margin-left:8.25px;width:24.5px;height:16.25px;background:%s;", color);
            if (control) strbuf_append_str(style, "border:0 solid transparent");
            StrBuf* html = test_html_box_source(style->str, "", "", "height:8.5px"); ASSERT_NE(html, nullptr);
            strbuf_free(style);
            ASSERT_TRUE(render_export_session_begin_source(&sessions[control], html->str, 80, 80, false)); strbuf_free(html);
            auto* ui = sessions[control].ui_context.get();
            ui_context_set_device_scale(ui, density, density);
            ui_context_create_surface(ui, (int)(80 * density), (int)(80 * density));
            RenderOutputTarget target = {}; render_output_target_init(&target, RENDER_OUTPUT_SCREEN, nullptr);
            RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
            ASSERT_EQ(render_output_render_view_tree_to_target(ui, ui->document->view_tree, &target), 0);
            EXPECT_EQ(counters.html_box_recordings, control ? 0u : 1u);
        }
        auto* surface = sessions[0].ui_context->surface.get();
        EXPECT_EQ(memcmp(surface->pixels, sessions[1].ui_context->surface->pixels, surface->pitch * surface->height), 0);
        render_export_session_end(&sessions[1]); render_export_session_end(&sessions[0]);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, TextOverlayUsesSerialReplayWithATextFreeRetainedDocument) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0;background:#404040'><input id='target' value='' "
        "style='width:20px;height:20px;background:red;font:16px Arial;border:0;padding:0'></body></html>", 240, 180, false));
    UiContext* ui = session.ui_context; ViewTree* tree = ui->document->view_tree;
    DocState* state = ui->document->state;
    DomElement* target = dom_find_element_by_id(ui->document->root, "target"); ASSERT_NE(target, nullptr);
    ASSERT_NE(lam::view_require_block(target)->font, nullptr);
    context_menu_open(state, static_cast<View*>(target), 20, 20);
    RenderOutputTarget output = {}; render_output_target_init(&output, RENDER_OUTPUT_SCREEN, nullptr);
    auto render = [&]() { doc_state_mark_dirty(state); return render_output_render_view_tree_to_target(ui, tree, &output); };
    for (unsigned i = 0; i < 4; i++) ASSERT_EQ(render(), 0);
    ASSERT_FALSE(dl_contains_glyphs(tree->render_list));
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    ASSERT_EQ(render(), 0);
    EXPECT_EQ(counters.full_frame_reuses, 1u);
    EXPECT_GT(counters.validation_command_checks, 0u);
    EXPECT_EQ(counters.glyph_command_checks, 0u); EXPECT_EQ(counters.media_command_checks, 0u);
    EXPECT_EQ(counters.tile_work_ns, 0u);
    test_radiant_expect_surface_pixel(ui->surface, 24, 24, 255, 255, 255);
    EXPECT_FALSE(dl_contains_glyphs(tree->render_list));
    context_menu_close(state);
    ASSERT_EQ(render(), 0);
    test_radiant_expect_surface_pixel(ui->surface, 24, 24, 64, 64, 64);
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, VideoPostCompositeKeepsPlacementsAndRetiresAbsentStreamsWithoutScanning) {
    rdt_engine_init(0);
    ImageSurface* surface = image_surface_create(32, 32); ASSERT_NE(surface, nullptr);
    RdtVideo* video = rdt_video_create(nullptr, nullptr); ASSERT_NE(video, nullptr);
    DocState state = {};
    DisplayList list;
    Bound clip = {0, 0, 32, 32};
    dl_video_placeholder(&list, video, 0, 0, 32, 32, 0, &clip, 1);
    ValidatedDisplayList recording;
    ASSERT_TRUE(dl_validate_recording(&list, list.item_count(), &recording, "test video"));
    EXPECT_TRUE(recording.content().has_video);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    doc_state_mark_video_frame_pending(&state);
    render_video_frames(recording.get(), surface, &state, nullptr);
    EXPECT_EQ(state.video_placement_count, 1); EXPECT_FALSE(state.video_frame_pending);
    EXPECT_EQ(counters.media_command_checks, 1u);
    dl_clear(&list); EXPECT_EQ(recording.get(), nullptr);
    doc_state_mark_video_frame_pending(&state); counters = {};
    render_video_frames(nullptr, surface, &state, nullptr);
    EXPECT_EQ(state.video_placement_count, 0); EXPECT_FALSE(state.video_frame_pending);
    EXPECT_EQ(counters.media_command_checks, 0u);
    dl_destroy(&list); rdt_video_destroy(video); image_surface_destroy(surface); rdt_engine_term();
}

TEST(PaintOwnership, ReplayMetadataRecognizesNativeTextPictures) {
    rdt_engine_init(0);
    for (bool text : {false, true}) {
        Tvg_Paint paint = text ? tvg_text_new() : tvg_shape_new(); ASSERT_NE(paint, nullptr);
        RdtPicture* picture = rdt_picture_take_tvg_paint(paint, 12, 12); ASSERT_NE(picture, nullptr);
        DisplayList list; dl_draw_picture(&list, picture, 255, nullptr);
        ValidatedDisplayList recording;
        ASSERT_TRUE(dl_validate_recording(&list, list.item_count(), &recording, "test native picture"));
        EXPECT_EQ(recording.content().has_glyphs, text); EXPECT_FALSE(recording.content().has_video);
        EXPECT_EQ(dl_contains_glyphs(&list), text);
        dl_destroy(&list);
    }
    rdt_engine_term();
}

TEST(PaintOwnership, ClipSegmentsDeferNativeDrawingAndPreserveCpuPainterOrder) {
    rdt_engine_init(0);
    uint32_t actual[32 * 32] = {}, expected[32 * 32] = {};
    auto group = [](DisplayList* list, unsigned index) {
        RdtPath* clip = rdt_path_new(); RdtPath* paint = rdt_path_new();
        rdt_path_add_circle(clip, 8 + (float)index * 8, 16, 9, 12);
        rdt_path_add_rect(paint, 0, 0, 32, 32, 0, 0);
        dl_push_clip(list, clip, nullptr);
        Color colors[] = {{0xa00000ffu}, {0xa000ff00u}, {0xa0ff0000u}};
        dl_fill_path(list, paint, colors[index], RDT_FILL_WINDING, nullptr);
        dl_pop_clip(list); rdt_path_free(clip); rdt_path_free(paint);
    };
    auto replay = [](DisplayList* list, uint32_t* pixels) {
        ImageSurface surface = {}; surface.pixels = pixels; surface.width = surface.height = 32;
        surface.pitch = 128; surface.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
        RdtVector vector = {}; rdt_vector_init(&vector, pixels, 32, 32, 32);
        ScratchArena scratch = {}; Bound clip = {0, 0, 32, 32};
        dl_replay(list, &vector, &surface, &clip, &scratch, 1, nullptr);
        rdt_vector_destroy(&vector); scratch_release(&scratch);
    };
    DisplayList list;
    for (unsigned i = 0; i < 3; i++) group(&list, i);
    {
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        replay(&list, actual);
        EXPECT_EQ(counters.clip_segments, 3u); EXPECT_EQ(counters.batch_flushes, 1u);
    }
    for (unsigned i = 0; i < 3; i++) {
        DisplayList separate; group(&separate, i); replay(&separate, expected); dl_destroy(&separate);
    }
    EXPECT_EQ(memcmp(actual, expected, sizeof(actual)), 0); EXPECT_NE(actual[16 * 32 + 16], 0u);
    dl_destroy(&list); memset(actual, 0, sizeof(actual)); memset(expected, 0, sizeof(expected));
    for (unsigned i = 0; i < 3; i++) {
        group(&list, i);
        DisplayList separate; group(&separate, i); replay(&separate, expected); dl_destroy(&separate);
        if (i == 1) {
            dl_fill_surface_rect(&list, 12, 12, 10, 10, 0xffffffffu, nullptr, nullptr, 0);
            DisplayList cpu;
            dl_fill_surface_rect(&cpu, 12, 12, 10, 10, 0xffffffffu, nullptr, nullptr, 0);
            replay(&cpu, expected); dl_destroy(&cpu);
        }
    }
    {
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        replay(&list, actual); EXPECT_EQ(counters.batch_flushes, 2u);
    }
    EXPECT_EQ(memcmp(actual, expected, sizeof(actual)), 0);
    dl_destroy(&list); rdt_engine_term();
}

TEST(PaintOwnership, ReferencedSubtreesMatchFlatReplayAcrossClipsEffectsTilesAndDirtyRegions) {
    rdt_engine_init(0);
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    DisplayList source, flat, referenced;
    int root = dl_begin_element(&source, 8300, 0, 0, 32, 32);
    RdtPath* clip = rdt_path_new(); ASSERT_NE(clip, nullptr);
    rdt_path_add_circle(clip, 16, 16, 14, 12); dl_push_clip(&source, clip, nullptr); rdt_path_free(clip);
    dl_fill_rounded_rect(&source, 1, 1, 30, 30, 5, 5, Color{0xa03020e0u});
    int child = dl_begin_element(&source, 8301, 4, 4, 24, 24);
    dl_save_backdrop(&source, 4, 4, 24, 24);
    Bound bounds = {4, 4, 28, 28};
    dl_fill_surface_rect(&source, 6, 6, 20, 20, 0xffe04030u, &bounds, nullptr, 0);
    dl_composite_opacity(&source, 4, 4, 24, 24, 0.625f);
    dl_end_element(&source, child); dl_pop_clip(&source); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    dl_fill_rect(&flat, 0, 0, 3, 3, Color{0xff000000u});
    ASSERT_TRUE(dl_copy_range(&flat, &source, 0, source.item_count() - 1));
    dl_fill_rect(&flat, 28, 28, 4, 4, Color{0xff00ff00u});
    dl_fill_rect(&referenced, 0, 0, 3, 3, Color{0xff000000u});
    ASSERT_TRUE(retained_dl_append_fragment(&referenced, retained_dl_cache_get(cache, 8300)));
    dl_fill_rect(&referenced, 28, 28, 4, 4, Color{0xff00ff00u});
    ASSERT_EQ(referenced.size(), 3u);
    ASSERT_TRUE(dl_validate(&referenced, nullptr));
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    auto replay = [](DisplayList* list, uint32_t* pixels, unsigned mode) {
        for (size_t i = 0; i < 32 * 32; i++) pixels[i] = 0xffd0c0b0u;
        ImageSurface surface = {}; surface.pixels = pixels; surface.width = surface.height = 32; surface.pitch = 32 * 4;
        Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
        RdtVector vector = {}; rdt_vector_init(&vector, pixels, 32, 32, 32);
        Bound viewport = {0, 0, 32, 32};
        DirtyRect rect = {}; rect.x = rect.y = 8; rect.width = rect.height = 16;
        DirtyTracker dirty = {}; dirty.dirty_list = &rect;
        if (mode == 1) dl_replay_tile(list, &vector, &surface, &scratch, 4, 4, 32, 32, 1);
        else dl_replay(list, &vector, &surface, &viewport, &scratch, 1, mode == 2 ? &dirty : nullptr);
        rdt_vector_destroy(&vector); scratch_release(&scratch); arena_destroy(arena);
    };
    for (unsigned mode = 0; mode < 3; mode++) {
        SCOPED_TRACE(mode);
        uint32_t expected[32 * 32], actual[32 * 32];
        replay(&flat, expected, mode); replay(&referenced, actual, mode);
        EXPECT_EQ(memcmp(actual, expected, sizeof(actual)), 0);
        EXPECT_NE(actual[12 * 32 + 12], 0xffd0c0b0u);
    }
    dl_destroy(&flat); dl_destroy(&referenced); pool_destroy(pool); rdt_engine_term();
}

static void record_translated_vector_test_scene(DisplayList* list, float dx, float dy,
    ImageSurface* image, RdtPicture* picture) {
    int root = dl_begin_element(list, 8400, dx, dy, 32, 32);
    RdtMatrix placement = rdt_matrix_translate(dx, dy);
    RdtMatrix local = {1, 0.125f, 1, -0.125f, 1, 2, 0, 0, 1};
    RdtMatrix transform = rdt_matrix_multiply(&placement, &local);
    RdtMatrix gradient = {1, 0.25f, 1, 0.125f, 0.75f, 2, 0, 0, 1};
    RdtPath* clip = rdt_path_new(); RdtPath* path = rdt_path_new();
    ASSERT_NE(clip, nullptr); ASSERT_NE(path, nullptr);
    const RdtMatrix* simple_placement = dx == 0 && dy == 0 ? nullptr : &placement;
    rdt_path_add_circle(clip, 16, 16, 15, 13); dl_push_clip(list, clip, simple_placement);
    dl_fill_rect(list, dx + 1, dy + 2, 15, 9, Color{0xff2030e0u});
    dl_fill_rounded_rect(list, dx + 6, dy + 5, 23, 24, 3, 4, Color{0x90803020u});
    int child = dl_begin_element(list, 8401, dx + 2, dy + 3, 28, 26);
    rdt_path_add_rect(path, 2, 3, 14, 10, 2, 1);
    dl_fill_path(list, path, Color{0xa0e03020u}, RDT_FILL_WINDING, simple_placement);
    float dashes[] = {3, 2, 1, 2};
    dl_stroke_path(list, path, Color{0xff20a030u}, 2, RDT_CAP_ROUND, RDT_JOIN_ROUND,
        dashes, 4, 0.5f, &transform);
    RdtGradientStop stops[] = {{0, 255, 0, 0, 255}, {1, 0, 128, 255, 192}};
    dl_fill_linear_gradient(list, path, 2, 3, 16, 13, stops, 2, RDT_FILL_EVEN_ODD,
        &transform, &gradient);
    rdt_path_free(path); path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_circle(path, 21, 20, 8, 9);
    dl_fill_radial_gradient(list, path, 21, 20, 9, stops, 2, RDT_FILL_WINDING,
        &transform, &gradient);
    dl_draw_image(list, (const uint32_t*)image->pixels, 2, 2, image->pitch / 4,
        3, 18, 9, 10, 200, &transform, image, image->generation);
    dl_draw_picture(list, rdt_picture_dup(picture), 190, &transform);
    dl_end_element(list, child); dl_pop_clip(list); dl_end_element(list, root);
    rdt_path_free(path); rdt_path_free(clip);
}

TEST(PaintOwnership, TranslatedVectorFragmentsMatchIndependentPlacementsAfterSourceRetirement) {
    rdt_engine_init(0);
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    ImageSurface* image = image_surface_create(2, 2); ASSERT_NE(image, nullptr);
    uint32_t colors[] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xff00ffffu};
    memcpy(image->pixels, colors, sizeof(colors)); image_surface_snapshot_publish(image);
    Tvg_Paint shape = tvg_shape_new(); ASSERT_NE(shape, nullptr);
    ASSERT_EQ(tvg_shape_append_rect(shape, 18, 4, 8, 7, 1, 1, true), TVG_RESULT_SUCCESS);
    ASSERT_EQ(tvg_shape_set_fill_color(shape, 230, 40, 10, 210), TVG_RESULT_SUCCESS);
    RdtPicture* picture = rdt_picture_take_tvg_paint(shape, 32, 32); ASSERT_NE(picture, nullptr);
    DisplayList source, flat, placed, canonical;
    ASSERT_NO_FATAL_FAILURE(record_translated_vector_test_scene(&source, 0, 0, image, picture));
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const auto* fragment = retained_dl_cache_get(cache, 8400); ASSERT_NE(fragment, nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&canonical, fragment));
    // the caller's clip remains in world coordinates while each referenced local clip moves.
    RdtPath* outer = rdt_path_new(); ASSERT_NE(outer, nullptr);
    rdt_path_add_rect(outer, 8, 6, 58, 60, 0, 0);
    dl_push_clip(&flat, outer, nullptr); dl_push_clip(&placed, outer, nullptr); rdt_path_free(outer);
    const float placements[][2] = {{4, 3}, {35.5f, 8.25f}, {15, 38}};
    for (const auto& offset : placements)
        ASSERT_NO_FATAL_FAILURE(record_translated_vector_test_scene(&flat, offset[0], offset[1], image, picture));
    {
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        for (const auto& offset : placements)
            ASSERT_TRUE(retained_dl_append_fragment_translated(&placed, fragment, offset[0], offset[1]));
        EXPECT_EQ(counters.command_copy_bytes, 0u); EXPECT_EQ(counters.payload_copy_bytes, 0u);
        EXPECT_EQ(counters.path_clones, 0u); EXPECT_GT(counters.placement_command_copy_bytes, 0u);
    }
    dl_pop_clip(&flat); dl_pop_clip(&placed);
    EXPECT_EQ(placed.size(), 5u); EXPECT_TRUE(dl_validate(&placed, nullptr));
    DisplayListCursor original(&canonical), positioned(&placed);
    ASSERT_NE(positioned.next(), nullptr); // enclosing world clip precedes the placement.
    while (const DisplayItem* before = original.next()) {
        const DisplayItem* after = positioned.next(); ASSERT_NE(after, nullptr);
        EXPECT_EQ(before->op, after->op);
        switch (before->op) {
            case DL_FILL_PATH: EXPECT_EQ(before->fill_path.path.get(), after->fill_path.path.get()); break;
            case DL_STROKE_PATH:
                EXPECT_EQ(before->stroke_path.path.get(), after->stroke_path.path.get());
                EXPECT_EQ(before->stroke_path.dash_array.get(), after->stroke_path.dash_array.get()); break;
            case DL_FILL_LINEAR_GRADIENT:
                EXPECT_EQ(before->fill_linear_gradient.stops.get(), after->fill_linear_gradient.stops.get()); break;
            case DL_FILL_RADIAL_GRADIENT:
                EXPECT_EQ(before->fill_radial_gradient.stops.get(), after->fill_radial_gradient.stops.get()); break;
            case DL_DRAW_IMAGE: EXPECT_EQ(before->draw_image.snapshot_lease.get(), after->draw_image.snapshot_lease.get()); break;
            case DL_DRAW_PICTURE: EXPECT_EQ(before->draw_picture.picture.get(), after->draw_picture.picture.get()); break;
            default: break;
        }
    }
    dl_destroy(&canonical);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    image_surface_snapshot_release(image); rdt_picture_free(picture);
    auto replay = [](DisplayList* list, uint32_t* pixels, unsigned mode) {
        for (size_t i = 0; i < 80 * 80; i++) pixels[i] = 0xffd0c0b0u;
        ImageSurface surface = {}; surface.pixels = pixels; surface.width = surface.height = 80; surface.pitch = 80 * 4;
        DirtyRect rect = {}; rect.x = 10; rect.y = 12; rect.width = 44; rect.height = 40;
        test_radiant_replay_surface(list, &surface, mode, &rect, 5, 7);
    };
    for (unsigned mode = 0; mode < 3; mode++) for (unsigned repeat = 0; repeat < 2; repeat++) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(repeat);
        uint32_t expected[80 * 80], actual[80 * 80];
        replay(&flat, expected, mode); replay(&placed, actual, mode);
        EXPECT_EQ(memcmp(actual, expected, sizeof(actual)), 0);
        size_t painted = 0;
        for (uint32_t pixel : actual) painted += pixel != 0xffd0c0b0u;
        EXPECT_GT(painted, 100u);
    }
    dl_destroy(&flat); dl_destroy(&placed); pool_destroy(pool); rdt_engine_term();
}

class SvgLayerScrollTest : public ::testing::Test {
protected:
    RenderExportSession session = {};
    UiContext* ui = nullptr;
    DomElement* svg = nullptr;
    RenderPipelineMetrics counters;
    size_t layer_images = 0;

    void SetUp() override { rdt_engine_init(0); }
    void TearDown() override { render_export_session_end(&session); rdt_engine_term(); }

    void open(const char* source) {
        ASSERT_TRUE(render_export_session_begin_source(&session, source, 160, 120, false));
        ui = session.ui_context;
        svg = dom_find_element_by_id(ui->document->root, "scene");
        ASSERT_NE(svg, nullptr);
    }

    void paint(float x = 0, float y = 0, const RdtMatrix* matrix = nullptr, const Bound* clip = nullptr) {
        counters = {};
        RenderPipelineMetricsScope probe(&counters);
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope frame(&context, ui, ui->document->view_tree, &profiler);
        memset(ui->surface->pixels, 255, ui->surface->pitch * ui->surface->height);
        context.transform = matrix ? *matrix : rdt_matrix_translate(x, y);
        context.has_transform = matrix || x != 0 || y != 0;
        if (clip) context.block.clip = *clip;
        render_inline_svg(&context, lam::view_require_block(svg));
        PaintRecordTarget target = rc_record_target(&context); paint_record_lower_pending(&target);
        ASSERT_TRUE(dl_validate(frame.list(), nullptr));
        layer_images = 0;
        DisplayListCursor cursor(frame.list());
        while (const DisplayItem* item = cursor.next())
            if (item->op == DL_DRAW_IMAGE) layer_images++;
        dl_replay(frame.list(), &context.vec, ui->surface, &context.block.clip, &context.scratch, 1, nullptr);
    }

    void warm() {
        ASSERT_NO_FATAL_FAILURE(paint());
        ASSERT_NO_FATAL_FAILURE(paint());
        ASSERT_EQ(counters.layer_recordings, 1u);
        ASSERT_NO_FATAL_FAILURE(paint());
        ASSERT_EQ(counters.layer_recordings, 0u);
    }

    void scroll(const char* id, float y) {
        DomElement* element = dom_find_element_by_id(ui->document->root, id);
        ASSERT_NE(element, nullptr);
        ViewBlock* block = lam::view_require_block(element);
        ASSERT_NE(block->scroller, nullptr);
        ASSERT_NE(block->scroller->pane, nullptr);
        ASSERT_GE(block->scroller->pane->v_max_scroll, y);
        scroll_state_set_position_for_view(ui->document->state, block, block->scroller->pane, 0, y, false);
    }
};

TEST_F(SvgLayerScrollTest, UnrelatedScrollingKeepsTheRasterAndMovesItsPlacement) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='40' style='display:block'>"
        "<rect width='40' height='30' fill='red'/></svg>"
        "<div id='scroll' style='height:20px;overflow:auto'><div style='height:200px'></div></div>"
        "</body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    for (unsigned step = 1; step <= 3; step++) {
        ASSERT_NO_FATAL_FAILURE(scroll("scroll", (float)step * 10));
        ASSERT_NO_FATAL_FAILURE(paint(50, 50));
        EXPECT_EQ(counters.layer_recordings, 0u);
        EXPECT_EQ(counters.layer_passes, 0u);
        EXPECT_EQ(layer_images, 1u);
        test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 255, 255);
        test_radiant_expect_surface_pixel(ui->surface, 60, 60, 255, 0, 0);
    }
}

TEST_F(SvgLayerScrollTest, HoverStillInvalidatesAfterScrollIndependentReuse) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><head><style>#ink:hover{fill:blue}</style></head><body style='margin:0'>"
        "<svg id='scene' width='60' height='40' style='display:block'>"
        "<rect id='ink' width='40' height='30' fill='red'/></svg>"
        "<div id='scroll' style='height:20px;overflow:auto'><div style='height:200px'></div></div>"
        "</body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 10));
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 0u);
    EXPECT_EQ(layer_images, 1u);
    DomElement* ink = dom_find_element_by_id(ui->document->root, "ink"); ASSERT_NE(ink, nullptr);
    doc_state_set_hover_target(ui->document->state, ink);
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(layer_images, 0u);
    radiant_recascade_document(ui->document);
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 0u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
}

TEST_F(SvgLayerScrollTest, ViewportSynchronizationPreservesSelectorStateAndTheRaster) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='40' style='display:block'>"
        "<rect width='40' height='30' fill='red'/></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    DocState* state = ui->document->state;
    uint64_t selectors = doc_state_selector_version(state);
    for (unsigned step = 1; step <= 3; step++) {
        uint64_t content = doc_state_content_version(state);
        float offset = (float)step * 10;
        doc_state_sync_viewport_scroll(state, ui->document, 0, offset);
        EXPECT_EQ(doc_state_content_version(state), content + 1);
        EXPECT_EQ(doc_state_selector_version(state), selectors);
        doc_state_sync_viewport_scroll(state, ui->document, 0, offset);
        EXPECT_EQ(doc_state_content_version(state), content + 1);
        EXPECT_EQ(doc_state_selector_version(state), selectors);
        ASSERT_NO_FATAL_FAILURE(paint(50, 50));
        EXPECT_EQ(layer_images, 1u);
        EXPECT_EQ(counters.layer_recordings, 0u);
        test_radiant_expect_surface_pixel(ui->surface, 60, 60, 255, 0, 0);
    }
}

TEST_F(SvgLayerScrollTest, ForeignHtmlScrollingChangesPixelsAndRestartsCapture) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='40' style='display:block'>"
        "<foreignObject width='60' height='40'><div xmlns='http://www.w3.org/1999/xhtml' id='scroll' "
        "style='width:60px;height:40px;overflow:auto'><div style='height:40px;background:red'></div>"
        "<div style='height:80px;background:blue'></div></div></foreignObject></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 40));
    ASSERT_NO_FATAL_FAILURE(paint());
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
}

TEST_F(SvgLayerScrollTest, ForeignHtmlOutsideTheRootKeepsConservativeScrollInvalidation) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='40' style='display:block'>"
        "<rect width='40' height='30' fill='red'/></svg><svg width='60' height='40'>"
        "<foreignObject width='60' height='40'><div xmlns='http://www.w3.org/1999/xhtml' id='scroll' "
        "style='width:60px;height:40px;overflow:auto'><div style='height:200px'></div></div>"
        "</foreignObject></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 10));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
}

TEST_F(SvgLayerScrollTest, AddingAndRemovingForeignHtmlRefreshesTheScrollDependency) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='40' style='display:block'>"
        "<rect width='40' height='30' fill='red'/></svg><svg id='other' width='60' height='40'></svg>"
        "<div id='scroll' style='height:20px;overflow:auto'><div style='height:200px'></div></div>"
        "</body></html>"));
    ASSERT_NO_FATAL_FAILURE(warm());
    DomElement* other = dom_find_element_by_id(ui->document->root, "other"); ASSERT_NE(other, nullptr);
    MarkBuilder builder(ui->document->input);
    Element* source = builder.element("foreignObject").final().element;
    DomElement* foreign = DomElement::create(ui->document, "foreignObject", source); ASSERT_NE(foreign, nullptr);
    ASSERT_TRUE(other->append_child(foreign));
    ASSERT_TRUE(dom_element_is_svg(foreign));
    dom_notify_mutation_detail(DOM_JS_MUTATION_CHILD_INSERT, foreign, other, nullptr, nullptr);
    ASSERT_NO_FATAL_FAILURE(warm());
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 10));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 1u);
    ASSERT_TRUE(other->remove_child(foreign));
    dom_notify_mutation_detail(DOM_JS_MUTATION_CHILD_REMOVE, foreign, other, nullptr, nullptr);
    ASSERT_NO_FATAL_FAILURE(warm());
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 20));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint());
    EXPECT_EQ(counters.layer_recordings, 0u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
}

class SvgVectorCacheTest : public SvgLayerScrollTest {
    char* previous_mode = nullptr;
protected:
    void SetUp() override {
        SvgLayerScrollTest::SetUp();
        const char* mode = getenv("RADIANT_SVG_VECTOR_CACHE");
        if (mode) previous_mode = mem_strdup(mode, MEM_CAT_RENDER);
        setenv("RADIANT_SVG_VECTOR_CACHE", "on", 1);
    }
    void TearDown() override {
        SvgLayerScrollTest::TearDown();
        if (previous_mode) setenv("RADIANT_SVG_VECTOR_CACHE", previous_mode, 1);
        else unsetenv("RADIANT_SVG_VECTOR_CACHE");
        mem_free(previous_mode);
    }
    void expect_uncached_parity(float x = 0, float y = 0,
        const RdtMatrix* matrix = nullptr, const Bound* clip = nullptr) {
        uint32_t expected[160 * 120];
        ASSERT_EQ((size_t)ui->surface->pitch * (size_t)ui->surface->height, sizeof(expected));
        memcpy(expected, ui->surface->pixels, sizeof(expected));
        setenv("RADIANT_SVG_VECTOR_CACHE", "off", 1);
        ASSERT_NO_FATAL_FAILURE(paint(x, y, matrix, clip));
        EXPECT_EQ(counters.svg_vector_hits, 0u);
        EXPECT_EQ(memcmp(expected, ui->surface->pixels, sizeof(expected)), 0);
        setenv("RADIANT_SVG_VECTOR_CACHE", "on", 1);
    }
};

TEST_F(SvgVectorCacheTest, ReusesMovedOverflowInkWithTheCurrentCallerClip) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='20' height='20' "
        "style='display:block;overflow:visible'><rect x='15' width='30' height='20' fill='red'/></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 35, 10, 255, 0, 0);
    ASSERT_NO_FATAL_FAILURE(paint(40.25f, 30.5f));
    EXPECT_EQ(counters.svg_vector_recordings, 0u); EXPECT_EQ(counters.svg_vector_hits, 1u);
    EXPECT_EQ(counters.command_copy_bytes, 0u); EXPECT_EQ(counters.payload_copy_bytes, 0u);
    test_radiant_expect_surface_pixel(ui->surface, 75, 40, 255, 0, 0);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(40.25f, 30.5f));
    Bound clip = {0, 0, 65, 120};
    ASSERT_NO_FATAL_FAILURE(paint(40, 30, nullptr, &clip)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 60, 40, 255, 0, 0);
    test_radiant_expect_surface_pixel(ui->surface, 70, 40, 255, 255, 255);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(40, 30, nullptr, &clip));
}

TEST_F(SvgVectorCacheTest, LocalAndCrossRootResourceMutationsRebuildVectorCommands) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg width='0' height='0' style='display:block'><defs><linearGradient id='ramp'>"
        "<stop id='stop' stop-color='red'/></linearGradient></defs></svg>"
        "<svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<rect id='ink' width='40' height='30' fill='url(#ramp)'/></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
    DomElement* stop = dom_find_element_by_id(ui->document->root, "stop"); ASSERT_NE(stop, nullptr);
    ASSERT_TRUE(stop->set_attribute("stop-color", "blue"));
    dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, stop, stop->parent, "stop-color", "red");
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 0, 0, 255);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    DomElement* ink = dom_find_element_by_id(ui->document->root, "ink"); ASSERT_NE(ink, nullptr);
    ASSERT_TRUE(ink->set_attribute("fill", "red"));
    dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, ink, ink->parent, "fill", "url(#ramp)");
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 0u);
    test_radiant_expect_surface_pixel(ui->surface, 10, 10, 255, 0, 0);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity());
}

TEST_F(SvgVectorCacheTest, ScrollKeepsVectorPaintButSelectorStateRebuildsIt) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><head><style>#ink:hover{fill:blue}</style></head><body style='margin:0'>"
        "<svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<rect id='ink' width='40' height='30' fill='red'/></svg>"
        "<div id='scroll' style='height:20px;overflow:auto'><div style='height:200px'></div></div></body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(scroll("scroll", 10));
    ASSERT_NO_FATAL_FAILURE(paint(50, 50)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    DomElement* ink = dom_find_element_by_id(ui->document->root, "ink"); ASSERT_NE(ink, nullptr);
    doc_state_set_hover_target(ui->document->state, ink); radiant_recascade_document(ui->document);
    ASSERT_NO_FATAL_FAILURE(paint(50, 50)); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 60, 60, 0, 0, 255);
    ASSERT_NO_FATAL_FAILURE(paint(50, 50)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(50, 50));
}

TEST_F(SvgVectorCacheTest, LinearTransformAndUsedViewportChangesRebuildVectorPaint) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='40' height='30' "
        "style='display:block;overflow:visible'><rect width='100%' height='100%' fill='red'/></svg></body></html>"));
    RdtMatrix matrix = {1, .25f, 10, .125f, 1, 5, 0, 0, 1};
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix));
    matrix.e13 += 15; matrix.e23 += 10;
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    matrix.e11 = 2;
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix)); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    lam::view_require_block(svg)->width += 10;
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix)); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint(0, 0, &matrix)); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(0, 0, &matrix));
}

TEST_F(SvgVectorCacheTest, RasterizedEffectsKeepDirectRecording) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<g opacity='.5'><rect width='40' height='30' fill='red'/><rect x='10' width='20' height='30' fill='blue'/></g>"
        "</svg></body></html>"));
    for (unsigned step = 0; step < 3; step++) {
        ASSERT_NO_FATAL_FAILURE(paint((float)step * 10, 0));
        EXPECT_EQ(counters.svg_vector_recordings, 1u); EXPECT_EQ(counters.svg_vector_hits, 0u);
    }
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(20, 0));
}

TEST_F(SvgVectorCacheTest, HostForeignHtmlKeepsConservativeRecording) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<rect width='40' height='30' fill='red'/></svg><svg width='0' height='0'>"
        "<foreignObject width='10' height='10'><div xmlns='http://www.w3.org/1999/xhtml'>x</div></foreignObject></svg>"
        "</body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint(20, 20));
    EXPECT_EQ(counters.svg_vector_recordings, 1u); EXPECT_EQ(counters.svg_vector_hits, 0u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(20, 20));
}

TEST_F(SvgVectorCacheTest, PublishedFrameOutlivesVectorRegistryAndDocument) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<rect x='15' width='40' height='30' fill='red'/></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint(20, 20));
    ASSERT_EQ(counters.svg_vector_hits, 1u);
    DisplayList pinned;
    {
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope frame(&context, ui, ui->document->view_tree, &profiler);
        context.has_transform = true; context.transform = rdt_matrix_translate(20, 20);
        render_inline_svg(&context, lam::view_require_block(svg));
        PaintRecordTarget target = rc_record_target(&context); paint_record_lower_pending(&target);
        ASSERT_TRUE(dl_copy_range(&pinned, frame.list(), 0, frame.list()->item_count() - 1));
    }
    render_export_session_end(&session); ui = nullptr; svg = nullptr;
    ASSERT_TRUE(dl_validate(&pinned, nullptr));
    ImageSurface* surface = image_surface_create(160, 120); ASSERT_NE(surface, nullptr);
    memset(surface->pixels, 255, surface->pitch * surface->height);
    RdtVector vector = {}; rdt_vector_init(&vector, (uint32_t*)surface->pixels, 160, 120, surface->pitch / 4);
    ScratchArena scratch = {}; Bound viewport = {0, 0, 160, 120};
    dl_replay(&pinned, &vector, surface, &viewport, &scratch, 1, nullptr);
    test_radiant_expect_surface_pixel(surface, 60, 30, 255, 0, 0);
    rdt_vector_destroy(&vector); scratch_release(&scratch); image_surface_destroy(surface); dl_destroy(&pinned);
}

TEST_F(SvgVectorCacheTest, HtmlCaptureSharesWarmSvgCommandsAcrossBothCacheTeardowns) {
    for (float placement : {0.0f, 13.25f}) {
        ASSERT_NO_FATAL_FAILURE(open(
            "<html><body style='margin:0'><svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
            "<defs><linearGradient id='g'><stop stop-color='blue'/><stop offset='1' stop-color='green'/></linearGradient></defs>"
            "<rect x='15' width='90' height='30' fill='red'/><svg x='5' y='40' width='55' height='20' overflow='hidden'>"
            "<path d='M0 0h90v20H0z' fill='url(#g)'/></svg>"
            "</svg></body></html>"));
        ASSERT_NO_FATAL_FAILURE(paint());
        DisplayList assembled;
        uint32_t expected[3][160 * 120];
        DirtyRect dirty = {}; dirty.x = 10; dirty.y = 12; dirty.width = 44; dirty.height = 40;
        Bound clip = {0, 0, 70, 120};
        Pool* owner_pool = pool_create(); ASSERT_NE(owner_pool, nullptr);
        auto* consumer = retained_dl_cache_create(owner_pool); ASSERT_NE(consumer, nullptr);
        {
            RenderProfiler profiler = {}; RasterRenderContext context;
            RenderFrameScope frame(&context, ui, ui->document->view_tree, &profiler);
            int begin = dl_begin_element(frame.list(), 49010, 0, 0, 160, 120);
            context.block.clip = clip;
            context.has_transform = placement != 0;
            context.transform = rdt_matrix_translate(placement, placement * 0.5f);
            render_inline_svg(&context, lam::view_require_block(svg));
            PaintRecordTarget target = rc_record_target(&context); paint_record_lower_pending(&target);
            dl_end_element(frame.list(), begin);
            for (unsigned mode = 0; mode < 3; mode++) {
                memset(ui->surface->pixels, 255, sizeof(expected[mode]));
                test_radiant_replay_surface(frame.list(), ui->surface, mode, &dirty, 5, 7);
                if (mode == 0) {
                    test_radiant_expect_surface_pixel(ui->surface, 60, 10, 255, 0, 0);
                    test_radiant_expect_surface_pixel(ui->surface, 80, 10, 255, 255, 255);
                    if (!placement) test_radiant_expect_surface_pixel(ui->surface, 65, 50, 255, 255, 255);
                }
                memcpy(expected[mode], ui->surface->pixels, sizeof(expected[mode]));
            }
            counters = {}; RenderPipelineMetricsScope probe(&counters);
            DisplayResourceSummary* resources = nullptr;
            ASSERT_TRUE(retained_dl_cache_capture_resources(consumer, frame.list(), frame.list()->item_count(), &resources, &assembled));
            EXPECT_EQ(counters.retained_foreign_chunks, 1u);
            EXPECT_TRUE(dl_resources_current(&assembled, 0, 0));
            dl_resource_summary_destroy(resources);
        }
        memcpy(ui->surface->pixels, expected[0], sizeof(expected[0]));
        ASSERT_NO_FATAL_FAILURE(expect_uncached_parity(placement, placement * 0.5f, nullptr, &clip));
        if (placement) { render_export_session_end(&session); ui = nullptr; svg = nullptr; }
        retained_dl_cache_destroy(consumer); pool_destroy(owner_pool);
        if (ui) { render_export_session_end(&session); ui = nullptr; svg = nullptr; }
        ASSERT_TRUE(dl_validate(&assembled, nullptr));
        ImageSurface* surface = image_surface_create(160, 120); ASSERT_NE(surface, nullptr);
        for (unsigned mode = 0; mode < 3; mode++) {
            SCOPED_TRACE(placement);
            SCOPED_TRACE(mode);
            memset(surface->pixels, 255, sizeof(expected[mode]));
            test_radiant_replay_surface(&assembled, surface, mode, &dirty, 5, 7);
            EXPECT_EQ(memcmp(expected[mode], surface->pixels, sizeof(expected[mode])), 0);
        }
        image_surface_destroy(surface); dl_destroy(&assembled);
    }
}

TEST_F(SvgVectorCacheTest, FontAndImageEpochsInvalidateOtherwiseStablePaint) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='40' height='30' style='display:block;overflow:visible'>"
        "<rect width='40' height='30' fill='red'/></svg></body></html>"));
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    font_context_reset_glyph_caches(ui->font_ctx);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ImageSurface* image = image_surface_create(2, 2); ASSERT_NE(image, nullptr);
    ASSERT_EQ(image_cache_adopt(ui, "vector-epoch-test", image), image);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    image_surface_bump_generation(image);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity());
}

TEST_F(SvgVectorCacheTest, AnimationSamplingRebuildsWithoutChangingBaseAttributes) {
    ASSERT_NO_FATAL_FAILURE(open(
        "<html><body style='margin:0'><svg id='scene' width='60' height='30' style='display:block;overflow:visible'>"
        "<rect width='20' height='20' fill='red'><animate attributeName='x' from='0' to='30' dur='2s' fill='freeze'/>"
        "</rect></svg></body></html>"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 0);
    ASSERT_NO_FATAL_FAILURE(paint());
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    svg_animation_set_time(svg, 1);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_recordings, 1u);
    test_radiant_expect_surface_pixel(ui->surface, 5, 10, 255, 255, 255);
    test_radiant_expect_surface_pixel(ui->surface, 25, 10, 255, 0, 0);
    ASSERT_NO_FATAL_FAILURE(paint()); EXPECT_EQ(counters.svg_vector_hits, 1u);
    ASSERT_NO_FATAL_FAILURE(expect_uncached_parity());
}

TEST(PaintOwnership, ForeignHtmlCaptureRestartsPaintingInsideForegroundSvg) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0;background:#808080'><svg id='foreign-svg' style='display:block' width='64' height='48'>"
        "<foreignObject width='64' height='48'><div xmlns='http://www.w3.org/1999/xhtml' "
        "style='width:64px;height:48px;background:red'></div></foreignObject></svg></body></html>",
        80, 60, false));
    UiContext* ui = session.ui_context;
    DomElement* node = dom_find_element_by_id(ui->document->root, "foreign-svg"); ASSERT_NE(node, nullptr);
    {
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope frame(&context, ui, ui->document->view_tree, &profiler);
        context.paint_phase = RADIANT_PAINT_FOREGROUND;
        for (size_t i = 0; i < (size_t)ui->surface->width * ui->surface->height; i++)
            ((uint32_t*)ui->surface->pixels)[i] = 0xff808080u;
        render_inline_svg(&context, lam::view_as_block(node));
        PaintRecordTarget target = rc_record_target(&context); paint_record_lower_pending(&target);
        ASSERT_TRUE(dl_validate(frame.list(), nullptr));
        dl_replay(frame.list(), &context.vec, ui->surface, &context.block.clip, &context.scratch, 1, nullptr);
        EXPECT_EQ(((uint32_t*)ui->surface->pixels)[20 * ui->surface->width + 20], 0xff0000ffu);
    }
    render_export_session_end(&session); rdt_engine_term();
}

TEST(PaintOwnership, SvgPhasesKeepForeignHtmlInsideItsSubscene) {
    rdt_engine_init(0);
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin_source(&session,
        "<html><body style='margin:0'><svg id='scene' width='220' height='190' "
        "style='display:block;background:blue'><g transform='translate(20 20) scale(1.5)'>"
        "<foreignObject x='30' y='10' width='80' height='120'>"
        "<div xmlns='http://www.w3.org/1999/xhtml' style='width:100%;height:100%;background:yellow'>"
        "<div style='width:80px;height:30px;background:red'></div></div>"
        "</foreignObject></g></svg></body></html>", 220, 190, false));
    UiContext* ui = session.ui_context;
    DomElement* node = dom_find_element_by_id(ui->document->root, "scene"); ASSERT_NE(node, nullptr);
    ASSERT_TRUE(radiant_stack_is_context(node));
    ASSERT_TRUE(node->is_block());
    struct PhaseProbe {
        RadiantPaintPhase phase;
        bool parent_stack;
        uint8_t outside[3], inside[3];
    };
    const PhaseProbe probes[] = {
        {RADIANT_PAINT_BLOCK_BACKGROUNDS, false, {0, 0, 255}, {0, 0, 255}},
        {RADIANT_PAINT_FOREGROUND, false, {255, 255, 255}, {255, 0, 0}},
        {RADIANT_PAINT_BLOCK_BACKGROUNDS, true, {255, 255, 255}, {255, 255, 255}},
        {RADIANT_PAINT_FOREGROUND, true, {0, 0, 255}, {255, 0, 0}}
    };
    for (const PhaseProbe& probe : probes) {
        SCOPED_TRACE(static_cast<unsigned>(probe.phase));
        SCOPED_TRACE(probe.parent_stack);
        RenderProfiler profiler = {}; RasterRenderContext context;
        RenderFrameScope frame(&context, ui, ui->document->view_tree, &profiler);
        context.paint_phase = probe.phase;
        for (size_t i = 0; i < (size_t)ui->surface->width * ui->surface->height; i++)
            ((uint32_t*)ui->surface->pixels)[i] = 0xffffffffu;
        // SVG 2 §3.4.1 makes the outer SVG atomic to its parent; its producer still owns partial phases.
        if (probe.parent_stack) render_children(&context, static_cast<View*>(node));
        else if (probe.phase == RADIANT_PAINT_BLOCK_BACKGROUNDS)
            render_block_view(&context, lam::view_require_block(node));
        else render_inline_svg(&context, lam::view_require_block(node));
        PaintRecordTarget target = rc_record_target(&context); paint_record_lower_pending(&target);
        ASSERT_TRUE(dl_validate(frame.list(), nullptr));
        dl_replay(frame.list(), &context.vec, ui->surface, &context.block.clip, &context.scratch, 1, nullptr);
        test_radiant_expect_surface_pixel(ui->surface, 40, 45,
            probe.outside[0], probe.outside[1], probe.outside[2]);
        test_radiant_expect_surface_pixel(ui->surface, 80, 45,
            probe.inside[0], probe.inside[1], probe.inside[2]);
    }
    render_export_session_end(&session); rdt_engine_term();
}

// native recorders keep the dependency fixtures on the same resource ownership path as production.
class RetainedResourceSummaryTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    void SetUp() override { pool = pool_create(); ASSERT_NE(pool, nullptr); }
    void TearDown() override { pool_destroy(pool); }
};

template<class Capture>
static void test_resource_summary_allocation_failures(Capture capture, uint64_t glyph_generation = 0) {
    bool failed = false, succeeded = false;
    for (size_t fail_at = 0; fail_at < 8; fail_at++) {
        size_t before = memtrack_get_current_usage();
        DisplayResourceSummary* snapshot = nullptr;
        memtrack_fault_inject(fail_at);
        bool captured = capture(&snapshot);
        memtrack_fault_clear();
        if (!captured) { failed = true; EXPECT_EQ(snapshot, nullptr); }
        else { succeeded = true; EXPECT_TRUE(dl_resource_summary_current(snapshot, 0, glyph_generation)); }
        dl_resource_summary_destroy(snapshot);
        EXPECT_EQ(memtrack_get_current_usage(), before);
    }
    EXPECT_TRUE(failed); EXPECT_TRUE(succeeded);
}

TEST_F(RetainedResourceSummaryTest, ReplayMetadataIncludesReferencedGlyphAndVideoCommands) {
    uint8_t coverage = 255;
    GlyphBitmap bitmap = {}; bitmap.buffer = &coverage; bitmap.width = bitmap.height = bitmap.pitch = 1;
    RdtVideo* video = rdt_video_create(nullptr, nullptr); ASSERT_NE(video, nullptr);
    DisplayList source; Bound clip = {0, 0, 20, 20};
    int root = dl_begin_element(&source, 34000, 0, 0, 20, 20);
    int child = dl_begin_element(&source, 34001, 0, 0, 20, 20);
    dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 11);
    dl_video_placeholder(&source, video, 0, 0, 2, 2, 0, &clip, 7);
    dl_end_element(&source, child); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_capture(cache, &source);
    EXPECT_EQ(counters.validation_command_checks, source.size()); EXPECT_EQ(counters.validation_chunk_checks, 1u);
    DisplayList frame;
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 34000)));
    dl_destroy(&source); retained_dl_cache_destroy(cache);
    ValidatedDisplayList recording;
    counters = {};
    ASSERT_TRUE(dl_validate_recording(&frame, frame.item_count(), &recording, "test referenced recording"));
    EXPECT_TRUE(recording.content().has_glyphs); EXPECT_TRUE(recording.content().has_video);
    EXPECT_EQ(counters.validation_command_checks, 0u); EXPECT_EQ(counters.validation_chunk_checks, 1u);
    dl_destroy(&frame); rdt_video_destroy(video);
}

TEST_F(RetainedResourceSummaryTest, StandaloneStateFailuresRemainUncertifiedAndUseFullValidation) {
    const DisplayOp operations[][2] = {
        {DL_POP_CLIP, DL_PUSH_CLIP},
        {DL_COMPOSITE_OPACITY, DL_SAVE_BACKDROP},
        {DL_SHADOW_CLIP_RESTORE, DL_SHADOW_CLIP_SAVE}
    };
    for (const auto& pair : operations) {
        DisplayList source;
        const RdtPath* clip_path = nullptr;
        int marker = dl_begin_element(&source, 35100, 0, 0, 3, 3);
        for (DisplayOp op : pair) {
            DisplayItem* item = dl_alloc_item(&source); ASSERT_NE(item, nullptr);
            item->op = op;
            if (op == DL_PUSH_CLIP) {
                item->push_clip.path = lam::own(rdt_path_new());
                ASSERT_NE(item->push_clip.path, nullptr); clip_path = item->push_clip.path;
            }
        }
        dl_end_element(&source, marker);
        RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
        retained_dl_cache_capture(cache, &source);
        DisplayList frame;
        DisplayItem* push = dl_alloc_item(&frame); ASSERT_NE(push, nullptr);
        push->op = pair[1];
        if (push->op == DL_PUSH_CLIP) {
            push->push_clip.path = lam::own(rdt_path_clone(clip_path));
            ASSERT_NE(push->push_clip.path, nullptr);
        }
        ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 35100)));
        EXPECT_EQ(retained_dl_chunk_validation(frame.back().draw_fragment.owner), nullptr);
        DisplayItem* pop = dl_alloc_item(&frame); ASSERT_NE(pop, nullptr); pop->op = pair[0];
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        // caller state makes the complete stream valid, but cannot certify the child alone.
        EXPECT_TRUE(dl_validate(&frame, nullptr));
        EXPECT_EQ(counters.validation_chunk_checks, 0u);
        EXPECT_EQ(counters.validation_command_checks, 6u);
        DisplayResourceSummary* snapshot = nullptr;
        EXPECT_TRUE(dl_resource_summary_capture(&frame, &snapshot));
        EXPECT_EQ(snapshot, nullptr);
        EXPECT_EQ(counters.resource_capture_commands, 6u);
        EXPECT_EQ(counters.resource_capture_chunks, 0u);
        dl_resource_summary_destroy(snapshot);
        dl_truncate(&frame, 2); // removing the final pop leaves caller state unbalanced.
        EXPECT_FALSE(dl_validate(&frame, nullptr));
        dl_destroy(&frame); retained_dl_cache_destroy(cache); dl_destroy(&source);
    }
}

TEST_F(RetainedResourceSummaryTest, ResourceSummaryCoalescesEpochsAndSkipsSurfaceFreeSiblings) {
    ImageSurface* surfaces[] = {image_surface_create(2, 2), image_surface_create(2, 2)};
    ASSERT_NE(surfaces[0], nullptr); ASSERT_NE(surfaces[1], nullptr);
    uint8_t coverage = 255;
    GlyphBitmap bitmap = {}; bitmap.buffer = &coverage; bitmap.width = bitmap.height = bitmap.pitch = 1;
    Bound clip = {0, 0, 20, 20};
    DisplayList source;
    int root = dl_begin_element(&source, 30000, 0, 0, 20, 20);
    for (uint32_t i = 0; i < 512; i++) {
        int child = dl_begin_element(&source, 30001 + i, 0, 0, 20, 20);
        dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 11);
        if (i == 0) dl_video_placeholder(&source, (RdtVideo*)&source, 0, 0, 2, 2, 0, &clip, 7);
        if (i == 1) {
            for (ImageSurface* surface : surfaces)
                dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
                    surface, surface->generation);
            dl_blit_surface_scaled(&source, surfaces[0], 0, 0, 2, 2, SCALE_MODE_LINEAR, &clip,
                nullptr, 0, 255, surfaces[0]->generation);
            dl_webview_layer_placeholder(&source, surfaces[0], 0, 0, 2, 2, &clip, surfaces[0]->generation);
        }
        dl_end_element(&source, child);
    }
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const auto* fragment = retained_dl_cache_get(cache, 30000); ASSERT_NE(fragment, nullptr);
    DisplayList frame;
    ASSERT_TRUE(retained_dl_append_fragment(&frame, fragment));
    ASSERT_TRUE(dl_validate(&frame, nullptr));
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    EXPECT_TRUE(dl_resources_current(&frame, 7, 11));
    EXPECT_EQ(counters.resource_command_checks, 0u); EXPECT_EQ(counters.resource_summary_checks, 1u);
    EXPECT_EQ(counters.resource_epoch_checks, 2u); EXPECT_EQ(counters.resource_child_checks, 1u);
    EXPECT_EQ(counters.resource_surface_checks, 2u);
    DisplayResourceSummary* snapshot = nullptr;
    counters = {};
    ASSERT_TRUE(dl_resource_summary_capture(&frame, &snapshot)); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(counters.resource_capture_commands, 0u);
    EXPECT_EQ(counters.resource_capture_chunks, 2u);
    EXPECT_GT(dl_resource_summary_bytes(snapshot), 0u);
    counters = {};
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 7, 11));
    EXPECT_EQ(counters.resource_command_checks, 0u); EXPECT_EQ(counters.resource_summary_checks, 1u);
    EXPECT_EQ(counters.resource_capture_commands, 0u);
    EXPECT_EQ(counters.resource_epoch_checks, 2u); EXPECT_EQ(counters.resource_child_checks, 0u);
    EXPECT_EQ(counters.resource_surface_checks, 2u);
    EXPECT_TRUE(dl_resources_current(&source, 7, 11));
    const struct { uint64_t video, glyph; } invalid_epochs[] = {{7, 12}, {8, 11}};
    for (const auto& epochs : invalid_epochs) {
        EXPECT_FALSE(dl_resources_current(&frame, epochs.video, epochs.glyph));
        EXPECT_FALSE(dl_resources_current(&source, epochs.video, epochs.glyph));
        EXPECT_FALSE(dl_resource_summary_current(snapshot, epochs.video, epochs.glyph));
    }
    dl_destroy(&source); retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 7, 11));
    image_surface_bump_generation(surfaces[0]);
    EXPECT_FALSE(dl_resources_current(&frame, 7, 11));
    EXPECT_FALSE(dl_resource_summary_current(snapshot, 7, 11));
    image_surface_destroy(surfaces[0]); image_surface_destroy(surfaces[1]);
    EXPECT_FALSE(dl_resources_current(&frame, 7, 11));
    dl_destroy(&frame);
    EXPECT_FALSE(dl_resource_summary_current(snapshot, 7, 11));
    dl_resource_summary_destroy(snapshot);
}

TEST_F(RetainedResourceSummaryTest, MixedSnapshotOutlivesPublicationsWithoutLosingConflictsOrFreshResources) {
    ImageSurface* surface = image_surface_create(2, 2); ASSERT_NE(surface, nullptr);
    uint8_t coverage = 255;
    GlyphBitmap bitmap = {}; bitmap.buffer = &coverage; bitmap.width = bitmap.height = bitmap.pitch = 1;
    Bound clip = {0, 0, 20, 20};
    DisplayList source;
    int root = dl_begin_element(&source, 35000, 0, 0, 20, 20);
    dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 11);
    dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
        surface, surface->generation);
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList frame;
    dl_video_placeholder(&frame, (RdtVideo*)&source, 0, 0, 2, 2, 0, &clip, 7);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 35000)));
    DisplayResourceSummary* snapshot = nullptr;
    ASSERT_TRUE(dl_resource_summary_capture(&frame, &snapshot)); ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 7, 11));
    // fresh dependencies after a composed chunk cannot be hidden by the retained metadata.
    dl_draw_glyph(&frame, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 12);
    DisplayResourceSummary* conflict = nullptr;
    ASSERT_TRUE(dl_resource_summary_capture(&frame, &conflict)); ASSERT_NE(conflict, nullptr);
    EXPECT_FALSE(dl_resource_summary_current(conflict, 7, 11));
    EXPECT_FALSE(dl_resource_summary_current(conflict, 7, 12));
    dl_resource_summary_destroy(conflict); dl_truncate(&frame, 2);
    dl_draw_image(&frame, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
        surface, surface->generation + 1);
    ASSERT_TRUE(dl_resource_summary_capture(&frame, &conflict)); ASSERT_NE(conflict, nullptr);
    EXPECT_FALSE(dl_resource_summary_current(conflict, 7, 11));
    dl_resource_summary_destroy(conflict);
    dl_destroy(&frame); dl_destroy(&source); retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 7, 11));
    image_surface_bump_generation(surface);
    EXPECT_FALSE(dl_resource_summary_current(snapshot, 7, 11));
    image_surface_destroy(surface);
    EXPECT_FALSE(dl_resource_summary_current(snapshot, 7, 11));
    dl_resource_summary_destroy(snapshot);
}

TEST_F(RetainedResourceSummaryTest, CaptureSnapshotsExcludeOverlaysAndAllowFlatFallbackAfterChunkRejection) {
    ImageSurface* surface = image_surface_create(2, 2); ASSERT_NE(surface, nullptr);
    uint8_t coverage = 255;
    GlyphBitmap bitmap = {}; bitmap.buffer = &coverage; bitmap.width = bitmap.height = bitmap.pitch = 1;
    Bound clip = {0, 0, 20, 20};
    DisplayList source;
    int root = dl_begin_element(&source, 37000, 0, 0, 20, 20);
    dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 11);
    dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
        surface, surface->generation);
    dl_end_element(&source, root);
    int document_commands = source.item_count();
    int overlay = dl_begin_element(&source, 37001, 0, 0, 20, 20);
    dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 12);
    dl_end_element(&source, overlay);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayResourceSummary* snapshot = nullptr;
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    ASSERT_TRUE(retained_dl_cache_capture_resources(cache, &source, document_commands, &snapshot));
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(counters.resource_capture_commands, 0u); EXPECT_EQ(counters.resource_capture_chunks, 1u);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 0, 11));
    EXPECT_NE(retained_dl_cache_get(cache, 37001), nullptr);
    DisplayList referenced;
    ASSERT_TRUE(retained_dl_append_fragment(&referenced, retained_dl_cache_get(cache, 37000)));
    test_resource_summary_allocation_failures([&](DisplayResourceSummary** trial) {
        return retained_dl_cache_capture_resources(cache, &referenced, referenced.item_count(), trial);
    }, 11);
    dl_destroy(&referenced);
    DisplayResourceSummary* fallback = nullptr;
    EXPECT_FALSE(retained_dl_cache_capture_resources(cache, &source, document_commands - 1, &fallback));
    EXPECT_EQ(fallback, nullptr);
    retained_dl_cache_set_storage_budget(cache, 0); retained_dl_cache_begin_frame(cache);
    EXPECT_FALSE(retained_dl_cache_capture_resources(cache, &source, document_commands, &fallback));
    EXPECT_EQ(fallback, nullptr);
    dl_truncate(&source, document_commands);
    ASSERT_TRUE(dl_resource_summary_capture(&source, &fallback));
    EXPECT_TRUE(dl_resource_summary_current(fallback, 0, 11));
    dl_destroy(&source); retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 0, 11));
    EXPECT_TRUE(dl_resource_summary_current(fallback, 0, 11));
    dl_resource_summary_destroy(snapshot); dl_resource_summary_destroy(fallback); image_surface_destroy(surface);
}

TEST_F(RetainedResourceSummaryTest, ConflictingEpochsInvalidateAncestorsWithoutInvalidatingSiblings) {
    uint8_t coverage = 255;
    GlyphBitmap bitmap = {}; bitmap.buffer = &coverage; bitmap.width = bitmap.height = bitmap.pitch = 1;
    Bound clip = {0, 0, 20, 20};
    for (bool video : {false, true}) {
        DisplayList source;
        int root = dl_begin_element(&source, 31000, 0, 0, 20, 20);
        for (uint32_t i = 0; i < 2; i++) {
            int child = dl_begin_element(&source, 31001 + i, 0, 0, 20, 20);
            if (video) dl_video_placeholder(&source, (RdtVideo*)&source, 0, 0, 2, 2, 0, &clip, 11 + i);
            else dl_draw_glyph(&source, &bitmap, 0, 0, Color{0xff000000u}, false, &clip, nullptr, 11 + i);
            dl_end_element(&source, child);
        }
        dl_end_element(&source, root);
        RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
        retained_dl_cache_capture(cache, &source);
        const auto* parent = retained_dl_cache_get(cache, 31000); ASSERT_NE(parent, nullptr);
        DisplayList frame;
        ASSERT_TRUE(retained_dl_append_fragment(&frame, parent));
        DisplayResourceSummary* snapshot = nullptr;
        ASSERT_TRUE(dl_resource_summary_capture(&frame, &snapshot)); ASSERT_NE(snapshot, nullptr);
        for (uint64_t epoch : {uint64_t(11), uint64_t(12)}) {
            EXPECT_FALSE(dl_resource_summary_current(snapshot, epoch, epoch));
            EXPECT_FALSE(retained_dl_fragment_resources_valid(parent, epoch, epoch));
            EXPECT_FALSE(dl_resources_current(&frame, epoch, epoch));
            EXPECT_FALSE(dl_resources_current(&source, epoch, epoch));
            EXPECT_TRUE(retained_dl_fragment_resources_valid(retained_dl_cache_get(cache, 31001 + (uint32_t)(epoch - 11)), epoch, epoch));
        }
        dl_resource_summary_destroy(snapshot);
        dl_destroy(&frame); retained_dl_cache_destroy(cache); dl_destroy(&source);
    }
}

TEST_F(RetainedResourceSummaryTest, DependencyAllocationFailureCannotPublishOrLeakBudgetCharges) {
    ImageSurface* surface = image_surface_create(2, 2); ASSERT_NE(surface, nullptr);
    DisplayList source;
    int root = dl_begin_element(&source, 32000, 0, 0, 20, 20);
    dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
        surface, surface->generation);
    dl_end_element(&source, root);
    bool failed = false, succeeded = false;
    for (size_t fail_at = 0; fail_at < 32; fail_at++) {
        RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
        memtrack_fault_inject(fail_at);
        retained_dl_cache_capture(cache, &source);
        memtrack_fault_clear();
        const auto* fragment = retained_dl_cache_get(cache, 32000);
        if (!fragment) {
            failed = true;
            EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, 0u);
            retained_dl_cache_capture(cache, &source);
            fragment = retained_dl_cache_get(cache, 32000); ASSERT_NE(fragment, nullptr);
        } else succeeded = true;
        EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 0));
        DisplayList frame;
        ASSERT_TRUE(retained_dl_append_fragment(&frame, fragment));
        size_t pinned_bytes = retained_dl_cache_stats(cache).charged_storage_bytes;
        EXPECT_GT(pinned_bytes, 0u);
        retained_dl_cache_set_storage_budget(cache, 0); retained_dl_cache_begin_frame(cache);
        EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, pinned_bytes);
        dl_destroy(&frame);
        EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, 0u);
        retained_dl_cache_destroy(cache);
    }
    EXPECT_TRUE(failed); EXPECT_TRUE(succeeded);
    dl_destroy(&source); image_surface_destroy(surface);
}

TEST_F(RetainedResourceSummaryTest, SurfaceUseWithAnOlderStampCannotBeCoalescedAway) {
    ImageSurface* surface = image_surface_create(2, 2); ASSERT_NE(surface, nullptr);
    uint64_t old_generation = surface->generation;
    image_surface_bump_generation(surface);
    DisplayList source;
    int root = dl_begin_element(&source, 33000, 0, 0, 20, 20);
    for (uint64_t stamp : {surface->generation, old_generation})
        dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr, surface, stamp);
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const auto* fragment = retained_dl_cache_get(cache, 33000); ASSERT_NE(fragment, nullptr);
    DisplayList frame;
    ASSERT_TRUE(retained_dl_append_fragment(&frame, fragment));
    EXPECT_FALSE(dl_resources_current(&source, 0, 0));
    EXPECT_FALSE(dl_resources_current(&frame, 0, 0));
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 0));
    DisplayResourceSummary* snapshot = nullptr;
    ASSERT_TRUE(dl_resource_summary_capture(&frame, &snapshot)); ASSERT_NE(snapshot, nullptr);
    EXPECT_FALSE(dl_resource_summary_current(snapshot, 0, 0));
    dl_resource_summary_destroy(snapshot);
    dl_destroy(&frame); dl_destroy(&source); retained_dl_cache_destroy(cache); image_surface_destroy(surface);
}

TEST_F(RetainedResourceSummaryTest, FlatSummaryFailureIsAtomicAndResourceFreeSnapshotsAllocateNothing) {
    DisplayList source;
    dl_fill_rect(&source, 0, 0, 2, 2, Color{0xff000000u});
    DisplayResourceSummary* snapshot = nullptr;
    memtrack_fault_inject(0);
    bool captured = dl_resource_summary_capture(&source, &snapshot, 0);
    memtrack_fault_clear();
    EXPECT_TRUE(captured); EXPECT_EQ(snapshot, nullptr);
    EXPECT_EQ(dl_resource_summary_bytes(snapshot), 0u);
    EXPECT_TRUE(dl_resource_summary_current(snapshot, 0, 0));
    EXPECT_FALSE(dl_resource_summary_capture(nullptr, &snapshot));
    EXPECT_FALSE(dl_resource_summary_capture(&source, nullptr));
    int root = dl_begin_element(&source, 36000, 0, 0, 20, 20);
    ImageSurface* surfaces[] = {image_surface_create(2, 2), image_surface_create(2, 2)};
    for (ImageSurface* surface : surfaces) {
        ASSERT_NE(surface, nullptr);
        dl_draw_image(&source, (uint32_t*)surface->pixels, 2, 2, 2, 0, 0, 2, 2, 255, nullptr,
            surface, surface->generation);
    }
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList referenced;
    ASSERT_TRUE(retained_dl_append_fragment(&referenced, retained_dl_cache_get(cache, 36000)));
    for (DisplayList* capture_source : {&source, &referenced}) {
        SCOPED_TRACE(capture_source == &referenced);
        test_resource_summary_allocation_failures([&](DisplayResourceSummary** trial) {
            return dl_resource_summary_capture(capture_source, trial);
        });
        ASSERT_TRUE(dl_resource_summary_capture(capture_source, &snapshot));
        size_t required_bytes = dl_resource_summary_bytes(snapshot);
        ASSERT_GT(required_bytes, 0u);
        dl_resource_summary_destroy(snapshot); snapshot = nullptr;
        EXPECT_FALSE(dl_resource_summary_capture(capture_source, &snapshot, required_bytes - 1));
        EXPECT_EQ(snapshot, nullptr);
        ASSERT_TRUE(dl_resource_summary_capture(capture_source, &snapshot, required_bytes));
        EXPECT_EQ(dl_resource_summary_bytes(snapshot), required_bytes);
        dl_resource_summary_destroy(snapshot); snapshot = nullptr;
        DisplayItem invalid = {}; invalid.op = DL_DRAW_FRAGMENT;
        ASSERT_TRUE(capture_source->append(invalid));
        EXPECT_FALSE(dl_resource_summary_capture(capture_source, &snapshot));
        EXPECT_EQ(snapshot, nullptr);
    }
    dl_destroy(&referenced); retained_dl_cache_destroy(cache);
    dl_destroy(&source);
    for (ImageSurface* surface : surfaces) image_surface_destroy(surface);
}
