#include "geomap.hpp"
#include "geomap_style.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/mem_factory.h"
#include <stdlib.h>
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/arraylist.h"
#include "../lib/color.h"
#include "../lib/mem.h"
#include "../lib/str.h"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include <math.h>

static constexpr double GEOMAP_PI = 3.14159265358979323846;
static constexpr double GEOMAP_MAX_LATITUDE = 85.0511287798066;
static constexpr unsigned GEOMAP_VERTEX_LIMIT = 262144;
static constexpr unsigned GEOMAP_FEATURE_LIMIT = 16384;

static double geomap_wrap(double value, double period) {
    return value - floor(value / period + 0.5) * period;
}
static double geomap_mercator_y(double latitude) {
    double radians = fmax(-GEOMAP_MAX_LATITUDE, fmin(GEOMAP_MAX_LATITUDE, latitude)) * GEOMAP_PI / 180;
    return (1 - log(tan(GEOMAP_PI / 4 + radians / 2)) / GEOMAP_PI) / 2;
}
bool geomap_camera_valid(const GeoMapCamera& c) {
    return isfinite(c.longitude) && isfinite(c.latitude) && fabs(c.latitude) <= 90 &&
        isfinite(c.zoom) && c.zoom >= -2 && c.zoom <= 22 && isfinite(c.bearing) &&
        isfinite(c.width) && c.width > 0 && isfinite(c.height) && c.height > 0;
}
static void geomap_screen(const GeoMapCamera& c, double dx, double dy, double* x, double* y) {
    double angle = geomap_wrap(c.bearing, 360) * GEOMAP_PI / 180;
    double world = 512 * exp2(c.zoom);
    *x = c.width / 2 + world * (cos(angle) * dx + sin(angle) * dy);
    *y = c.height / 2 + world * (-sin(angle) * dx + cos(angle) * dy);
}
bool geomap_project(const GeoMapCamera& c, double longitude, double latitude, double* x, double* y) {
    if (!x || !y || !geomap_camera_valid(c) || !isfinite(longitude) || fabs(longitude) > 180 ||
        !isfinite(latitude) || fabs(latitude) > 90) return false;
    geomap_screen(c, geomap_wrap((longitude - geomap_wrap(c.longitude, 360)) / 360, 1),
        geomap_mercator_y(latitude) - geomap_mercator_y(c.latitude), x, y);
    return true;
}
bool geomap_unproject(const GeoMapCamera& c, double x, double y, double* longitude, double* latitude) {
    if (!longitude || !latitude || !geomap_camera_valid(c) || !isfinite(x) || !isfinite(y)) return false;
    double angle = geomap_wrap(c.bearing, 360) * GEOMAP_PI / 180;
    double world = 512 * exp2(c.zoom), dx = (x - c.width / 2) / world, dy = (y - c.height / 2) / world;
    *longitude = geomap_wrap(geomap_wrap(c.longitude, 360) + 360 * (cos(angle) * dx - sin(angle) * dy), 360);
    double north = GEOMAP_PI * (1 - 2 * (geomap_mercator_y(c.latitude) + sin(angle) * dx + cos(angle) * dy));
    *latitude = atan(sinh(north)) * 180 / GEOMAP_PI;
    return true;
}

struct GeoMapBuild {
    GeoMapCamera camera;
    ArrayList* shapes;
    ArrayList* styles;
    const char *source, *layer;
    Item feature;
    int64_t feature_index, record;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
    unsigned vertices, features, warnings;
    char style_diagnostic[256];
    const char* diagnostic;
};
static bool geomap_fail(GeoMapBuild* build, const char* message) {
    if (!build->diagnostic) build->diagnostic = message;
    return false;
}
static ItemReader geomap_field(ItemReader item, const char* key) {
    if (item.isMap()) return item.asMap().get(key);
    if (item.isElement()) return item.asElement().get_attr(key);
    return ItemReader();
}
static const char* geomap_text(ItemReader item) {
    if (item.isString()) return item.asString()->chars;
    if (item.isSymbol()) return item.asSymbol()->chars;
    return nullptr;
}
static bool geomap_is(ItemReader item, const char* text) {
    const char* value = geomap_text(item);
    return value && !strcmp(value, text);
}
static bool geomap_number(ItemReader item, double fallback, double* result) {
    if (item.isNull()) { *result = fallback; return true; }
    return item_try_to_double(item.item(), result) && isfinite(*result);
}
static bool geomap_position(GeoMapBuild* b, ItemReader item, double* longitude, double* latitude) {
    if (++b->vertices > GEOMAP_VERTEX_LIMIT) return geomap_fail(b, "vertex quota exceeded");
    if (!item.isArray() || item.asArray().length() < 2 ||
        !geomap_number(item.asArray().get(0), NAN, longitude) ||
        !geomap_number(item.asArray().get(1), NAN, latitude) ||
        !isfinite(*longitude) || !isfinite(*latitude) || fabs(*longitude) > 180 || fabs(*latitude) > 90)
        return geomap_fail(b, "invalid GeoJSON position");
    return true;
}
static bool geomap_add_shape(GeoMapBuild* b, RdtPath* path, Color color, float stroke_width,
    RdtFillRule rule = RDT_FILL_WINDING) {
    if (b->shapes->length>=262144) {rdt_path_free(path);return geomap_fail(b,"shape quota exceeded");}
    auto* shape = (GeoMapShape*)mem_alloc(sizeof(GeoMapShape), MEM_CAT_RENDER);
    if (!shape) { rdt_path_free(path); return geomap_fail(b, "shape allocation failed"); }
    *shape = {path, color, stroke_width, rule, b->cap, b->join,
        b->source, b->layer, b->feature, b->feature_index, b->record};
    if (!arraylist_append(b->shapes, shape)) {
        rdt_path_free(path); mem_free(shape); return geomap_fail(b, "shape quota allocation failed");
    }
    return true;
}
static bool geomap_sequence(GeoMapBuild* b, RdtPath* path, ItemReader coordinates, bool ring, double anchor) {
    if (!coordinates.isArray() || coordinates.asArray().length() < (ring ? 4 : 2))
        return geomap_fail(b, "invalid GeoJSON line or ring");
    ArrayReader points = coordinates.asArray();
    double previous = anchor, first_lon = 0, first_lat = 0, lon = 0, lat = 0;
    for (int64_t i = 0; i < points.length(); i++) {
        if (!geomap_position(b, points.get(i), &lon, &lat)) return false;
        // unwrap connected segments together so a dateline crossing never draws across the world.
        double dx = geomap_wrap((lon - geomap_wrap(b->camera.longitude, 360)) / 360 - previous, 1) + previous;
        previous = dx;
        double x, y;
        geomap_screen(b->camera, dx, geomap_mercator_y(lat) - geomap_mercator_y(b->camera.latitude), &x, &y);
        if (!i) { rdt_path_move_to(path, (float)x, (float)y); first_lon = lon; first_lat = lat; }
        else rdt_path_line_to(path, (float)x, (float)y);
    }
    if (ring) {
        if (lon != first_lon || lat != first_lat) return geomap_fail(b, "GeoJSON ring must be closed");
        rdt_path_close(path);
    }
    return true;
}
static bool geomap_geometry(GeoMapBuild* b, ItemReader geometry, const char* layer_type,
    const GeoMapStyleResult& paint, unsigned depth = 0) {
    if (depth > 32) return geomap_fail(b, "geometry nesting quota exceeded");
    if (!geometry.isMap()) return geomap_fail(b, "geometry must be a GeoJSON map");
    ItemReader type = geomap_field(geometry, "type"), coordinates = geomap_field(geometry, "coordinates");
    if (geomap_is(type, "GeometryCollection")) {
        ItemReader children = geomap_field(geometry, "geometries");
        if (!children.isArray()) return geomap_fail(b, "invalid geometry collection");
        for (int64_t i = 0; i < children.asArray().length(); i++)
            if (!geomap_geometry(b, children.asArray().get(i), layer_type, paint, depth + 1)) return false;
        return true;
    }
    bool point = geomap_is(type, "Point"), multi_point = geomap_is(type, "MultiPoint");
    bool line = geomap_is(type, "LineString"), multi_line = geomap_is(type, "MultiLineString");
    bool polygon = geomap_is(type, "Polygon"), multi_polygon = geomap_is(type, "MultiPolygon");
    if (!point && !multi_point && !line && !multi_line && !polygon && !multi_polygon)
        return geomap_fail(b, "unsupported GeoJSON geometry");
    if (!coordinates.isArray()) return geomap_fail(b, "geometry coordinates must be an array");
    if (!coordinates.asArray().length()) return true;
    Color color=paint.color;float size=paint.size;
    b->cap=paint.cap;b->join=paint.join;
    bool circles = !strcmp(layer_type, "circle"), fills = !strcmp(layer_type, "fill");
    if ((circles && !point && !multi_point) || (!circles && (point || multi_point)) || (fills && !polygon && !multi_polygon)) return true;
    RdtPath* path = rdt_path_new();
    if (!path) return geomap_fail(b, "path allocation failed");
    bool ok = true;
    ArrayReader components = coordinates.asArray();
    int64_t count = point || line || polygon ? 1 : components.length();
    for (int64_t i = 0; ok && i < count; i++) {
        ItemReader component = point || line || polygon ? coordinates : components.get(i);
        if (circles) {
            double lon, lat, x, y;
            ok = geomap_position(b, component, &lon, &lat) && geomap_project(b->camera, lon, lat, &x, &y);
            if (ok) {
                rdt_path_add_circle(path, (float)x, (float)y, size, size);
                if (paint.stroke_width>0 && paint.stroke_color.a) {
                    RdtPath* stroke=rdt_path_new();
                    if (!stroke) {ok=geomap_fail(b,"circle stroke allocation failed");break;}
                    // circle radius describes the inner fill; the stroke grows outwards.
                    float radius=size+paint.stroke_width/2;
                    rdt_path_add_circle(stroke,(float)x,(float)y,radius,radius);
                    ok=geomap_add_shape(b,stroke,paint.stroke_color,paint.stroke_width);
                }
            }
        } else if (line || multi_line) ok = geomap_sequence(b, path, component, false, 0);
        else if (!component.isArray()) ok = geomap_fail(b, "polygon requires an array of rings");
        else if (!component.asArray().length()) continue;
        else {
            ArrayReader rings = component.asArray();
            double anchor = 0;
            // holes use the outer ring's world copy, including at the antimeridian.
            ItemReader first_ring = rings.get(0);
            if (first_ring.isArray() && first_ring.asArray().length()) {
                ItemReader first = first_ring.asArray().get(0);
                double lon, lat;
                ok = geomap_position(b, first, &lon, &lat);
                if (ok) anchor = geomap_wrap((lon - geomap_wrap(b->camera.longitude, 360)) / 360, 1);
            }
            for (int64_t j = 0; ok && j < rings.length(); j++) ok = geomap_sequence(b, path, rings.get(j), true, anchor);
        }
    }
    if (!ok) { rdt_path_free(path); return false; }
    // map width zero hides a line; emitting it would request a PDF device hairline.
    if (!circles && !fills && size == 0) { rdt_path_free(path); return true; }
    RdtPath* outline=fills && paint.has_outline && paint.outline.a?rdt_path_clone(path):nullptr;
    if (fills && paint.has_outline && paint.outline.a && !outline) {rdt_path_free(path);return geomap_fail(b,"outline allocation failed");}
    ok=true;
    if (!color.a || (circles && size==0)) rdt_path_free(path);
    else ok=geomap_add_shape(b,path,color,circles || fills?-1:size,fills?RDT_FILL_EVEN_ODD:RDT_FILL_WINDING);
    if (outline) {
        if (ok) ok=geomap_add_shape(b,outline,paint.outline,1); else rdt_path_free(outline);
    }
    return ok;
}
static void geomap_evaluate(GeoMapBuild* b, const GeoMapStyle* style, ItemReader feature, GeoMapStyleResult* paint) {
    geomap_style_evaluate(style, feature, b->camera.zoom, paint);
    unsigned warnings = paint->warnings & ~b->warnings;
    if (warnings) log_warn("geomap style fallback: layer=%s properties=%u", style->id ? style->id : "", warnings);
    b->warnings |= paint->warnings;
}
static bool geomap_data(GeoMapBuild* b, ItemReader data, const char* type, const GeoMapStyle* style, unsigned depth = 0) {
    if (depth > 32 || ++b->features > GEOMAP_FEATURE_LIMIT) return geomap_fail(b, "feature quota exceeded");
    if (!data.isMap()) return geomap_fail(b, "source data requires typed GeoJSON; use parse(text, 'json') in Lambda");
    ItemReader kind = geomap_field(data, "type");
    if (geomap_is(kind, "FeatureCollection")) {
        ItemReader features = geomap_field(data, "features");
        if (!features.isArray()) return geomap_fail(b, "invalid feature collection");
        for (int64_t i = 0; i < features.asArray().length(); i++)
            { b->feature_index=i;if (!geomap_data(b, features.asArray().get(i), type, style, depth + 1)) return false; }
        return true;
    }
    b->feature=data.item();b->record++;
    GeoMapStyleResult paint;
    geomap_evaluate(b, style, data, &paint);
    if (!paint.visible) return true;
    ItemReader geometry = geomap_is(kind, "Feature") ? geomap_field(data, "geometry") : data;
    return geometry.isNull() || geomap_geometry(b, geometry, type, paint);
}
static bool geomap_supported_attributes(GeoMapBuild* b, ElementReader element,
    const char* const* allowed, unsigned count) {
    auto attrs = element.attrs();
    const char* key; ItemReader value;
    while (attrs.next(&key, &value)) {
        bool supported = false;
        for (unsigned i = 0; i < count; i++) if (!strcmp(key, allowed[i])) { supported = true; break; }
        if (!supported) return geomap_fail(b, "unsupported source or layer attribute");
    }
    return true;
}
static bool geomap_compile(GeoMapBuild* b, Element* root) {
    ElementReader map(root);
    if (map.childCount() > 1024) return geomap_fail(b, "map child quota exceeded");
    double pitch;
    ItemReader center = map.get_attr("center");
    b->camera.longitude = b->camera.latitude = 0;
    if (!center.isNull() && (!center.isArray() || center.asArray().length() != 2 ||
        !geomap_position(b, center, &b->camera.longitude, &b->camera.latitude)))
        return geomap_fail(b, "camera center requires longitude and latitude");
    if (!geomap_number(map.get_attr("zoom"), 0, &b->camera.zoom) ||
        !geomap_number(map.get_attr("bearing"), 0, &b->camera.bearing) ||
        !geomap_number(map.get_attr("pitch"), 0, &pitch) || pitch != 0 || !geomap_camera_valid(b->camera))
        return geomap_fail(b, "invalid camera; native geomap currently requires pitch zero");
    ItemReader projection = map.get_attr("projection"), copies = map.get_attr("world-copies");
    if ((!projection.isNull() && !geomap_is(projection, "mercator")) || (!copies.isNull() && (!copies.isBool() || copies.asBool())))
        return geomap_fail(b, "only Mercator with a single world copy is currently supported");
    for (int64_t i = 0; i < map.childCount(); i++) {
        ItemReader child = map.childAt(i);
        if (!child.isElement()) return geomap_fail(b, "geomap children must be source or layer elements");
        const char* name = child.asElement().tagName();
        if (strcmp(name, "source") && strcmp(name, "layer")) return geomap_fail(b, "unsupported geomap child");
        ItemReader id = geomap_field(child, "id");
        const char* id_text = geomap_text(id);
        if (!id_text || !*id_text) return geomap_fail(b, "source and layer IDs must be nonempty text");
        for (int64_t j = 0; j < i; j++) {
            ItemReader previous = map.childAt(j);
            if (previous.isElement() && !strcmp(name, previous.asElement().tagName()) &&
                geomap_is(geomap_field(previous, "id"), id_text)) return geomap_fail(b, "duplicate source or layer ID");
        }
        if (!strcmp(name, "source") && !geomap_is(geomap_field(child, "type"), "geojson"))
            return geomap_fail(b, "only inline GeoJSON sources are currently supported");
        if (!strcmp(name, "source") && (!geomap_field(child, "url").isNull() || !geomap_field(child, "src").isNull()))
            return geomap_fail(b, "external sources are not yet supported");
        // source transforms such as clustering must fail rather than paint their untransformed data.
        static const char* source_attrs[] = {"id", "type", "data"};
        static const char* layer_attrs[] = {"id", "type", "source", "paint", "layout", "minzoom", "maxzoom", "metadata", "filter", "source-layer"};
        bool source = !strcmp(name, "source");
        if (!geomap_supported_attributes(b, child.asElement(), source ? source_attrs : layer_attrs,
            source ? sizeof(source_attrs) / sizeof(*source_attrs) : sizeof(layer_attrs) / sizeof(*layer_attrs))) return false;
    }
    for (int64_t i = 0; i < map.childCount(); i++) {
        ItemReader layer = map.childAt(i);
        if (strcmp(layer.asElement().tagName(), "layer")) continue;
        const char* type = geomap_text(geomap_field(layer, "type"));
        auto* compiled=(GeoMapStyle*)mem_calloc(1,sizeof(GeoMapStyle),MEM_CAT_RENDER);
        if (!compiled) return geomap_fail(b,"style allocation failed");
        if (!geomap_style_compile(layer.asElement(),compiled,b->style_diagnostic,sizeof(b->style_diagnostic))) {
            mem_free(compiled);return geomap_fail(b,b->style_diagnostic);
        }
        if (!arraylist_append(b->styles,compiled)) {geomap_style_destroy(compiled);mem_free(compiled);return geomap_fail(b,"style list allocation failed");}
        const GeoMapStyle& style=*compiled;
        b->source=nullptr;b->layer=style.id;b->feature=ItemNull;b->feature_index=0;
        b->warnings = 0;
        bool ok = true;
        if (style.kind == GEOMAP_BACKGROUND) {
            GeoMapStyleResult paint;
            geomap_evaluate(b, &style, ItemReader(), &paint);
            if (paint.visible) {
                RdtPath* path = rdt_path_new();
                if (!path) ok = geomap_fail(b, "path allocation failed");
                else {
                    rdt_path_add_rect(path, 0, 0, (float)b->camera.width, (float)b->camera.height, 0, 0);
                    ok = geomap_add_shape(b, path, paint.color, -1);
                }
            }
        } else {
            const char* reference = geomap_text(geomap_field(layer, "source"));
            ItemReader source;
            for (int64_t j = 0; reference && j < map.childCount(); j++) {
                ItemReader candidate = map.childAt(j);
                if (!strcmp(candidate.asElement().tagName(), "source") && geomap_is(geomap_field(candidate, "id"), reference)) source = candidate;
            }
            if (source.isNull()) ok = geomap_fail(b, "layer source does not exist");
            else {b->source=reference;ok = geomap_data(b, geomap_field(source, "data"), type, &style);}
        }
        if (!ok) return false;
    }
    return true;
}
static GeoMapIndexNode geomap_shape_bounds(const GeoMapShape* shape,int index) {
    GeoMapIndexNode node={};node.shape=index;node.first=node.second=-1;
    if (!rdt_path_get_bounds(shape->path,&node.left,&node.top,&node.right,&node.bottom)) return node;
    float reach=shape->stroke_width<0?0:shape->stroke_width/2*(shape->join==RDT_JOIN_MITER?4:1);
    node.left-=reach;node.top-=reach;node.right+=reach;node.bottom+=reach;return node;
}
static int geomap_index_compare(const void* a,const void* b) {
    const auto* x=(const GeoMapIndexNode*)a;const auto* y=(const GeoMapIndexNode*)b;
    float delta=(x->left+x->right)-(y->left+y->right);
    return delta<0?-1:delta>0?1:x->shape-y->shape;
}
static int geomap_index_build(GeoMapFrame* frame,unsigned begin,unsigned end,unsigned* next) {
    if (end-begin==1) return (int)begin; // INT_CAST_OK: bounded shape index.
    unsigned middle=begin+(end-begin)/2,position=(*next)++;
    int first=geomap_index_build(frame,begin,middle,next),second=geomap_index_build(frame,middle,end,next);
    const auto& a=frame->index[first];const auto& b=frame->index[second];
    frame->index[position]={fminf(a.left,b.left),fminf(a.top,b.top),fmaxf(a.right,b.right),fmaxf(a.bottom,b.bottom),first,second,-1};
    return (int)position; // INT_CAST_OK: bounded index node.
}
void geomap_frame_retain(GeoMapFrame* frame) {if(frame) frame->references++;}
void geomap_frame_release(GeoMapFrame* frame) {
    if (!frame || --frame->references) return;
    if (frame->shapes) {
        for (int i=0;i<frame->shapes->length;i++) {auto* shape=(GeoMapShape*)frame->shapes->data[i];rdt_path_free(shape->path);mem_free(shape);}
        arraylist_free(frame->shapes);
    }
    if (frame->styles) {
        for (int i=0;i<frame->styles->length;i++) {auto* style=(GeoMapStyle*)frame->styles->data[i];geomap_style_destroy(style);mem_free(style);}
        arraylist_free(frame->styles);
    }
    mem_free(frame->index);mem_pool_destroy(frame->pool);mem_free(frame);
}
GeoMapFrame* geomap_plan(Element* model,float width,float height,char* diagnostic,size_t capacity) {
    if (diagnostic && capacity) diagnostic[0]=0;
    if (!model || !isfinite(width) || !isfinite(height) || width<=0 || height<=0 || width>1048576 || height>1048576) return nullptr;
    auto* frame=(GeoMapFrame*)mem_calloc(1,sizeof(GeoMapFrame),MEM_CAT_RENDER);
    if (!frame) return nullptr;
    frame->references=1;frame->pool=mem_pool_create(nullptr,MEM_ROLE_RENDER,"geomap.frame");
    frame->input=frame->pool?Input::create(frame->pool,nullptr):nullptr;
    frame->shapes=arraylist_new(16);frame->styles=arraylist_new(8);
    GeoMapBuild build={};build.camera.width=width;build.camera.height=height;
    build.shapes=frame->shapes;build.styles=frame->styles;
    bool ok=frame->input && frame->shapes && frame->styles;
    if (ok) {
        // every retained expression literal, property and feature belongs to this snapshot.
        MarkBuilder builder(frame->input);frame->input->root=builder.deep_copy((Item){.element=model});
        ok=get_type_id(frame->input->root)==LMD_TYPE_ELEMENT && geomap_compile(&build,frame->input->root.element);
    }
    frame->camera=build.camera;
    unsigned count=frame->shapes?(unsigned)frame->shapes->length:0;
    if (ok && count) {
        frame->index=(GeoMapIndexNode*)mem_calloc(count*2-1,sizeof(GeoMapIndexNode),MEM_CAT_RENDER);
        ok=frame->index!=nullptr;
        if (ok) {
            for (unsigned i=0;i<count;i++) frame->index[i]=geomap_shape_bounds((GeoMapShape*)frame->shapes->data[i],(int)i); // INT_CAST_OK: shape index.
            qsort(frame->index,count,sizeof(GeoMapIndexNode),geomap_index_compare);
            frame->index_count=count;geomap_index_build(frame,0,count,&frame->index_count);
        }
    }
    if (!ok) {
        const char* message=build.diagnostic?build.diagnostic:"map frame allocation failed";
        if(diagnostic && capacity) str_copy(diagnostic,capacity,message,strlen(message));
        geomap_frame_release(frame);return nullptr;
    }
    return frame;
}
void geomap_frame_paint(PaintList* paint,const GeoMapFrame* frame,Rect viewport) {
    if (!paint || !frame) return;
    RdtMatrix transform=rdt_matrix_translate(viewport.x,viewport.y);
    RdtPath* clip=rdt_path_new();if (!clip) return;
    rdt_path_add_rect(clip,0,0,viewport.width,viewport.height,0,0);paint_push_clip(paint,clip,&transform);rdt_path_free(clip);
    for (int i=0;i<frame->shapes->length;i++) {
        auto* shape=(GeoMapShape*)frame->shapes->data[i];
        if (shape->stroke_width>=0) paint_stroke_path(paint,shape->path,shape->color,shape->stroke_width,
            shape->cap,shape->join,nullptr,0,0,&transform);
        else paint_fill_path(paint,shape->path,shape->color,shape->rule,&transform);
    }
    paint_pop_clip(paint);
}
StrBuf* geomap_frame_svg(const GeoMapFrame* frame) {
    if (!frame) return nullptr;
    Arena* arena=arena_create_default();if (!arena) return nullptr;
    PaintList paint={};paint_list_init(&paint,arena);
    geomap_frame_paint(&paint,frame,{0,0,(float)frame->camera.width,(float)frame->camera.height});
    StrBuf* out=strbuf_new();
    if (out) {
        strbuf_append_format(out,"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.9g\" height=\"%.9g\" viewBox=\"0 0 %.9g %.9g\">\n",frame->camera.width,frame->camera.height,frame->camera.width,frame->camera.height);
        PaintSvgLoweringOptions options={};options.caps=lam::up(render_export_target_get_caps(RENDER_EXPORT_TARGET_SVG));
        paint_ir_lower_svg(&paint,out,&options,nullptr);strbuf_append_str(out,"</svg>");
    }
    paint_list_destroy(&paint);arena_destroy(arena);return out;
}
bool geomap_shape_hit(const GeoMapShape* shape,Rect query,bool rectangle,float tolerance) {
    if (!shape || !shape->source || !shape->color.a) return false;
    return dom_geometry_path_query(shape->path,query.x,query.y,query.x+query.width,query.y+query.height,rectangle,
        shape->stroke_width<0?(int)shape->rule:-1,shape->stroke_width<0?tolerance*2:shape->stroke_width+tolerance*2,shape->cap,shape->join);
}
struct GeoMapEntry {GeoMapEntry* next;DomNodeRef root;GeoMapFrame* frame;uint64_t epoch,style_epoch,revision;float width,height;bool valid;char diagnostic[256];};
struct GeoMapRegistry:DomDocumentResourceData {GeoMapEntry* entries;};
static void geomap_registry_destroy(DomDocumentResourceData* data) {
    auto* registry=(GeoMapRegistry*)data;
    while(registry->entries) {auto* entry=registry->entries;registry->entries=entry->next;geomap_frame_release(entry->frame);mem_free(entry);}
    mem_free(registry);
}
static GeoMapRegistry* geomap_registry(DomDocument* document,bool create) {
    if (!document) return nullptr;
    for(DomDocumentResource* resource=document->resources;resource;resource=resource->next)
        if(resource->destroy==geomap_registry_destroy) return (GeoMapRegistry*)resource->data.get();
    if (!create) return nullptr;
    auto* registry=(GeoMapRegistry*)mem_calloc(1,sizeof(GeoMapRegistry),MEM_CAT_RENDER);
    if (!registry || !dom_document_add_resource(document,registry,geomap_registry_destroy)) {mem_free(registry);return nullptr;}
    return registry;
}
void geomap_release_subtree(DomNode* root) {
    if(!root || !root->is_element()) return;
    auto* registry=geomap_registry(root->as_element()->doc,false);if(!registry) return;
    auto** link=&registry->entries;
    while(*link) {
        auto* entry=*link;auto* node=dom_node_ref_validate(root->as_element()->doc,entry->root);bool contained=!node;
        for(auto* parent=node;parent;parent=parent->parent) if(parent==root) {contained=true;break;}
        if(contained) {*link=entry->next;geomap_frame_release(entry->frame);mem_free(entry);} else link=&entry->next;
    }
}
static GeoMapEntry* geomap_entry(DomElement* root,bool create) {
    if(!root || root->tag_id!=MARKUP_NAME_GEOMAP) return nullptr;
    auto* registry=geomap_registry(root->doc,create);if(!registry) return nullptr;
    DomNodeRef ref=dom_node_ref(root);
    for(auto* entry=registry->entries;entry;entry=entry->next)
        if(entry->root.address==ref.address && entry->root.expected_id==ref.expected_id) return entry;
    if(!create) return nullptr;
    auto* entry=(GeoMapEntry*)mem_calloc(1,sizeof(GeoMapEntry),MEM_CAT_RENDER);if(!entry) return nullptr;
    entry->root=ref;entry->epoch=UINT64_MAX;entry->next=registry->entries;registry->entries=entry;return entry;
}
GeoMapFrame* geomap_displayed_frame(DomElement* root) {auto* entry=geomap_entry(root,false);return entry && entry->valid?entry->frame:nullptr;}
bool geomap_paint(PaintList* paint,DomElement* root,Rect viewport,char* diagnostic,size_t capacity) {
    auto* entry=geomap_entry(root,true);if(!entry) return false;
    if(entry->epoch!=root->doc->mutation_epoch || entry->style_epoch!=root->doc->style_query_epoch || entry->width!=viewport.width || entry->height!=viewport.height) {
        GeoMapFrame* frame=geomap_plan(dom_element_to_element(root),viewport.width,viewport.height,entry->diagnostic,sizeof(entry->diagnostic));
        geomap_frame_release(entry->frame);entry->frame=frame;entry->valid=frame!=nullptr;
        entry->epoch=root->doc->mutation_epoch;entry->style_epoch=root->doc->style_query_epoch;entry->width=viewport.width;entry->height=viewport.height;
        if(frame) frame->revision=++entry->revision;
    }
    if(diagnostic && capacity) str_copy(diagnostic,capacity,entry->diagnostic,strlen(entry->diagnostic));
    if(entry->valid) geomap_frame_paint(paint,entry->frame,viewport);
    else {
        Color color={};color.r=255;color.g=color.b=224;color.a=255;
        paint_fill_rect(paint,viewport.x,viewport.y,viewport.width,viewport.height,color);
        log_error("geomap projection: %s",entry->diagnostic);
    }
    return entry->valid;
}
StrBuf* geomap_svg(Element* model,float width,float height) {
    GeoMapFrame* frame=geomap_plan(model,width,height,nullptr,0);StrBuf* out=geomap_frame_svg(frame);geomap_frame_release(frame);return out;
}
void render_geomap_content(RasterRenderContext* context, ViewBlock* view) {
    if (!context || !context->paint_list || !context->dl || !view) return;
    Rect viewport = render_geometry_block_content_rect(&context->block, view, context->raster_scale);
    // the shared block scope applies CSS transforms; map zoom is independent of device density.
    Bound clip = context->block.clip;
    RdtPath* clip_path = rdt_path_new();
    if (!clip_path) return;
    rdt_path_add_rect(clip_path, clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top, 0, 0);
    paint_push_clip(context->paint_list, clip_path, nullptr);
    rdt_path_free(clip_path);
    RdtMatrix scale = rdt_matrix_scale(context->raster_scale, context->raster_scale);
    if (context->has_transform) scale = rdt_matrix_multiply(&context->transform, &scale);
    paint_push_transform(context->paint_list, &scale);
    viewport.x /= context->raster_scale; viewport.y /= context->raster_scale;
    viewport.width /= context->raster_scale; viewport.height /= context->raster_scale;
    geomap_paint(context->paint_list, view->as_element(), viewport);
    paint_pop_transform(context->paint_list);
    paint_pop_clip(context->paint_list);
    paint_ir_lower_raster_fragment(context->paint_list, context->dl);
    paint_list_clear(context->paint_list);
}
