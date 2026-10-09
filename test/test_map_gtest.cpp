#include <gtest/gtest.h>
#include "../radiant/geomap.hpp"
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/event.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/mem.h"
#include "../lib/image.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"
#include <math.h>

TEST(MapCamera, AnalyticWorldAndBearingRoundTripAtFractionalZoom) {
    GeoMapCamera camera = {103.858, 1.283, 12.5, 37, 640, 360};
    double x, y, lon, lat;
    ASSERT_TRUE(geomap_project(camera, 103.86, 1.29, &x, &y));
    ASSERT_TRUE(geomap_unproject(camera, x, y, &lon, &lat));
    EXPECT_NEAR(lon, 103.86, 1e-10); EXPECT_NEAR(lat, 1.29, 1e-10);
    camera = {0, 0, 0, 0, 256, 192};
    ASSERT_TRUE(geomap_project(camera, 90, 0, &x, &y));
    EXPECT_DOUBLE_EQ(x, 256); EXPECT_NEAR(y, 96, 1e-12);
    camera.bearing = 90;
    ASSERT_TRUE(geomap_project(camera, 90, 0, &x, &y));
    EXPECT_NEAR(x, 128, 1e-10); EXPECT_NEAR(y, -32, 1e-10);
}
TEST(MapCamera, DatelinePolesHighZoomAndInvalidNumbers) {
    GeoMapCamera camera = {179, 0, 1, 0, 256, 192};
    double x, y, lon, lat;
    ASSERT_TRUE(geomap_project(camera, -179, 0, &x, &y)); EXPECT_NEAR(x, 128 + 1024.0 / 180, 1e-10);
    ASSERT_TRUE(geomap_project(camera, 179, 90, &x, &y)); EXPECT_TRUE(isfinite(y));
    camera = {103.858, 1.283, 22, -17, 256, 192};
    ASSERT_TRUE(geomap_project(camera, 103.858001, 1.283001, &x, &y));
    ASSERT_TRUE(geomap_unproject(camera, x, y, &lon, &lat));
    EXPECT_NEAR(lon, 103.858001, 1e-10); EXPECT_NEAR(lat, 1.283001, 1e-10);
    EXPECT_FALSE(geomap_project(camera, NAN, 0, &x, &y));
    EXPECT_FALSE(geomap_project(camera, 181, 0, &x, &y));
    camera.zoom = INFINITY; EXPECT_FALSE(geomap_camera_valid(camera));
}

class MapRender : public ::testing::Test {
protected:
    UiContext ui{};
    DomDocument* document = nullptr;
    DomElement* viewport = nullptr;
    void SetUp() override { ASSERT_EQ(ui_context_init(&ui, true, 1), 0); }
    void TearDown() override {
        ui.document = nullptr;
        if (document) free_document(document);
        ui_context_cleanup(&ui);
    }
    void load(const char* path = "test/map/offline.ls", float scale = 1) {
        DocumentJsHostConfig host = {}; host.ui_context = &ui; host.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
        Url* base = get_current_dir(); ASSERT_NE(base, nullptr);
        document = load_html_doc(base, (char*)path, 360, 260, &host);
        url_destroy(base);
        ASSERT_NE(document, nullptr);
        ui.document = lam::up(document); ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 360; ui.viewport_height = 260;
        ui.window_width = 360 * scale; ui.window_height = 260 * scale;
        ui.create_surface(360 * scale, 260 * scale);
        layout_html_doc(&ui, document, false);
        viewport = dom_find_element_by_id(document->root->as_element(), "viewport");
        ASSERT_NE(viewport, nullptr);
    }
    void pixel(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned tolerance = 2) {
        ASSERT_NE(ui.surface, nullptr);
        ASSERT_LT(x, (unsigned)ui.surface->width); ASSERT_LT(y, (unsigned)ui.surface->height);
        const uint8_t* p = (const uint8_t*)ui.surface->pixels + y * ui.surface->pitch + x * 4;
        const unsigned expected[] = {r,g,b};
        for (unsigned channel = 0; channel < 3; channel++) {
            unsigned difference = p[channel] > expected[channel] ? p[channel] - expected[channel] : expected[channel] - p[channel];
            EXPECT_LE(difference, tolerance) << "pixel " << x << "," << y << " channel " << channel;
        }
    }
};
TEST_F(MapRender, NativeViewportPaintsOrderedLayersHolesAndCssChrome) {
    load(); ASSERT_NE(viewport, nullptr);
    EXPECT_FLOAT_EQ(viewport->width, 280); EXPECT_FLOAT_EQ(viewport->height, 216);
    EXPECT_EQ(viewport->tag_id, MARKUP_NAME_GEOMAP);
    for (DomNode* child = viewport->first_child; child; child = child->next_sibling)
        EXPECT_EQ(child->view_type, RDT_VIEW_NONE);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(156,124,255,0,0); // point above the polygon's hole.
    pixel(176,124,238,243,246); // hole exposes the background.
    pixel(206,124,0,170,0); // polygon outside its hole.
    pixel(156,167,0,0,255); // the line layer paints above the land.
    pixel(24,167,255,255,255); // viewport clipping keeps the road out of padding.
    pixel(18,18,34,34,34); pixel(24,24,255,255,255); pixel(310,10,0,255,0);
    EXPECT_EQ(image_save_png("temp/geomap-native.png", (const unsigned char*)ui.surface->pixels,
        ui.surface->width, ui.surface->height, 4), 1);
}
TEST_F(MapRender, SvgExportContainsNativeVectorPaintAndViewportClip) {
    load(); ASSERT_NE(viewport, nullptr);
    char* svg = render_view_tree_to_svg(&ui, document->view_tree->root, 360, 260);
    ASSERT_NE(svg, nullptr);
    EXPECT_NE(strstr(svg, "rgb(0,170,0)"), nullptr);
    EXPECT_NE(strstr(svg, "rgb(255,0,0)"), nullptr);
    EXPECT_NE(strstr(svg, "clipPath"), nullptr);
    EXPECT_NE(strstr(svg, "evenodd"), nullptr);
    EXPECT_TRUE(save_svg_to_file(svg, "temp/geomap-native.svg"));
    mem_free(svg);
}
TEST_F(MapRender, ExpressionsFilterFeaturesAndUseFractionalPaintValues) {
    load("test/map/expressions.ls"); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(64,96,255,0,0);
    pixel(70,96,255,0,0); // beyond the default radius and inside the interpolated radius.
    pixel(75,96,238,243,246);
    pixel(192,96,119,121,251); // feature ID selects half-opacity blue.
    pixel(128,96,238,243,246); // the village fails the filter.
}
TEST_F(MapRender, InvalidUnusedExpressionBranchRejectsTheWholeMap) {
    load("test/map/invalid_expression.ls"); ASSERT_NE(viewport, nullptr);
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    PaintList paint = {}; paint_list_init(&paint, arena);
    char diagnostic[256];
    EXPECT_FALSE(geomap_paint(&paint, viewport, {0,0,256,192}, diagnostic, sizeof(diagnostic)));
    EXPECT_NE(strstr(diagnostic,"constant expression"), nullptr);
    EXPECT_EQ(paint_list_count(&paint), 1); // the preceding green background is not published.
    paint_list_clear(&paint); arena_destroy(arena);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(128,96,255,224,224);
}
TEST_F(MapRender, DefaultInlineViewportUnwrapsDatelinePolygonsAndHoles) {
    load("test/map/dateline.ls"); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr);
    // inline line-box placement is supplied by layout, never folded into the geographic camera.
    BlockBlot parent = {};
    Rect content = render_geometry_block_content_rect(&parent, lam::view_require_block(viewport), 1);
    EXPECT_FLOAT_EQ(content.width, 256); EXPECT_FLOAT_EQ(content.height, 192);
    pixel((unsigned)(content.x + 139), (unsigned)(content.y + 96), 255,0,0);
    pixel((unsigned)(content.x + 118), (unsigned)(content.y + 96), 238,243,246);
    pixel((unsigned)(content.x + 88), (unsigned)(content.y + 96), 0,170,0);
    char* svg = render_view_tree_to_svg(&ui, document->view_tree->root, 360, 260);
    ASSERT_NE(svg, nullptr); EXPECT_NE(strstr(svg,"rgb(255,0,0)"),nullptr); mem_free(svg);
}
TEST_F(MapRender, InvalidRawElementRejectsPartialPaintWithDiagnostic) {
    load(); ASSERT_NE(viewport, nullptr);
    DomElement* layer = viewport->last_child->as_element(); ASSERT_NE(layer, nullptr);
    ASSERT_TRUE(layer->set_attribute("type", "raster"));
    Arena* arena = arena_create_default(); ASSERT_NE(arena, nullptr);
    PaintList paint = {}; paint_list_init(&paint, arena);
    char diagnostic[256];
    EXPECT_FALSE(geomap_paint(&paint, viewport, {0,0,256,192}, diagnostic, sizeof(diagnostic)));
    EXPECT_NE(strstr(diagnostic, "layer type"), nullptr);
    EXPECT_EQ(paint_list_count(&paint), 1); // no valid prefix of the rejected map was published.
    paint_list_clear(&paint);
    ASSERT_TRUE(layer->set_attribute("type", "circle"));
    DomElement* source = viewport->first_child->as_element(); ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->set_attribute("cluster", "true"));
    EXPECT_FALSE(geomap_paint(&paint, viewport, {0,0,256,192}, diagnostic, sizeof(diagnostic)));
    EXPECT_NE(strstr(diagnostic, "attribute"), nullptr);
    EXPECT_EQ(paint_list_count(&paint), 1);
    paint_list_destroy(&paint); arena_destroy(arena);
}
TEST_F(MapRender, DeviceDensityChangesRasterSizeWithoutChangingGeographicZoom) {
    load("test/map/offline.ls", 2); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(312,248,255,0,0); pixel(412,248,0,170,0); pixel(312,334,0,0,255);
    EXPECT_FLOAT_EQ(viewport->width, 280); EXPECT_FLOAT_EQ(viewport->height, 216);
}
TEST_F(MapRender, RemovingADataLayerChangesNativePixelsWithoutLayoutChildren) {
    // the package fixture initializes the declared DOM host bridge before this native call.
    load("test/map/interactive.ls"); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr); pixel(144,112,255,0,0);
    DomElement* marker = viewport->last_child->as_element(); ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->view_type, RDT_VIEW_NONE);
    Item removed = dom_remove_child_bridge(viewport, dom_wrap_element(marker));
    ASSERT_NE(get_type_id(removed), LMD_TYPE_ERROR);
    ASSERT_NE(removed.item, ItemNull.item);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(144,112,238,243,246); // the removed circle exposes the map background.
}
TEST_F(MapRender, RealPointerCaptureDragWheelAndKeyboardUpdateTheNativeViewport) {
    load("test/map/interactive.ls"); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr); pixel(144,112,255,0,0);
    RdtEvent press = {}; press.type = RDT_EVENT_MOUSE_DOWN;
    press.mouse_button.x = 144; press.mouse_button.y = 112;
    press.mouse_button.button = 0; press.mouse_button.clicks = 1;
    handle_event(&ui, document, &press);
    RdtEvent move = {}; move.type = RDT_EVENT_MOUSE_MOVE;
    move.mouse_position.x = 184; move.mouse_position.y = 112;
    handle_event(&ui, document, &move);
    RdtEvent release = press; release.type = RDT_EVENT_MOUSE_UP;
    release.mouse_button.x = 184; handle_event(&ui, document, &release);
    layout_html_doc(&ui, document, false); render_html_doc(&ui, document->view_tree, nullptr);
    pixel(184,112,255,0,0); pixel(144,112,238,243,246);
    RdtEvent wheel = {}; wheel.type = RDT_EVENT_SCROLL;
    wheel.scroll.x = 184; wheel.scroll.y = 112; wheel.scroll.yoffset = 1;
    handle_event(&ui, document, &wheel);
    layout_html_doc(&ui, document, false); render_html_doc(&ui, document->view_tree, nullptr);
    pixel(184,112,255,0,0); // zoom preserves the geographic point under the cursor.
    viewport = dom_find_element_by_id(document->root->as_element(), "viewport"); ASSERT_NE(viewport, nullptr);
    ItemReader zoom = ElementReader(dom_element_backing(viewport)).get_attr("zoom");
    double zoom_value; ASSERT_TRUE(item_try_to_double(zoom.item(), &zoom_value)); EXPECT_GT(zoom_value, 1);
    RdtEvent key = {}; key.type = RDT_EVENT_KEY_DOWN; key.key.key = RDT_KEY_RIGHT;
    handle_event(&ui, document, &key);
    layout_html_doc(&ui, document, false); render_html_doc(&ui, document->view_tree, nullptr);
    pixel(144,112,255,0,0); pixel(184,112,238,243,246);
}
TEST_F(MapRender, TwoInteractiveViewportsKeepTheirCamerasAndPaintIndependent) {
    load("test/map/two_maps.ls"); ASSERT_NE(viewport, nullptr);
    render_html_doc(&ui, document->view_tree, nullptr);
    pixel(80,64,255,0,0); pixel(240,64,255,0,0);
    RdtEvent press = {}; press.type = RDT_EVENT_MOUSE_DOWN;
    press.mouse_button.x = 80; press.mouse_button.y = 64;
    press.mouse_button.button = 0; press.mouse_button.clicks = 1;
    handle_event(&ui, document, &press);
    RdtEvent move = {}; move.type = RDT_EVENT_MOUSE_MOVE;
    move.mouse_position.x = 104; move.mouse_position.y = 64;
    handle_event(&ui, document, &move);
    RdtEvent release = press; release.type = RDT_EVENT_MOUSE_UP;
    release.mouse_button.x = 104; handle_event(&ui, document, &release);
    layout_html_doc(&ui, document, false); render_html_doc(&ui, document->view_tree, nullptr);
    pixel(104,64,255,0,0); pixel(80,64,238,243,246);
    pixel(240,64,255,0,0); pixel(264,64,238,243,246);
    // inspect committed models as well as pixels: the sibling must retain its initial center.
    viewport = dom_find_element_by_id(document->root->as_element(), "viewport");
    DomElement* other = dom_find_element_by_id(document->root->as_element(), "other");
    ASSERT_NE(viewport, nullptr); ASSERT_NE(other, nullptr);
    ItemReader first_center = ElementReader(dom_element_backing(viewport)).get_attr("center");
    ItemReader second_center = ElementReader(dom_element_backing(other)).get_attr("center");
    ASSERT_TRUE(first_center.isArray()); ASSERT_TRUE(second_center.isArray());
    double first_lon, second_lon;
    ASSERT_TRUE(item_try_to_double(first_center.asArray().get(0).item(), &first_lon));
    ASSERT_TRUE(item_try_to_double(second_center.asArray().get(0).item(), &second_lon));
    EXPECT_LT(first_lon, 0); EXPECT_DOUBLE_EQ(second_lon, 0);
}
