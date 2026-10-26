// glTF 2.0 and GLB export of native scene3d element trees (Radiant_Design_WebGL.md §15).
// The exporter reads the vocabulary the renderer draws: hierarchy and transforms,
// buffer and box/plane geometry, basic/Lambert materials, base-color textures,
// cameras, and directional/point/spot lights. Skins, morph targets and animation
// clips are refused rather than silently dropped.
#include "format.h"
#include "../core/mark_reader.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/escape.h"
#include "../../lib/base64.h"
#include "../../lib/color.h"
#include "../../lib/mesh_primitive.h"
#include "../../lib/arraylist.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include <math.h>
#include <string.h>

namespace {
enum GltfPart { VIEWS, ACCESSORS, MESHES, MATERIALS, TEXTURES, IMAGES, SAMPLERS, LIGHTS, CAMERAS, PART_COUNT };
const char* PART_NAMES[PART_COUNT] = {"bufferViews", "accessors", "meshes", "materials", "textures", "images", "samplers", "", "cameras"};

struct GltfDefinition { const char* id; const char* tag; const void* element; };
struct GltfCache { const void* first; const void* second; int index; };
struct GltfGeometry { const void* element; int position, normal, uv, color, indices; };

struct GltfExport {
    StrBuf* bin = strbuf_new();
    StrBuf* parts[PART_COUNT];
    unsigned counts[PART_COUNT] = {};
    lam::ArrayList<StrBuf*> nodes;               // node JSON by index, filled after children
    lam::ArrayList<GltfDefinition> definitions;
    lam::ArrayList<GltfCache> materials, textures, meshes;
    lam::ArrayList<GltfGeometry> geometries;
    bool unlit = false, emissive_strength = false, lights_used = false;
    const char* error = nullptr;
    GltfExport() { for (auto& part : parts) part = strbuf_new(); }
    ~GltfExport() {
        strbuf_free(bin); for (auto& part : parts) strbuf_free(part);
        for (StrBuf* node : nodes) if (node) strbuf_free(node);
    }
    bool fail(const char* reason) { if (!error) error = reason; return false; }

    // ---- JSON and attribute helpers ----

    StrBuf* begin(GltfPart part, int* index) {
        StrBuf* out = parts[part]; if (counts[part]) strbuf_append_char(out, ',');
        *index = (int)counts[part]++; return out;
    }
    static void number(StrBuf* out, double value) {
        // integers print exactly; other values keep float32 precision.
        if (value == floor(value) && fabs(value) < 1e15) strbuf_append_format(out, "%.0f", value);
        else strbuf_append_format(out, "%.9g", value);
    }
    static void numbers(StrBuf* out, const double* values, unsigned count) {
        strbuf_append_char(out, '[');
        for (unsigned i = 0; i < count; i++) { if (i) strbuf_append_char(out, ','); number(out, values[i]); }
        strbuf_append_char(out, ']');
    }
    static void string(StrBuf* out, const char* value) { escape_append_json_string(out, value, strlen(value), true, false); }
    static const char* text(ElementReader e, const char* key) { return e.get_attr(key).cstring(); }
    static bool scalar(ItemReader value, double* out) {
        if (value.isInt()) { *out = (double)value.asInt(); return true; }
        if (value.isFloat()) { *out = value.asFloat(); return isfinite(*out); }
        return false;
    }
    bool attr_number(ElementReader e, const char* key, double fallback, double* out) {
        ItemReader value = e.get_attr(key);
        if (value.isNull()) { *out = fallback; return true; }
        return scalar(value, out) || fail("non-numeric scene attribute");
    }
    // a fixed-size numeric attribute; false (without failing) when it is absent
    bool vector(ElementReader e, const char* key, double* out, unsigned count, bool* present) {
        ItemReader value = e.get_attr(key); *present = !value.isNull();
        if (!*present) return true;
        if (!value.isArray() || value.asArray().length() != count) return fail("scene vector has the wrong component count");
        for (unsigned i = 0; i < count; i++) if (!scalar(value.asArray().get(i), &out[i])) return fail("scene vector component is not numeric");
        return true;
    }
    bool stream(ElementReader e, const char* key, lam::ArrayList<double>& out) {
        ItemReader value = e.get_attr(key); out.clear();
        if (value.isNull()) return true;
        if (!value.isArray()) return fail("geometry stream is not an array");
        ArrayReader array = value.asArray();
        for (int64_t i = 0; i < array.length(); i++) { double n; if (!scalar(array.get(i), &n)) return fail("geometry stream value is not numeric"); out.append(n); }
        return true;
    }
    // scene colors are sRGB hex; glTF factors are linear
    bool color(ElementReader e, const char* key, const char* fallback, double rgba[4]) {
        const char* value = text(e, key); if (!value) value = fallback;
        if (!strcmp(value, "transparent")) { rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0; return true; }
        uint8_t r, g, b, a; if (!color_parse_hex(value, &r, &g, &b, &a)) return fail("invalid scene color");
        rgba[0] = color_srgb_to_linear(r / 255.0); rgba[1] = color_srgb_to_linear(g / 255.0);
        rgba[2] = color_srgb_to_linear(b / 255.0); rgba[3] = a / 255.0; return true;
    }

    // ---- definitions and buffers ----

    void collect(ElementReader e, unsigned depth = 0) {
        if (depth > 64) return;
        const char* id = text(e, "id");
        if (id && (e.hasTag("geometry") || e.hasTag("material") || e.hasTag("texture"))) definitions.append({id, e.tagName(), e.element()});
        for (int64_t i = 0; i < e.childCount(); i++) { ItemReader child = e.childAt(i); if (child.isElement()) collect(child.asElement(), depth + 1); }
    }
    const void* definition(const char* id, const char* tag) {
        for (auto& d : definitions) if (!strcmp(d.id, id) && !strcmp(d.tag, tag)) return d.element;
        return nullptr;
    }
    // a referenced or inline child resource of `owner`
    bool resource(ElementReader owner, const char* key, const char* tag, ElementReader* out) {
        const char* reference = text(owner, key);
        if (reference) {
            const void* element = definition(reference, tag);
            if (!element) return fail("unresolved scene resource reference");
            *out = ElementReader((const Element*)element); return true;
        }
        *out = owner.findChildElement(tag); return out->isValid() || fail("mesh lacks geometry or material");
    }
    int accessor(const void* data, size_t bytes, unsigned count, unsigned component_type, const char* type,
        int target, const double* minimum = nullptr, const double* maximum = nullptr, unsigned components = 0) {
        while (bin->length % 4) strbuf_append_char(bin, 0);
        int view; StrBuf* v = begin(VIEWS, &view);
        strbuf_append_format(v, "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", bin->length, bytes);
        if (target) strbuf_append_format(v, ",\"target\":%d", target);
        strbuf_append_char(v, '}');
        strbuf_append_str_n(bin, (const char*)data, bytes);
        int index; StrBuf* a = begin(ACCESSORS, &index);
        strbuf_append_format(a, "{\"bufferView\":%d,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"", view, component_type, count, type);
        if (minimum) { strbuf_append_str(a, ",\"min\":"); numbers(a, minimum, components); strbuf_append_str(a, ",\"max\":"); numbers(a, maximum, components); }
        strbuf_append_char(a, '}');
        return index;
    }
    int floats(const lam::ArrayList<float>& values, unsigned width, const char* type, bool bounds = false) {
        unsigned count = (unsigned)(values.size() / width);
        double minimum[3] = {INFINITY, INFINITY, INFINITY}, maximum[3] = {-INFINITY, -INFINITY, -INFINITY};
        if (bounds) for (unsigned i = 0; i < count; i++) for (unsigned c = 0; c < width; c++) {
            minimum[c] = fmin(minimum[c], values[i * width + c]); maximum[c] = fmax(maximum[c], values[i * width + c]);
        }
        return accessor(values.data(), values.size() * sizeof(float), count, 5126, type, 34962,
            bounds ? minimum : nullptr, bounds ? maximum : nullptr, width);
    }

    // ---- geometry, materials and textures ----

    bool geometry(ElementReader e, GltfGeometry* out) {
        for (auto& g : geometries) if (g.element == e.element()) { *out = g; return true; }
        const char* deformation[] = {"skin-indices", "skin-weights", "morph-positions", "morph-normals"};
        for (const char* key : deformation) if (!e.get_attr(key).isNull()) return fail("skinned and morphing geometry is not exported");
        const char* type = text(e, "type");
        lam::ArrayList<float> positions, normals, uvs, colors; lam::ArrayList<uint32_t> indices;
        if (type && (!strcmp(type, "box") || !strcmp(type, "plane"))) {
            double size[3] = {1, 1, 1}; bool present;
            if (!vector(e, "size", size, 3, &present)) return false;
            if (size[0] <= 0 || size[1] <= 0 || size[2] <= 0) return fail("invalid primitive size");
            bool box = !strcmp(type, "box"); unsigned vertices = box ? MESH_BOX_VERTICES : MESH_PLANE_VERTICES;
            float sizes[3] = {(float)size[0], (float)size[1], (float)size[2]};
            float p[MESH_BOX_VERTICES * 3], n[MESH_BOX_VERTICES * 3], t[MESH_BOX_VERTICES * 2]; uint32_t ix[MESH_BOX_INDICES];
            mesh_primitive(box, sizes, p, n, t, ix);
            for (unsigned i = 0; i < vertices * 3; i++) { positions.append(p[i]); normals.append(n[i]); }
            for (unsigned i = 0; i < vertices * 2; i++) uvs.append(t[i]);
            for (unsigned i = 0; i < (box ? MESH_BOX_INDICES : MESH_PLANE_INDICES); i++) indices.append(ix[i]);
        } else if (type && !strcmp(type, "buffer")) {
            lam::ArrayList<double> pv, nv, uv, cv, iv;
            if (!stream(e, "positions", pv) || !stream(e, "normals", nv) || !stream(e, "uvs", uv) ||
                !stream(e, "colors", cv) || !stream(e, "indices", iv)) return false;
            size_t vertices = pv.size() / 3;
            if (!pv.size() || pv.size() % 3 || (nv.size() && nv.size() != pv.size()) || (uv.size() && uv.size() != vertices * 2) ||
                (cv.size() && cv.size() != pv.size()) || (iv.size() ? iv.size() % 3 : vertices % 3)) return fail("invalid geometry component counts");
            for (double v : pv) positions.append((float)v);
            for (double v : nv) normals.append((float)v);
            for (double v : uv) uvs.append((float)v);
            for (double v : cv) { if (v < 0 || v > 1) return fail("vertex color out of range"); colors.append((float)color_srgb_to_linear(v)); }
            for (double v : iv) { if (v < 0 || v >= vertices || floor(v) != v) return fail("geometry index out of bounds"); indices.append((uint32_t)v); }
            if (normals.empty()) {
                // the renderer smooths buffer geometry without normals; export the same normals.
                for (size_t i = 0; i < positions.size(); i++) normals.append(0);
                mesh_accumulate_normals(positions.data(), indices.size() ? indices.data() : nullptr,
                    (unsigned)(indices.size() ? indices.size() : vertices), normals.data());
            }
            for (size_t i = 0; i < vertices; i++) {
                float* n = normals.data() + i * 3; float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (!(length > 0)) return fail("degenerate geometry normals");
                for (unsigned c = 0; c < 3; c++) n[c] /= length;
            }
        } else return fail("unknown geometry type");
        // native raster UVs start at the lower left; glTF image UVs start at the top.
        for (size_t i = 1; i < uvs.size(); i += 2) uvs[i] = 1 - uvs[i];
        GltfGeometry g = {e.element(), -1, -1, -1, -1, -1};
        g.position = floats(positions, 3, "VEC3", true); g.normal = floats(normals, 3, "VEC3");
        if (uvs.size()) g.uv = floats(uvs, 2, "VEC2");
        if (colors.size()) g.color = floats(colors, 3, "VEC3");
        if (indices.size()) {
            size_t vertices = positions.size() / 3;
            if (vertices <= 65535) {
                lam::ArrayList<uint16_t> narrow; for (uint32_t i : indices) narrow.append((uint16_t)i);
                g.indices = accessor(narrow.data(), narrow.size() * 2, (unsigned)narrow.size(), 5123, "SCALAR", 34963);
            } else g.indices = accessor(indices.data(), indices.size() * 4, (unsigned)indices.size(), 5125, "SCALAR", 34963);
        }
        geometries.append(g); *out = g; return true;
    }
    int texture(ElementReader e) {
        for (auto& t : textures) if (t.first == e.element()) return t.index;
        const char* source = text(e, "src");
        if (!source || !*source) { fail("texture requires a source"); return -1; }
        int image; StrBuf* im = begin(IMAGES, &image);
        strbuf_append_str(im, "{\"uri\":"); string(im, source); strbuf_append_char(im, '}');
        const char* keys[] = {"wrapS", "wrapT", "minFilter", "magFilter"}; const char* attrs[] = {"wrap-s", "wrap-t", "min-filter", "mag-filter"};
        int sampler = -1; StrBuf* fields = strbuf_new();
        for (unsigned k = 0; k < 4; k++) {
            double value; if (!attr_number(e, attrs[k], -1, &value)) { strbuf_free(fields); return -1; }
            if (value < 0) continue;
            strbuf_append_format(fields, "%s\"%s\":%.0f", fields->length ? "," : "", keys[k], value);
        }
        if (fields->length) { StrBuf* s = begin(SAMPLERS, &sampler); strbuf_append_format(s, "{%s}", fields->str); }
        strbuf_free(fields);
        int index; StrBuf* t = begin(TEXTURES, &index);
        strbuf_append_format(t, "{\"source\":%d", image);
        if (sampler >= 0) strbuf_append_format(t, ",\"sampler\":%d", sampler);
        strbuf_append_char(t, '}');
        textures.append({e.element(), nullptr, index}); return index;
    }
    int material(ElementReader e) {
        for (auto& m : materials) if (m.first == e.element()) return m.index;
        const char* type = text(e, "type");
        if (!type || (strcmp(type, "basic") && strcmp(type, "lambert"))) { fail("unknown material type"); return -1; }
        if (e.get_attr("depth_only").isBool() && e.get_attr("depth_only").asBool()) { fail("depth-only materials are not exported"); return -1; }
        double rgba[4], opacity, emissive[4], intensity;
        if (!color(e, "color", "#ffffff", rgba) || !attr_number(e, "opacity", 1, &opacity) ||
            !color(e, "emissive", "#000000", emissive) || !attr_number(e, "emissive-intensity", 1, &intensity)) return -1;
        rgba[3] *= opacity;
        int texture_index = -1;
        if (text(e, "texture")) {
            const void* definition_element = definition(text(e, "texture"), "texture");
            if (!definition_element) { fail("unresolved texture reference"); return -1; }
            texture_index = texture(ElementReader((const Element*)definition_element));
        } else if (e.findChildElement("texture").isValid()) texture_index = texture(e.findChildElement("texture"));
        if (error) return -1;
        // without an explicit mode, scene materials blend when their factor or flag is transparent.
        const char* alpha = text(e, "alpha-mode");
        bool blend = alpha ? !strcmp(alpha, "blend") : rgba[3] < 1 || (e.get_attr("transparent").isBool() && e.get_attr("transparent").asBool());
        const char* side = text(e, "side");
        int index; StrBuf* m = begin(MATERIALS, &index);
        strbuf_append_str(m, "{");
        if (text(e, "name")) { strbuf_append_str(m, "\"name\":"); string(m, text(e, "name")); strbuf_append_char(m, ','); }
        strbuf_append_str(m, "\"pbrMetallicRoughness\":{\"baseColorFactor\":"); numbers(m, rgba, 4);
        if (texture_index >= 0) strbuf_append_format(m, ",\"baseColorTexture\":{\"index\":%d}", texture_index);
        // Lambert shading is a fully rough dielectric.
        strbuf_append_str(m, ",\"metallicFactor\":0,\"roughnessFactor\":1}");
        if (blend) strbuf_append_str(m, ",\"alphaMode\":\"BLEND\"");
        // glTF has no back-only culling; a back-side material is exported double-sided.
        if (side && strcmp(side, "front")) strbuf_append_str(m, ",\"doubleSided\":true");
        bool emits = emissive[0] > 0 || emissive[1] > 0 || emissive[2] > 0;
        if (emits && intensity > 0) {
            // factors stay within [0,1]; brighter emission moves into the strength extension.
            double scale = intensity <= 1 ? intensity : 1, factor[3] = {emissive[0] * scale, emissive[1] * scale, emissive[2] * scale};
            strbuf_append_str(m, ",\"emissiveFactor\":"); numbers(m, factor, 3);
        }
        bool strength = emits && intensity > 1, is_unlit = !strcmp(type, "basic");
        if (strength || is_unlit) {
            strbuf_append_str(m, ",\"extensions\":{");
            if (is_unlit) strbuf_append_str(m, "\"KHR_materials_unlit\":{}");
            if (strength) { strbuf_append_format(m, "%s\"KHR_materials_emissive_strength\":{\"emissiveStrength\":", is_unlit ? "," : ""); number(m, intensity); strbuf_append_char(m, '}'); }
            strbuf_append_char(m, '}');
        }
        strbuf_append_char(m, '}');
        unlit |= is_unlit; emissive_strength |= strength;
        materials.append({e.element(), nullptr, index}); return index;
    }
    int mesh(ElementReader e) {
        ElementReader geometry_element, material_element;
        if (!resource(e, "geometry", "geometry", &geometry_element) || !resource(e, "material", "material", &material_element)) return -1;
        for (auto& m : meshes) if (m.first == geometry_element.element() && m.second == material_element.element()) return m.index;
        GltfGeometry g; if (!geometry(geometry_element, &g)) return -1;
        int material_index = material(material_element); if (material_index < 0) return -1;
        int index; StrBuf* out = begin(MESHES, &index);
        strbuf_append_format(out, "{\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d", g.position, g.normal);
        if (g.uv >= 0) strbuf_append_format(out, ",\"TEXCOORD_0\":%d", g.uv);
        if (g.color >= 0) strbuf_append_format(out, ",\"COLOR_0\":%d", g.color);
        strbuf_append_char(out, '}');
        if (g.indices >= 0) strbuf_append_format(out, ",\"indices\":%d", g.indices);
        strbuf_append_format(out, ",\"material\":%d}]}", material_index);
        meshes.append({geometry_element.element(), material_element.element(), index}); return index;
    }

    // ---- nodes ----

    // rotation whose -Z axis points from eye to target, with +Y toward up
    static bool look_at(const double* eye, const double* target, const double* up, double* q) {
        double z[3] = {eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};
        double zl = sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]); if (!(zl > 0)) return false;
        for (double& c : z) c /= zl;
        double x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
        double xl = sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); if (!(xl > 1e-12)) return false;
        for (double& c : x) c /= xl;
        double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
        // columns x, y, z form the rotation matrix m[row][column]
        double m00 = x[0], m01 = y[0], m02 = z[0], m10 = x[1], m11 = y[1], m12 = z[1], m20 = x[2], m21 = y[2], m22 = z[2];
        double trace = m00 + m11 + m22;
        if (trace > 0) { double s = sqrt(trace + 1) * 2; q[3] = s / 4; q[0] = (m21 - m12) / s; q[1] = (m02 - m20) / s; q[2] = (m10 - m01) / s; }
        else if (m00 > m11 && m00 > m22) { double s = sqrt(1 + m00 - m11 - m22) * 2; q[3] = (m21 - m12) / s; q[0] = s / 4; q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s; }
        else if (m11 > m22) { double s = sqrt(1 + m11 - m00 - m22) * 2; q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s; q[1] = s / 4; q[2] = (m12 + m21) / s; }
        else { double s = sqrt(1 + m22 - m00 - m11) * 2; q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s; q[1] = (m12 + m21) / s; q[2] = s / 4; }
        return true;
    }
    // node TRS/matrix; `aim` replaces the rotation with a look-at toward the element's target
    bool transform(ElementReader e, StrBuf* out, bool aim, bool camera) {
        double matrix[16], position[3] = {0, 0, 0}, rotation[3] = {0, 0, 0}, quaternion[4], scale[3] = {1, 1, 1};
        bool has_matrix, has_position, has_rotation, has_quaternion, has_scale;
        if (!vector(e, "matrix", matrix, 16, &has_matrix) || !vector(e, "position", position, 3, &has_position) ||
            !vector(e, "rotation", rotation, 3, &has_rotation) || !vector(e, "quaternion", quaternion, 4, &has_quaternion) ||
            !vector(e, "scale", scale, 3, &has_scale)) return false;
        if (has_matrix && (has_position || has_rotation || has_quaternion || has_scale)) return fail("matrix and TRS transforms are exclusive");
        if (aim) {
            double target[3] = {0, 0, 0}, up[3] = {0, 1, 0}; bool has_target, has_up;
            if (!vector(e, "target", target, 3, &has_target) || !vector(e, "up", up, 3, &has_up)) return false;
            if (camera && !has_target) aim = false;
            else {
                if (has_matrix) for (unsigned c = 0; c < 3; c++) position[c] = matrix[12 + c];
                // a light pointing straight up or down takes +Z as its up vector instead.
                double alternative[3] = {0, 0, 1};
                if (!look_at(position, target, up, quaternion) && (camera || !look_at(position, target, alternative, quaternion)))
                    return fail("light or camera orientation is degenerate");
                strbuf_append_str(out, ",\"translation\":"); numbers(out, position, 3);
                strbuf_append_str(out, ",\"rotation\":"); numbers(out, quaternion, 4);
                return true;
            }
        }
        if (has_matrix) { strbuf_append_str(out, ",\"matrix\":"); numbers(out, matrix, 16); return true; }
        if (has_position) { strbuf_append_str(out, ",\"translation\":"); numbers(out, position, 3); }
        if (has_rotation && !has_quaternion) {
            // Euler XYZ, R = Rx * Ry * Rz, as the renderer composes it.
            double cx = cos(rotation[0] / 2), sx = sin(rotation[0] / 2), cy = cos(rotation[1] / 2), sy = sin(rotation[1] / 2);
            double cz = cos(rotation[2] / 2), sz = sin(rotation[2] / 2);
            quaternion[0] = sx * cy * cz + cx * sy * sz; quaternion[1] = cx * sy * cz - sx * cy * sz;
            quaternion[2] = cx * cy * sz + sx * sy * cz; quaternion[3] = cx * cy * cz - sx * sy * sz;
            has_quaternion = true;
        } else if (has_quaternion) {
            double length = sqrt(quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] + quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
            if (!(length > 0)) return fail("zero node quaternion");
            for (double& c : quaternion) c /= length;
        }
        if (has_quaternion) { strbuf_append_str(out, ",\"rotation\":"); numbers(out, quaternion, 4); }
        if (has_scale) { strbuf_append_str(out, ",\"scale\":"); numbers(out, scale, 3); }
        return true;
    }
    int light(ElementReader e) {
        const char* type = text(e, "type");
        if (!type || (strcmp(type, "directional") && strcmp(type, "point") && strcmp(type, "spot"))) { fail("invalid light type"); return -1; }
        double rgba[4], intensity, distance, angle, penumbra;
        if (!color(e, "color", "#ffffff", rgba) || !attr_number(e, "intensity", 1, &intensity) || !attr_number(e, "distance", 0, &distance) ||
            !attr_number(e, "angle", M_PI / 3, &angle) || !attr_number(e, "penumbra", 0, &penumbra)) return -1;
        int index; StrBuf* out = begin(LIGHTS, &index);
        strbuf_append_format(out, "{\"type\":\"%s\",\"color\":", type); numbers(out, rgba, 3);
        strbuf_append_str(out, ",\"intensity\":"); number(out, intensity);
        if (distance > 0 && strcmp(type, "directional")) { strbuf_append_str(out, ",\"range\":"); number(out, distance); }
        if (!strcmp(type, "spot")) {
            // glTF requires the inner cone to be strictly narrower than the outer cone.
            double inner = fmin(angle * (1 - penumbra), angle * (1 - 1e-6));
            strbuf_append_str(out, ",\"spot\":{\"innerConeAngle\":"); number(out, inner);
            strbuf_append_str(out, ",\"outerConeAngle\":"); number(out, angle); strbuf_append_char(out, '}');
        }
        strbuf_append_char(out, '}'); lights_used = true; return index;
    }
    int camera(ElementReader e) {
        const char* type = text(e, "type"); double fov, near_plane, far_plane, aspect, view_height;
        if (!attr_number(e, "fov", 50, &fov) || !attr_number(e, "near", .1, &near_plane) || !attr_number(e, "far", 1000, &far_plane) ||
            !attr_number(e, "aspect", 0, &aspect) || !attr_number(e, "view_height", 4, &view_height)) return -1;
        bool orthographic = type && !strcmp(type, "orthographic");
        int index; StrBuf* out = begin(CAMERAS, &index);
        if (orthographic) {
            // glTF needs a horizontal extent; an automatic aspect exports a square frame.
            double ymag = view_height / 2, xmag = ymag * (aspect > 0 ? aspect : 1);
            strbuf_append_str(out, "{\"type\":\"orthographic\",\"orthographic\":{\"xmag\":"); number(out, xmag);
            strbuf_append_str(out, ",\"ymag\":"); number(out, ymag);
        } else {
            strbuf_append_str(out, "{\"type\":\"perspective\",\"perspective\":{\"yfov\":"); number(out, fov * M_PI / 180);
            if (aspect > 0) { strbuf_append_str(out, ",\"aspectRatio\":"); number(out, aspect); }
        }
        strbuf_append_str(out, ",\"znear\":"); number(out, near_plane);
        strbuf_append_str(out, ",\"zfar\":"); number(out, far_plane); strbuf_append_str(out, "}}");
        return index;
    }
    int new_node() { nodes.append(nullptr); return (int)nodes.size() - 1; }
    // a node for e and its subtree; -1 means the element has no glTF node
    int node(ElementReader e, unsigned depth = 0) {
        if (error) return -1;
        if (depth > 64) { fail("scene hierarchy exceeds depth 64"); return -1; }
        ItemReader visible = e.get_attr("visible");
        if (visible.isBool() && !visible.asBool()) return -1;
        bool branch = e.hasTag("scene3d") || e.hasTag("group") || e.hasTag("rigid-body");
        if (e.hasTag("skeleton") || e.hasTag("bone")) { fail("skeletons are not exported"); return -1; }
        if (e.hasTag("animation-clip") || e.hasTag("motion-controller")) { fail("animation clips are not exported"); return -1; }
        // glTF has no ambient light; resources, physics and colliders carry no node.
        const char* type = text(e, "type");
        if (e.hasTag("resources") || e.hasTag("geometry") || e.hasTag("material") || e.hasTag("texture") ||
            e.hasTag("physics") || e.hasTag("collider") || e.hasTag("joint") || (e.hasTag("light") && type && !strcmp(type, "ambient"))) return -1;
        bool is_mesh = e.hasTag("mesh"), is_light = e.hasTag("light"), is_camera = e.hasTag("camera");
        if (!branch && !is_mesh && !is_light && !is_camera) { fail("unknown scene element"); return -1; }
        int index = new_node(); StrBuf* out = strbuf_new();
        strbuf_append_char(out, '{');
        const char* name = text(e, "name"); if (!name) name = text(e, "id");
        if (name) { strbuf_append_str(out, "\"name\":"); string(out, name); }
        else strbuf_append_str(out, "\"name\":\"node\"");
        if (is_light && !type) { strbuf_free(out); fail("invalid light type"); return -1; }
        bool aim = is_camera || (is_light && strcmp(type, "point"));
        if (!transform(e, out, aim, is_camera)) { strbuf_free(out); return -1; }
        lam::ArrayList<int> children;
        if (is_mesh) {
            if (!e.get_attr("skeleton").isNull() || !e.get_attr("morph-weights").isNull()) { strbuf_free(out); fail("skinned and morphing meshes are not exported"); return -1; }
            int mesh_index = mesh(e); if (mesh_index < 0) { strbuf_free(out); return -1; }
            lam::ArrayList<double> instances; ItemReader value = e.get_attr("instances");
            // instance matrices become child nodes sharing the mesh.
            if (value.isArray() && value.asArray().length() && value.asArray().get(0).isArray()) {
                for (int64_t i = 0; i < value.asArray().length(); i++) {
                    ItemReader m = value.asArray().get(i);
                    if (!m.isArray() || m.asArray().length() != 16) { strbuf_free(out); fail("instance requires a 16-component matrix"); return -1; }
                    for (int64_t c = 0; c < 16; c++) { double n; if (!scalar(m.asArray().get(c), &n)) { strbuf_free(out); fail("invalid instance matrix"); return -1; } instances.append(n); }
                }
            } else if (!stream(e, "instances", instances)) { strbuf_free(out); return -1; }
            if (instances.size() % 16) { strbuf_free(out); fail("instances require 16-component matrices"); return -1; }
            if (instances.empty()) strbuf_append_format(out, ",\"mesh\":%d", mesh_index);
            for (size_t i = 0; i < instances.size(); i += 16) {
                int child = new_node(); StrBuf* instance = strbuf_new();
                strbuf_append_format(instance, "{\"name\":\"instance\",\"mesh\":%d,\"matrix\":", mesh_index);
                numbers(instance, instances.data() + i, 16); strbuf_append_char(instance, '}');
                nodes[child] = instance; children.append(child);
            }
        } else if (is_light) {
            int light_index = light(e); if (light_index < 0) { strbuf_free(out); return -1; }
            strbuf_append_format(out, ",\"extensions\":{\"KHR_lights_punctual\":{\"light\":%d}}", light_index);
        } else if (is_camera) {
            int camera_index = camera(e); if (camera_index < 0) { strbuf_free(out); return -1; }
            strbuf_append_format(out, ",\"camera\":%d", camera_index);
        }
        if (branch) for (int64_t i = 0; i < e.childCount(); i++) {
            ItemReader child = e.childAt(i); if (!child.isElement()) continue;
            int c = node(child.asElement(), depth + 1);
            if (error) { strbuf_free(out); return -1; }
            if (c >= 0) children.append(c);
        }
        if (children.size()) {
            strbuf_append_str(out, ",\"children\":[");
            for (size_t i = 0; i < children.size(); i++) strbuf_append_format(out, "%s%d", i ? "," : "", children[i]);
            strbuf_append_char(out, ']');
        }
        strbuf_append_char(out, '}'); nodes[index] = out; return index;
    }

    // ---- document ----

    bool document(Item root_item, bool binary, StrBuf* result) {
        ItemReader root(root_item.to_const());
        if (!root.isElement()) return fail("glTF export expects a scene3d element tree");
        ElementReader scene = root.asElement(); collect(scene);
        lam::ArrayList<int> roots;
        // a <scene3d> viewport contributes its children; any other element is the single root.
        if (scene.hasTag("scene3d")) {
            for (int64_t i = 0; i < scene.childCount(); i++) {
                ItemReader child = scene.childAt(i); if (!child.isElement()) continue;
                int n = node(child.asElement()); if (n >= 0) roots.append(n);
            }
        } else { int n = node(scene); if (n >= 0) roots.append(n); }
        if (error) return false;
        StrBuf* json = strbuf_new();
        strbuf_append_str(json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Lambda\"}");
        const char* used[3]; unsigned used_count = 0;
        if (unlit) used[used_count++] = "KHR_materials_unlit";
        if (emissive_strength) used[used_count++] = "KHR_materials_emissive_strength";
        if (lights_used) used[used_count++] = "KHR_lights_punctual";
        if (used_count) {
            strbuf_append_str(json, ",\"extensionsUsed\":[");
            for (unsigned i = 0; i < used_count; i++) strbuf_append_format(json, "%s\"%s\"", i ? "," : "", used[i]);
            strbuf_append_char(json, ']');
        }
        strbuf_append_str(json, ",\"scene\":0,\"scenes\":[{\"nodes\":[");
        for (size_t i = 0; i < roots.size(); i++) strbuf_append_format(json, "%s%d", i ? "," : "", roots[i]);
        strbuf_append_str(json, "]}]");
        if (nodes.size()) {
            strbuf_append_str(json, ",\"nodes\":[");
            for (size_t i = 0; i < nodes.size(); i++) { if (i) strbuf_append_char(json, ','); strbuf_append_str_n(json, nodes[i]->str, nodes[i]->length); }
            strbuf_append_char(json, ']');
        }
        for (unsigned p = 0; p < PART_COUNT; p++) if (counts[p] && p != LIGHTS)
            strbuf_append_format(json, ",\"%s\":[%s]", PART_NAMES[p], parts[p]->str);
        if (counts[LIGHTS]) strbuf_append_format(json, ",\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[%s]}}", parts[LIGHTS]->str);
        while (bin->length % 4) strbuf_append_char(bin, 0);
        if (bin->length) {
            strbuf_append_format(json, ",\"buffers\":[{\"byteLength\":%zu", bin->length);
            if (!binary) {
                char* encoded = base64_encode_alloc((const uint8_t*)bin->str, bin->length, BASE64_STD);
                if (!encoded) { strbuf_free(json); return fail("buffer encoding allocation failed"); }
                strbuf_append_str(json, ",\"uri\":\"data:application/octet-stream;base64,"); strbuf_append_str(json, encoded);
                strbuf_append_char(json, '"'); mem_free(encoded);
            }
            strbuf_append_str(json, "}]");
        }
        strbuf_append_char(json, '}');
        if (!binary) { strbuf_append_str_n(result, json->str, json->length); strbuf_free(json); return true; }
        // GLB 2.0: header, space-padded JSON chunk, zero-padded BIN chunk.
        while (json->length % 4) strbuf_append_char(json, ' ');
        size_t total = 12 + 8 + json->length + (bin->length ? 8 + bin->length : 0);
        if (total > 0xffffffffu) { strbuf_free(json); return fail("GLB exceeds 4 GiB"); }
        auto word = [&](uint32_t value) { for (unsigned i = 0; i < 4; i++) strbuf_append_char(result, (char)(value >> (i * 8))); };
        strbuf_append_str_n(result, "glTF", 4); word(2); word((uint32_t)total);
        word((uint32_t)json->length); word(0x4e4f534a); strbuf_append_str_n(result, json->str, json->length);
        if (bin->length) { word((uint32_t)bin->length); word(0x004e4942); strbuf_append_str_n(result, bin->str, bin->length); }
        strbuf_free(json); return true;
    }
};
}

bool format_gltf_bytes(Item root, bool binary, StrBuf* out, const char** error) {
    GltfExport exporter;
    bool ok = exporter.document(root, binary, out);
    if (!ok) log_error("glTF export: %s", exporter.error ? exporter.error : "failed");
    if (error) *error = ok ? nullptr : exporter.error ? exporter.error : "failed";
    return ok;
}

String* format_gltf(Pool* pool, Item root) {
    StrBuf* out = strbuf_new(); String* result = nullptr;
    if (format_gltf_bytes(root, false, out, nullptr)) result = string_from_strview(strview_init(out->str, out->length), pool);
    strbuf_free(out); return result;
}
