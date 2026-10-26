// STL and PLY mesh input. One native parse of each format feeds both the raw
// Lambda mapping (`input(x, 'stl')`) and the scene3d asset conversion, so large
// binary meshes are never rebuilt from boxed values.
#include "input-scene3d.hpp"
#include "input-parsers.h"

namespace lambda {
namespace {

// ---- shared text scanning ----

struct MeshText {
    const char* cursor;
    const char* end;
    MeshText(const char* source, size_t length) : cursor(source), end(source + length) {}
    static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
    // the next whitespace-delimited token; empty at the end of input
    StrView token() {
        while (cursor < end && space(*cursor)) cursor++;
        const char* start = cursor;
        while (cursor < end && !space(*cursor)) cursor++;
        return {start, (size_t)(cursor - start)};
    }
    // the rest of the current line, trimmed; the cursor moves past its terminator
    StrView rest_of_line() {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) cursor++;
        const char* start = cursor;
        while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
        StrView line = {start, (size_t)(cursor - start)};
        if (cursor < end && *cursor == '\r') cursor++;
        if (cursor < end && *cursor == '\n') cursor++;
        strview_trim(&line);
        return line;
    }
};
bool is(StrView s, const char* value) { return strview_equal(&s, value); }
// token text is not NUL-terminated in the source span, so numbers parse from a bounded copy.
bool number(StrView s, double* out) {
    char buffer[64];
    if (!s.length || s.length >= sizeof(buffer)) return false;
    memcpy(buffer, s.str, s.length); buffer[s.length] = 0;
    char* stop; *out = strtod(buffer, &stop);
    return stop == buffer + s.length && isfinite(*out);
}
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
float le_float(const uint8_t* p) { uint32_t bits = le32(p); float value; memcpy(&value, &bits, 4); return value; }

// ---- STL ----

struct StlMesh {
    bool binary = false;
    StrView name = {}, header = {};
    unsigned solids = 0;
    lam::ArrayList<double> positions, normals;   // nine and three components per facet
    lam::ArrayList<uint16_t> attributes;         // binary attribute byte counts
    bool any_attribute = false;
};

bool stl_binary(const char* source, size_t length) {
    return length >= 84 && (uint64_t)84 + (uint64_t)le32((const uint8_t*)source + 80) * 50 == length;
}
bool stl_expect(InputContext& ctx, MeshText& t, const char* word) {
    StrView token = t.token();
    if (is(token, word)) return true;
    ctx.addError("STL input: expected '%s'", word); return false;
}
bool stl_vector(InputContext& ctx, MeshText& t, lam::ArrayList<double>& out) {
    for (unsigned c = 0; c < 3; c++) {
        double value; if (!number(t.token(), &value)) { ctx.addError("STL input: expected a finite coordinate"); return false; }
        out.append(value);
    }
    return true;
}
bool stl_parse(InputContext& ctx, const char* source, size_t length, StlMesh& mesh) {
    if (stl_binary(source, length)) {
        // the binary layout is fixed: 80-byte header, facet count, 50-byte facets.
        const uint8_t* bytes = (const uint8_t*)source; mesh.binary = true;
        size_t header = 80; while (header && (bytes[header - 1] == 0 || bytes[header - 1] == ' ')) header--;
        bool text = true; for (size_t i = 0; i < header; i++) if (bytes[i] < 32 || bytes[i] > 126) text = false;
        if (text) mesh.header = {source, header};
        uint32_t count = le32(bytes + 80);
        for (uint32_t f = 0; f < count; f++) {
            const uint8_t* facet = bytes + 84 + (size_t)f * 50;
            for (unsigned c = 0; c < 12; c++) {
                double value = le_float(facet + c * 4);
                if (!isfinite(value)) { ctx.addError("STL input: non-finite value in facet %u", f); return false; }
                (c < 3 ? mesh.normals : mesh.positions).append(value);
            }
            uint16_t attribute = (uint16_t)(facet[48] | (facet[49] << 8));
            mesh.attributes.append(attribute); mesh.any_attribute |= attribute != 0;
        }
        return true;
    }
    MeshText t(source, length);
    if (length >= 3 && !memcmp(source, "\xef\xbb\xbf", 3)) t.cursor += 3;
    StrView word = t.token();
    if (!is(word, "solid")) { ctx.addError("STL input: expected an ASCII 'solid' or a binary facet count matching the file size"); return false; }
    while (word.length) {
        if (!is(word, "solid")) { ctx.addError("STL input: expected 'solid'"); return false; }
        StrView name = t.rest_of_line();
        if (!mesh.solids++) mesh.name = name;
        for (;;) {
            word = t.token();
            if (is(word, "endsolid")) { t.rest_of_line(); break; }
            if (!is(word, "facet")) { ctx.addError("STL input: expected 'facet' or 'endsolid'"); return false; }
            if (!stl_expect(ctx, t, "normal") || !stl_vector(ctx, t, mesh.normals) ||
                !stl_expect(ctx, t, "outer") || !stl_expect(ctx, t, "loop")) return false;
            for (unsigned v = 0; v < 3; v++) if (!stl_expect(ctx, t, "vertex") || !stl_vector(ctx, t, mesh.positions)) return false;
            if (!stl_expect(ctx, t, "endloop") || !stl_expect(ctx, t, "endfacet")) return false;
        }
        word = t.token();
    }
    return true;
}

// ---- PLY ----

enum PlyType { PLY_INT8, PLY_UINT8, PLY_INT16, PLY_UINT16, PLY_INT32, PLY_UINT32, PLY_FLOAT32, PLY_FLOAT64, PLY_NONE };
const unsigned PLY_SIZES[] = {1, 1, 2, 2, 4, 4, 4, 8};
const ArrayNumElemType PLY_ELEMS[] = {ELEM_INT8, ELEM_UINT8, ELEM_INT16, ELEM_UINT16, ELEM_INT32, ELEM_UINT32, ELEM_FLOAT32, ELEM_FLOAT64};
const unsigned PLY_ELEMENT_LIMIT = 16, PLY_PROPERTY_LIMIT = 64;

PlyType ply_type(StrView s) {
    static const char* names[][2] = {{"char", "int8"}, {"uchar", "uint8"}, {"short", "int16"}, {"ushort", "uint16"},
        {"int", "int32"}, {"uint", "uint32"}, {"float", "float32"}, {"double", "float64"}};
    for (unsigned i = 0; i < 8; i++) if (is(s, names[i][0]) || is(s, names[i][1])) return (PlyType)i;
    return PLY_NONE;
}
struct PlyProperty { StrView name; PlyType type, count_type; };   // count_type is PLY_NONE for scalars
struct PlyElement {
    StrView name; int64_t count;
    PlyProperty properties[PLY_PROPERTY_LIMIT]; unsigned property_count;
};
// rows are stored as one value stream per element; a list is its length followed by its items.
struct PlyFile {
    StrView format;
    lam::ArrayList<StrView> comments, info;
    PlyElement elements[PLY_ELEMENT_LIMIT]; unsigned element_count = 0;
    lam::ArrayList<double> values[PLY_ELEMENT_LIMIT];
    lam::ArrayList<size_t> rows[PLY_ELEMENT_LIMIT];
    int element(const char* name) const {
        for (unsigned e = 0; e < element_count; e++) if (is(elements[e].name, name)) return (int)e;
        return -1;
    }
    int property(int e, const char* name) const {
        if (e < 0) return -1;
        for (unsigned p = 0; p < elements[e].property_count; p++) if (is(elements[e].properties[p].name, name)) return (int)p;
        return -1;
    }
    // the stream offset of property p in row r (a list's offset addresses its length)
    size_t offset(int e, size_t r, int p) const {
        size_t at = rows[e][r];
        for (int k = 0; k < p; k++) at += elements[e].properties[k].count_type == PLY_NONE ? 1 : 1 + (size_t)values[e][at];
        return at;
    }
};

bool ply_header(InputContext& ctx, MeshText& t, PlyFile& ply) {
    if (!is(t.rest_of_line(), "ply")) { ctx.addError("PLY input: missing 'ply' signature"); return false; }
    for (;;) {
        if (t.cursor >= t.end) { ctx.addError("PLY input: header lacks end_header"); return false; }
        StrView line = t.rest_of_line(); MeshText words(line.str, line.length); StrView keyword = words.token();
        if (!keyword.length) continue;
        if (is(keyword, "end_header")) break;
        if (is(keyword, "format")) {
            ply.format = words.token(); StrView version = words.token();
            if ((!is(ply.format, "ascii") && !is(ply.format, "binary_little_endian") && !is(ply.format, "binary_big_endian")) ||
                !is(version, "1.0")) { ctx.addError("PLY input: unsupported format line"); return false; }
        } else if (is(keyword, "comment") || is(keyword, "obj_info")) {
            StrView text = {words.cursor, (size_t)(words.end - words.cursor)}; strview_trim(&text);
            (is(keyword, "comment") ? ply.comments : ply.info).append(text);
        } else if (is(keyword, "element")) {
            if (ply.element_count >= PLY_ELEMENT_LIMIT) { ctx.addError("PLY input: element quota exceeded"); return false; }
            PlyElement& element = ply.elements[ply.element_count++];
            element.name = words.token(); element.property_count = 0; double count;
            if (!element.name.length || !number(words.token(), &count) || count < 0 || count > 2147483647 || floor(count) != count) {
                ctx.addError("PLY input: invalid element declaration"); return false;
            }
            element.count = (int64_t)count;
        } else if (is(keyword, "property")) {
            if (!ply.element_count) { ctx.addError("PLY input: property before any element"); return false; }
            PlyElement& element = ply.elements[ply.element_count - 1];
            if (element.property_count >= PLY_PROPERTY_LIMIT) { ctx.addError("PLY input: property quota exceeded"); return false; }
            PlyProperty& property = element.properties[element.property_count++];
            StrView first = words.token(); property.count_type = PLY_NONE;
            if (is(first, "list")) {
                property.count_type = ply_type(words.token()); property.type = ply_type(words.token());
                if (property.count_type == PLY_NONE || property.count_type >= PLY_FLOAT32) { ctx.addError("PLY input: list counts need an integer type"); return false; }
            } else property.type = ply_type(first);
            property.name = words.token();
            if (property.type == PLY_NONE || !property.name.length) { ctx.addError("PLY input: invalid property declaration"); return false; }
            // `count` already carries each element's row count in the Lambda mapping.
            if (is(property.name, "count")) { ctx.addError("PLY input: property name 'count' is reserved"); return false; }
        } else { ctx.addError("PLY input: unknown header keyword"); return false; }
    }
    if (!ply.format.length) { ctx.addError("PLY input: missing format line"); return false; }
    // a row that reads no bytes would let a tiny file declare billions of rows.
    for (unsigned e = 0; e < ply.element_count; e++) if (ply.elements[e].count && !ply.elements[e].property_count) {
        ctx.addError("PLY input: element without properties has rows"); return false;
    }
    return true;
}
bool ply_binary_value(const uint8_t*& p, const uint8_t* end, PlyType type, bool big, double* out) {
    unsigned size = PLY_SIZES[type]; if ((size_t)(end - p) < size) return false;
    uint8_t bytes[8]; for (unsigned i = 0; i < size; i++) bytes[i] = big ? p[size - 1 - i] : p[i];
    p += size;
    switch (type) {
    case PLY_INT8: *out = (int8_t)bytes[0]; break;
    case PLY_UINT8: *out = bytes[0]; break;
    case PLY_INT16: { int16_t v; memcpy(&v, bytes, 2); *out = v; break; }
    case PLY_UINT16: { uint16_t v; memcpy(&v, bytes, 2); *out = v; break; }
    case PLY_INT32: { int32_t v; memcpy(&v, bytes, 4); *out = v; break; }
    case PLY_UINT32: { uint32_t v; memcpy(&v, bytes, 4); *out = v; break; }
    case PLY_FLOAT32: { float v; memcpy(&v, bytes, 4); *out = v; break; }
    default: { double v; memcpy(&v, bytes, 8); *out = v; break; }
    }
    return isfinite(*out);
}
bool ply_parse(InputContext& ctx, const char* source, size_t length, PlyFile& ply) {
    MeshText t(source, length);
    if (!ply_header(ctx, t, ply)) return false;
    bool ascii = is(ply.format, "ascii"), big = is(ply.format, "binary_big_endian");
    const uint8_t* p = (const uint8_t*)t.cursor; const uint8_t* end = (const uint8_t*)t.end;
    for (unsigned e = 0; e < ply.element_count; e++) {
        const PlyElement& element = ply.elements[e];
        for (int64_t r = 0; r < element.count; r++) {
            ply.rows[e].append(ply.values[e].size());
            for (unsigned k = 0; k < element.property_count; k++) {
                const PlyProperty& property = element.properties[k];
                double n = 1;
                bool list = property.count_type != PLY_NONE;
                if (list) {
                    bool ok = ascii ? number(t.token(), &n) : ply_binary_value(p, end, property.count_type, big, &n);
                    if (!ok || n < 0 || floor(n) != n || n > 65536) { ctx.addError("PLY input: invalid list length in element '%.*s'",
                        (int)element.name.length, element.name.str); return false; }
                    ply.values[e].append(n);
                }
                for (int64_t i = 0; i < (int64_t)n; i++) {
                    double value;
                    bool ok = ascii ? number(t.token(), &value) : ply_binary_value(p, end, property.type, big, &value);
                    // integer properties must hold an integer in their declared range.
                    if (ok && ascii && property.type < PLY_FLOAT32) {
                        static const double low[] = {-128, 0, -32768, 0, -2147483648.0, 0}, high[] = {127, 255, 32767, 65535, 2147483647.0, 4294967295.0};
                        ok = floor(value) == value && value >= low[property.type] && value <= high[property.type];
                    }
                    if (!ok) { ctx.addError("PLY input: truncated or invalid value in element '%.*s'",
                        (int)element.name.length, element.name.str); return false; }
                    ply.values[e].append(value);
                }
            }
        }
    }
    if (ascii && t.token().length) ctx.addWarning("PLY input: data after the last declared element is ignored");
    else if (!ascii && p != end) ctx.addWarning("PLY input: %zu bytes after the last declared element are ignored", (size_t)(end - p));
    return true;
}

// ---- raw Lambda mappings ----

struct MeshBuilder {
    InputContext& ctx;
    Item element(StrView name) { return ctx.builder.createElement(ctx.builder.createName(name.str, name.length)->chars); }
    Item text(StrView s) { return ctx.builder.createStringItem(s.str, s.length); }
    void attr(Item target, const char* key, Item value) {
        ctx.builder.putToElement(lam::gc_borrow(target.element), ctx.builder.createName(key), value);
    }
    void append(Item parent, Item child) {
        list_push_with_owner((List*)parent.element, child, ctx.builder.pool(), ctx.builder.arena(),
            ctx.input()->ui_mode ? ctx.input() : nullptr);
    }
    // a typed ndarray from double samples, converted to the declared element type
    Item column(ArrayNumElemType type, int rank, const int64_t* shape, const double* values, size_t count) {
        Item array = ctx.builder.createNumericArray(type, rank, shape);
        if (get_type_id(array) != LMD_TYPE_ARRAY_NUM) return ItemError;
        char* data = (char*)array.array_num->data;
        for (size_t i = 0; i < count; i++) {
            double v = values[i];
            switch (type) {
            case ELEM_INT8: ((int8_t*)data)[i] = (int8_t)v; break;
            case ELEM_UINT8: ((uint8_t*)data)[i] = (uint8_t)v; break;
            case ELEM_INT16: ((int16_t*)data)[i] = (int16_t)v; break;
            case ELEM_UINT16: ((uint16_t*)data)[i] = (uint16_t)v; break;
            case ELEM_INT32: ((int32_t*)data)[i] = (int32_t)v; break;
            case ELEM_UINT32: ((uint32_t*)data)[i] = (uint32_t)v; break;
            case ELEM_FLOAT32: ((float*)data)[i] = (float)v; break;
            default: ((double*)data)[i] = v; break;
            }
        }
        return array;
    }
    Item strings(const lam::ArrayList<StrView>& values) {
        auto array = ctx.builder.array();
        for (const StrView& value : values) array.append(text(value));
        return array.final();
    }
};

Item stl_mapping(MeshBuilder& b, StlMesh& mesh) {
    Item root = b.element({"stl", 3});
    b.attr(root, "format", b.ctx.builder.createSymbolItem(mesh.binary ? "binary" : "ascii"));
    if (mesh.name.length) b.attr(root, "name", b.text(mesh.name));
    if (mesh.header.length) b.attr(root, "header", b.text(mesh.header));
    if (mesh.solids > 1) b.attr(root, "solids", b.ctx.builder.createInt(mesh.solids));
    // binary STL stores float32; ASCII text keeps double precision.
    ArrayNumElemType type = mesh.binary ? ELEM_FLOAT32 : ELEM_FLOAT64;
    int64_t facets = (int64_t)mesh.normals.size() / 3, normal_shape[2] = {facets, 3}, position_shape[2] = {facets * 3, 3};
    b.attr(root, "normals", b.column(type, 2, normal_shape, mesh.normals.data(), mesh.normals.size()));
    b.attr(root, "positions", b.column(type, 2, position_shape, mesh.positions.data(), mesh.positions.size()));
    if (mesh.any_attribute) {
        lam::ArrayList<double> values; for (uint16_t a : mesh.attributes) values.append(a);
        b.attr(root, "attributes", b.column(ELEM_UINT16, 1, &facets, values.data(), values.size()));
    }
    return root;
}

Item ply_mapping(MeshBuilder& b, PlyFile& ply) {
    Item root = b.element({"ply", 3});
    b.attr(root, "format", b.ctx.builder.createSymbolItem(b.ctx.builder.createName(ply.format.str, ply.format.length)->chars));
    b.attr(root, "version", b.text({"1.0", 3}));
    if (ply.comments.size()) b.attr(root, "comments", b.strings(ply.comments));
    if (ply.info.size()) b.attr(root, "obj_info", b.strings(ply.info));
    for (unsigned e = 0; e < ply.element_count; e++) {
        const PlyElement& element = ply.elements[e]; Item out = b.element(element.name);
        b.attr(out, "count", b.ctx.builder.createInt(element.count));
        for (unsigned k = 0; k < element.property_count; k++) {
            const PlyProperty& property = element.properties[k];
            String* key = b.ctx.builder.createName(property.name.str, property.name.length);
            Item value;
            if (property.count_type == PLY_NONE) {
                // scalar properties become one typed column per property.
                lam::ArrayList<double> column;
                for (int64_t r = 0; r < element.count; r++) column.append(ply.values[e][ply.offset(e, r, k)]);
                int64_t shape = element.count;
                value = b.column(PLY_ELEMS[property.type], 1, &shape, column.data(), column.size());
            } else {
                auto rows = b.ctx.builder.array();
                for (int64_t r = 0; r < element.count; r++) {
                    size_t at = ply.offset(e, r, k); size_t n = (size_t)ply.values[e][at];
                    auto items = b.ctx.builder.array();
                    for (size_t i = 0; i < n; i++) {
                        double v = ply.values[e][at + 1 + i];
                        items.append(property.type < PLY_FLOAT32 ? b.ctx.builder.createInt((int64_t)v) : b.ctx.builder.createFloat(v));
                    }
                    rows.append(items.final());
                }
                value = rows.final();
            }
            b.ctx.builder.putToElement(lam::gc_borrow(out.element), key, value);
        }
        b.append(root, out);
    }
    return root;
}

// ---- scene3d asset conversion ----

// the first of the given property names that the element declares
int ply_first(const PlyFile& ply, int e, const char* a, const char* b, const char* c = nullptr, const char* d = nullptr) {
    const char* names[] = {a, b, c, d};
    for (const char* name : names) { int p = name ? ply.property(e, name) : -1; if (p >= 0) return p; }
    return -1;
}
}

}

bool input_parse_mesh(Input* input, const char* source, size_t length, const char* type) {
    using namespace lambda;
    bool stl = !strcmp(type, "stl");
    if (!stl && strcmp(type, "ply")) return false;
    if (!source) { source = ""; length = 0; }
    InputContext ctx(input, source, length); MeshBuilder b{ctx};
    Item root = ItemError;
    if (stl) { StlMesh mesh; if (stl_parse(ctx, source, length, mesh)) root = stl_mapping(b, mesh); }
    else { PlyFile ply; if (ply_parse(ctx, source, length, ply)) root = ply_mapping(b, ply); }
    input->root = ctx.hasErrors() ? ItemError : root;
    if (ctx.hasErrors() || ctx.hasWarnings()) ctx.logErrors();
    return true;
}

namespace lambda {
bool asset_import_stl(SceneAsset& a, const char* source, size_t length) {
    StlMesh mesh; if (!stl_parse(a.ctx, source, length, mesh)) return false;
    if (mesh.name.length) a.attr(a.root, "name", a.ctx.builder.createStringItem(mesh.name.str, mesh.name.length));
    AssetColumn columns[ASSET_COLUMN_COUNT]; columns[0].name = "positions"; columns[0].width = 3;
    for (double v : mesh.positions) columns[0].values.append(v);
    // stored facet normals are often zero or stale; the winding defines the face normal.
    lam::ArrayList<double> triangles; double normal[3];
    for (unsigned i = 0; i < mesh.positions.size() / 3; i += 3)
        if (asset_face_normal(columns[0].values, i, i + 1, i + 2, normal)) for (unsigned j = 0; j < 3; j++) triangles.append(i + j);
    if (triangles.empty()) return a.fail("STL contains no nondegenerate triangles");
    Item material = ElementReader(a.material()).get_attr("id").item();
    asset_static_chunks(a, a.root, material, columns, triangles, mesh.positions.size() / 3, true);
    return true;
}

bool asset_import_ply(SceneAsset& a, const char* source, size_t length) {
    PlyFile ply; if (!ply_parse(a.ctx, source, length, ply)) return false;
    int ve = ply.element("vertex"), fe = ply.element("face");
    int x = ply.property(ve, "x"), y = ply.property(ve, "y"), z = ply.property(ve, "z");
    if (ve < 0 || x < 0 || y < 0 || z < 0) return a.fail("PLY lacks vertex x/y/z properties");
    int faces = ply_first(ply, fe, "vertex_indices", "vertex_index");
    if (faces < 0) return a.fail("PLY point clouds without faces are not renderable");
    const PlyElement& vertex = ply.elements[ve];
    if (vertex.count > 1048576) return a.fail("PLY vertex quota exceeded");
    int nx = ply.property(ve, "nx"), ny = ply.property(ve, "ny"), nz = ply.property(ve, "nz");
    int red = ply_first(ply, ve, "red", "diffuse_red", "r"), green = ply_first(ply, ve, "green", "diffuse_green", "g"),
        blue = ply_first(ply, ve, "blue", "diffuse_blue", "b");
    int u = ply_first(ply, ve, "s", "u", "texture_u", "texture_s"), v = ply_first(ply, ve, "t", "v", "texture_v", "texture_t");
    int corner_uv = ply.property(fe, "texcoord");
    if (ply.property(ve, "alpha") >= 0) a.ctx.addWarning("scene3d asset: PLY vertex alpha is not rendered");
    const int vertex_columns[][3] = {{x, y, z}, {nx, ny, nz}, {u, v, -1}, {red, green, blue}};
    bool present[4];
    for (unsigned k = 0; k < 4; k++) {
        const int* p = vertex_columns[k]; unsigned width = k == 2 ? 2 : 3;
        present[k] = true; bool any = false;
        for (unsigned c = 0; c < width; c++) { present[k] &= p[c] >= 0; any |= p[c] >= 0; }
        if (any && !present[k]) return a.fail("PLY declares an incomplete vertex attribute group");
        for (unsigned c = 0; c < width; c++) if (present[k] && vertex.properties[p[c]].count_type != PLY_NONE)
            return a.fail("PLY vertex attributes must be scalar properties");
    }
    // integer colors normalize by their type's range; float colors already span [0,1].
    double color_scale = 1;
    if (present[3]) {
        PlyType type = vertex.properties[red].type;
        if (vertex.properties[green].type != type || vertex.properties[blue].type != type ||
            type == PLY_INT8 || type == PLY_INT16 || type == PLY_INT32) return a.fail("PLY colors need one unsigned or float type");
        color_scale = type == PLY_UINT8 ? 255 : type == PLY_UINT16 ? 65535 : type == PLY_UINT32 ? 4294967295.0 : 1;
    }
    AssetColumn source_columns[4];
    for (int64_t r = 0; r < vertex.count; r++) for (unsigned k = 0; k < 4; k++) if (present[k]) {
        for (unsigned c = 0; c < (k == 2 ? 2u : 3u); c++) {
            double value = ply.values[ve][ply.offset(ve, r, vertex_columns[k][c])];
            if (k == 3) { value /= color_scale; if (value < 0 || value > 1) return a.fail("PLY vertex color outside [0,1]"); }
            source_columns[k].values.append(value);
        }
    }
    const lam::ArrayList<double>& points = source_columns[0].values;
    // triangulate every face, dropping zero-area triangles; corners keep their face texcoords.
    lam::ArrayList<double> triangles, corner_uvs; lam::ArrayList<unsigned> corners; lam::ArrayList<double> polygon;
    const PlyElement& face = ply.elements[fe];
    for (int64_t r = 0; r < face.count; r++) {
        size_t at = ply.offset(fe, r, faces), n = (size_t)ply.values[fe][at];
        if (n < 3) return a.fail("PLY face has fewer than three corners");
        size_t uv_at = 0;
        if (corner_uv >= 0) {
            uv_at = ply.offset(fe, r, corner_uv);
            if ((size_t)ply.values[fe][uv_at] != n * 2) return a.fail("PLY face texcoords must hold two values per corner");
        }
        corners.clear(); polygon.clear();
        for (size_t i = 0; i < n; i++) {
            double index = ply.values[fe][at + 1 + i];
            if (index < 0 || index >= vertex.count) return a.fail("PLY face index out of bounds");
            corners.append((unsigned)index);
            for (unsigned c = 0; c < 3; c++) polygon.append(points[(size_t)index * 3 + c]);
        }
        lam::ArrayList<unsigned> local;
        if (n == 3) { local.append(0); local.append(1); local.append(2); }
        else if (!asset_triangulate(a, polygon.data(), n, local)) return false;
        for (size_t t = 0; t < local.size(); t += 3) {
            double normal[3];
            if (!asset_face_normal(points, corners[local[t]], corners[local[t + 1]], corners[local[t + 2]], normal)) continue;
            for (unsigned j = 0; j < 3; j++) {
                triangles.append(corners[local[t + j]]);
                if (corner_uv >= 0) for (unsigned c = 0; c < 2; c++) corner_uvs.append(ply.values[fe][uv_at + 1 + local[t + j] * 2 + c]);
            }
        }
    }
    if (triangles.empty()) return a.fail("PLY contains no nondegenerate triangles");
    if (triangles.size() > 8388608) return a.fail("PLY triangle index quota exceeded");
    if (!present[1]) {
        // area-weighted smooth normals; a vertex whose faces cancel takes one adjacent face's normal.
        lam::ArrayList<double> sums, fallback;
        for (int64_t i = 0; i < vertex.count * 3; i++) { sums.append(0); fallback.append(0); }
        for (size_t t = 0; t < triangles.size(); t += 3) {
            unsigned p = (unsigned)triangles[t], q = (unsigned)triangles[t + 1], s = (unsigned)triangles[t + 2];
            double d1[3], d2[3], cross[3], unit[3];
            for (unsigned c = 0; c < 3; c++) { d1[c] = points[q * 3 + c] - points[p * 3 + c]; d2[c] = points[s * 3 + c] - points[p * 3 + c]; }
            for (unsigned c = 0; c < 3; c++) cross[c] = d1[(c + 1) % 3] * d2[(c + 2) % 3] - d1[(c + 2) % 3] * d2[(c + 1) % 3];
            asset_face_normal(points, p, q, s, unit);
            const unsigned ids[] = {p, q, s};
            for (unsigned corner : ids) for (unsigned c = 0; c < 3; c++) { sums[corner * 3 + c] += cross[c]; fallback[corner * 3 + c] = unit[c]; }
        }
        for (int64_t i = 0; i < vertex.count; i++) {
            double* n = &sums[i * 3]; double length = hypot(hypot(n[0], n[1]), n[2]);
            for (unsigned c = 0; c < 3; c++) source_columns[1].values.append(length > 0 ? n[c] / length : fallback[i * 3 + c]);
        }
        present[1] = true;
    }
    Item material = a.material();
    for (size_t i = 0; i < ply.comments.size(); i++) {
        // MeshLab and photogrammetry tools name the texture image in a comment.
        MeshText words(ply.comments[i].str, ply.comments[i].length);
        if (!is(words.token(), "TextureFile")) continue;
        StrView file = words.rest_of_line(); if (!file.length) continue;
        Item texture = ItemNull;
        Item reference = a.texture(a.ctx.builder.createString(file.str, file.length)->chars, nullptr, &texture);
        if (get_type_id(texture) == LMD_TYPE_NULL) return false;
        a.attr(material, "texture", reference); break;
    }
    const char* names[] = {"positions", "normals", "uvs", "colors"};
    AssetColumn columns[ASSET_COLUMN_COUNT];
    bool by_corner = corner_uv >= 0;
    size_t vertex_count = by_corner ? triangles.size() : (size_t)vertex.count;
    for (unsigned k = 0; k < 4; k++) if (present[k] || (k == 2 && by_corner)) {
        columns[k].name = names[k]; columns[k].width = k == 2 ? 2 : 3;
        if (k == 2 && by_corner) for (double value : corner_uvs) columns[k].values.append(value);
        // per-corner texcoords give each triangle corner its own vertex.
        else if (by_corner) for (double index : triangles) for (unsigned c = 0; c < columns[k].width; c++)
            columns[k].values.append(source_columns[k].values[(size_t)index * columns[k].width + c]);
        else for (double value : source_columns[k].values) columns[k].values.append(value);
    }
    if (by_corner) for (size_t i = 0; i < triangles.size(); i++) triangles[i] = (double)i;
    asset_static_chunks(a, a.root, ElementReader(material).get_attr("id").item(), columns, triangles, vertex_count, false);
    return true;
}
}
