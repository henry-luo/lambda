#include "input.hpp"
#include "input-parsers.h"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/arraylist.hpp"
#include "../../lib/hex.h"
#include "../../lib/str.h"
#include <math.h>

using namespace lambda;

namespace {

// model files are data: ordered statements retain state changes and references
// without loading dependencies, executing commands, or triangulating surfaces.
struct ModelText {
    InputContext ctx;
    const char* cursor;
    const char* end;
    StrBuf* line;

    ModelText(Input* input, const char* source, size_t length)
        : ctx(input, source, length), cursor(source), end(source + length),
          line(strbuf_new()) {
        if (length >= 3 && memcmp(source, "\xef\xbb\xbf", 3) == 0) cursor += 3;
    }
    ~ModelText() { strbuf_free(line); }

    bool next_line(StrView* out, bool wavefront = false) {
        if (cursor == end) return false;
        ctx.syncTo(cursor);
        strbuf_reset(line);
        bool continued;
        do {
            const char* start = cursor;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
            const char* stop = cursor;
            if (cursor < end && *cursor == '\r') cursor++;
            if (cursor < end && *cursor == '\n') cursor++;
            if (wavefront) {
                char quote = 0;
                for (const char* p = start; p < stop; p++) {
                    if ((*p == '\'' || *p == '"') && (p == start || p[-1] != '\\')) {
                        if (!quote && (p == start || p[-1] == ' ' || p[-1] == '\t')) quote = *p;
                        else if (quote == *p) quote = 0;
                    }
                    if (!quote && *p == '#') { stop = p; break; }
                }
                while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
            }
            continued = wavefront && stop > start && stop[-1] == '\\';
            if (continued) stop--;
            strbuf_append_str_n(line, start, (size_t)(stop - start));
            if (continued) {
                strbuf_append_char(line, ' ');
                if (cursor == end) ctx.addError("model input: unfinished line continuation");
            }
        } while (continued && cursor < end);
        *out = {line->str, line->length};
        strview_trim(out);
        return true;
    }

    Item text(StrView s) { return ctx.builder.createStringItem(s.str, s.length); }
    Item element(const char* name) { return ctx.builder.createElement(name); }
    Item element(StrView name) {
        String* key = ctx.builder.createName(name.str, name.length);
        return element(key->chars);
    }
    void attr(Item target, const char* key, Item value) {
        ctx.builder.putToElement(lam::gc_borrow(target.element), ctx.builder.createName(key), value);
    }
    void append(Item parent, Item child) {
        list_push_with_owner((List*)parent.element, child, ctx.builder.pool(),
            ctx.builder.arena(), ctx.input()->ui_mode ? ctx.input() : nullptr);
    }
    void finish(Item root) {
        ctx.input()->root = ctx.hasErrors() ? ItemError : root;
        if (ctx.hasErrors() || ctx.hasWarnings()) ctx.logErrors();
    }
};

static bool is(StrView s, const char* value) { return strview_equal(&s, value); }

static StrView token(ModelText& p, StrView* rest) {
    strview_trim(rest);
    const char* start = rest->str;
    size_t n = 0;
    char quote = rest->length && (*start == '"' || *start == '\'') ? *start : 0;
    if (quote) {
        start++;
        n = 1;
        while (n < rest->length && (rest->str[n] != quote || rest->str[n - 1] == '\\')) n++;
        if (n == rest->length) p.ctx.addError("model input: unterminated quoted token");
        StrView result = {start, n - 1};
        size_t used = n < rest->length ? n + 1 : n;
        *rest = {rest->str + used, rest->length - used};
        return result;
    }
    while (n < rest->length && rest->str[n] != ' ' && rest->str[n] != '\t') n++;
    StrView result = {start, n};
    *rest = {rest->str + n, rest->length - n};
    return result;
}

static Item rest_text(ModelText& p, StrView rest) {
    strview_trim(&rest);
    if (rest.length >= 2 && (rest.str[0] == '"' || rest.str[0] == '\'') &&
            rest.str[rest.length - 1] == rest.str[0]) {
        rest = {rest.str + 1, rest.length - 2};
    }
    return p.text(rest);
}

static bool decimal_token(StrView s, bool integer) {
    size_t at = 0;
    if (at < s.length && (s.str[at] == '+' || s.str[at] == '-')) at++;
    size_t digits = 0;
    while (at < s.length && s.str[at] >= '0' && s.str[at] <= '9') { at++; digits++; }
    if (!integer && at < s.length && s.str[at] == '.') {
        at++;
        while (at < s.length && s.str[at] >= '0' && s.str[at] <= '9') { at++; digits++; }
    }
    if (!digits) return false;
    if (!integer && at < s.length && (s.str[at] == 'e' || s.str[at] == 'E')) {
        at++;
        if (at < s.length && (s.str[at] == '+' || s.str[at] == '-')) at++;
        size_t exponent = at;
        while (at < s.length && s.str[at] >= '0' && s.str[at] <= '9') at++;
        if (at == exponent) return false;
    }
    return at == s.length;
}

static Item number(ModelText& p, StrView s, bool integer = false) {
    double value;
    int64_t index;
    if (decimal_token(s, integer)) {
        if (integer && strview_to_int64(&s, &index)) return p.ctx.builder.createInt(index);
        if (!integer && strview_to_double(&s, &value) && isfinite(value))
            return p.ctx.builder.createFloat(value);
    }
    p.ctx.addError("model input: invalid %s '%.*s'", integer ? "integer" : "number",
        (int)s.length, s.str);
    return ItemNull;
}

static Item numeric_array(ModelText& p, StrView rest, size_t minimum, size_t maximum,
                          bool integer = false) {
    ArrayBuilder values = p.ctx.builder.array();
    size_t count = 0;
    while ((strview_trim(&rest), rest.length)) {
        values.append(number(p, token(p, &rest), integer));
        count++;
    }
    if (count < minimum || count > maximum)
        p.ctx.addError("model input: expected %zu..%zu numeric values, got %zu", minimum, maximum, count);
    return values.final();
}

static void generic_arguments(ModelText& p, Item record, StrView rest, bool strings = false) {
    ArrayBuilder operands = p.ctx.builder.array();
    size_t count = 0;
    while ((strview_trim(&rest), rest.length)) {
        StrView value = token(p, &rest);
        Item item;
        if (strings) item = p.text(value);
        else if (decimal_token(value, true)) item = parse_integer_token_exact(p.ctx, value.str, value.length);
        else if (decimal_token(value, false)) item = number(p, value);
        else item = p.text(value);
        operands.append(item);
        count++;
    }
    // S2.6.4 merges adjacent content strings; an array preserves operand bounds.
    if (count) p.append(record, operands.final());
}

// OBJ corners are signed one-based references; A3D corners are zero-based.
// Retaining authored indices avoids losing relative OBJ references or topology.
static Item corner(ModelText& p, StrView source, bool a3d, unsigned fields) {
    static const char* keys[] = {"v", "vt", "vn", "maximum"};
    MapBuilder result = p.ctx.builder.map();
    StrViewSplitIter split;
    strview_split_init(&split, source, '/');
    StrView part;
    unsigned at = 0;
    while (strview_split_next(&split, &part)) {
        if (at >= fields) { p.ctx.addError("model input: too many corner indices"); break; }
        if (part.length) {
            Item value = number(p, part, true);
            if (get_type_id(value) != LMD_TYPE_NULL) {
                int64_t index = ItemReader(value.to_const()).asInt();
                if ((!a3d && index == 0) || (a3d && index < 0))
                    p.ctx.addError("model input: invalid %s vertex index", a3d ? "zero-based" : "one-based");
                result.put(keys[at], value);
            }
        } else if (at == 0) p.ctx.addError("model input: missing vertex index");
        at++;
    }
    if (!at) p.ctx.addError("model input: missing vertex index");
    if (source.length && source.str[source.length - 1] == '/')
        p.ctx.addError("model input: missing final corner index");
    return result.final();
}

static void corners(ModelText& p, Item record, StrView rest, bool a3d,
                    unsigned fields, size_t minimum, size_t maximum = SIZE_MAX) {
    size_t count = 0;
    while ((strview_trim(&rest), rest.length)) {
        p.append(record, corner(p, token(p, &rest), a3d, fields));
        count++;
    }
    if (count < minimum || count > maximum)
        p.ctx.addError("model input: invalid corner count %zu", count);
}

struct NumericStatement { const char* name; size_t minimum; size_t maximum; bool integer; };

static bool numeric_statement(ModelText& p, Item record, StrView name, StrView rest,
                              const NumericStatement* table, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!is(name, table[i].name)) continue;
        p.append(record, numeric_array(p, rest, table[i].minimum, table[i].maximum, table[i].integer));
        return true;
    }
    return false;
}

static void wavefront_obj(ModelText& p) {
    Item root = p.element("obj");
    StrView line;
    static const NumericStatement numeric[] = {
        {"v", 3, 8, false}, {"vt", 1, 3, false}, {"vn", 3, 3, false}, {"vp", 1, 3, false},
        {"deg", 1, 2, true}, {"step", 1, 2, true}, {"lod", 1, 1, true},
        {"curv2", 1, SIZE_MAX, true}, {"sp", 1, SIZE_MAX, true}
    };
    while (p.next_line(&line, true) && !p.ctx.shouldStopParsing()) {
        if (!line.length) continue;
        StrView name = token(p, &line);
        Item record = p.element(name);
        if (numeric_statement(p, record, name, line, numeric, sizeof(numeric) / sizeof(numeric[0]))) {}
        else if (is(name, "f") || is(name, "l") || is(name, "p")) {
            corners(p, record, line, false, is(name, "f") ? 3 : (is(name, "l") ? 2 : 1),
                is(name, "f") ? 3 : (is(name, "l") ? 2 : 1));
        } else if (is(name, "curv") || is(name, "surf")) {
            const char* bounds[] = {"u0", "u1", "v0", "v1"};
            size_t count = is(name, "curv") ? 2 : 4;
            for (size_t i = 0; i < count; i++) p.attr(record, bounds[i], number(p, token(p, &line)));
            corners(p, record, line, false, count == 2 ? 1 : 3, 1);
        } else if (is(name, "parm") || is(name, "bmat")) {
            StrView axis = token(p, &line);
            if (!is(axis, "u") && !is(axis, "v")) p.ctx.addError("OBJ input: expected u or v axis");
            p.attr(record, "axis", p.text(axis));
            p.append(record, numeric_array(p, line, 1, SIZE_MAX));
        } else if (is(name, "trim") || is(name, "hole") || is(name, "scrv")) {
            size_t count = 0;
            while ((strview_trim(&line), line.length)) {
                MapBuilder segment = p.ctx.builder.map();
                segment.put("start", number(p, token(p, &line)));
                segment.put("end", number(p, token(p, &line)));
                segment.put("curve", number(p, token(p, &line), true));
                p.append(record, segment.final());
                count++;
            }
            if (!count) p.ctx.addError("OBJ input: missing trimming curve");
        } else if (is(name, "o") || is(name, "usemtl") || is(name, "usemap") ||
                   is(name, "shadow_obj") || is(name, "trace_obj") || is(name, "csh")) {
            p.append(record, rest_text(p, line));
        } else {
            // free-form attributes, groups, legacy commands and extensions keep
            // their ordered typed operands; interpreting them is a consumer job.
            generic_arguments(p, record, line, is(name, "g") || is(name, "mtllib") || is(name, "maplib"));
        }
        p.append(root, record);
    }
    p.finish(root);
}

static bool material_color(StrView name) {
    return is(name, "Ka") || is(name, "Kd") || is(name, "Ks") || is(name, "Ke") || is(name, "Tf");
}

static bool texture_statement(StrView name) {
    return strview_starts_with(&name, "map_") || is(name, "bump") || is(name, "disp") ||
        is(name, "decal") || is(name, "refl") || is(name, "norm");
}

static void texture_arguments(ModelText& p, Item record, StrView rest) {
    Item options = p.element("options");
    while ((strview_trim(&rest), rest.length && rest.str[0] == '-')) {
        StrView option_name = token(p, &rest);
        Item option = p.element({option_name.str + 1, option_name.length - 1});
        size_t minimum = 1, maximum = 1;
        bool numeric = false;
        if (is(option_name, "-o") || is(option_name, "-s") || is(option_name, "-t")) {
            numeric = true; maximum = 3;
        } else if (is(option_name, "-mm")) { numeric = true; minimum = maximum = 2; }
        else if (is(option_name, "-bm") || is(option_name, "-boost") || is(option_name, "-texres")) numeric = true;
        else if (!is(option_name, "-blendu") && !is(option_name, "-blendv") && !is(option_name, "-cc") &&
                 !is(option_name, "-clamp") && !is(option_name, "-imfchan") && !is(option_name, "-type") &&
                 !is(option_name, "-colorspace")) {
            // unknown arity makes the filename boundary ambiguous; preserve the
            // complete tail instead of assigning some operands to a false path.
            p.attr(record, "unparsed", p.text({option_name.str,
                (size_t)(rest.str + rest.length - option_name.str)}));
            p.attr(record, "options", options);
            p.ctx.addWarning("MTL input: preserving unknown texture option tail");
            return;
        }
        size_t count = 0;
        while (count < maximum) {
            StrView next = rest;
            StrView value = token(p, &next);
            if (!value.length || (numeric && !decimal_token(value, false))) break;
            p.append(option, numeric ? number(p, value) : p.text(value));
            rest = next;
            count++;
        }
        if (count < minimum) p.ctx.addError("MTL input: missing texture option operand");
        p.append(options, option);
    }
    p.attr(record, "options", options);
    strview_trim(&rest);
    if (!rest.length) p.ctx.addError("MTL input: missing texture filename");
    p.attr(record, "file", rest_text(p, rest));
}

static Item material_statement(ModelText& p, StrView name, StrView rest, bool a3d) {
    Item record = p.element(name);
    static const NumericStatement numeric[] = {
        {"Ns", 1, 1, false}, {"Ni", 1, 1, false}, {"sharpness", 1, 1, false},
        {"illum", 1, 1, true}, {"Tr", 1, 1, false}, {"Pr", 1, 1, false},
        {"Pm", 1, 1, false}, {"Ps", 1, 1, false}, {"Pc", 1, 1, false},
        {"Pcr", 1, 1, false}, {"aniso", 1, 1, false}, {"anisor", 1, 1, false}
    };
    if (material_color(name) && !a3d) {
        StrView first = token(p, &rest);
        if (is(first, "spectral")) {
            p.attr(record, "space", p.ctx.builder.createStringItem("spectral"));
            StrView file = token(p, &rest);
            if (!file.length) p.ctx.addError("MTL input: missing spectral filename");
            p.attr(record, "file", p.text(file));
            strview_trim(&rest);
            p.attr(record, "factor", rest.length ? number(p, token(p, &rest)) : p.ctx.builder.createFloat(1.0));
            strview_trim(&rest);
            if (rest.length) p.ctx.addError("MTL input: excess spectral operands");
        } else {
            bool xyz = is(first, "xyz");
            p.attr(record, "space", p.ctx.builder.createStringItem(xyz ? "xyz" : "rgb"));
            if (!xyz) rest = {first.str, (size_t)(rest.str + rest.length - first.str)};
            p.append(record, numeric_array(p, rest, 1, 3));
        }
    } else if (texture_statement(name) && !a3d) texture_arguments(p, record, rest);
    else if (is(name, "d")) {
        StrView first = token(p, &rest);
        if (is(first, "-halo")) p.attr(record, "halo", p.ctx.builder.createBool(true));
        else rest = {first.str, (size_t)(rest.str + rest.length - first.str)};
        p.append(record, numeric_array(p, rest, 1, 1));
    } else if (!numeric_statement(p, record, name, rest, numeric, sizeof(numeric) / sizeof(numeric[0]))) {
        if (a3d && (material_color(name) || texture_statement(name))) p.append(record, rest_text(p, rest));
        else generic_arguments(p, record, rest);
    }
    return record;
}

static void wavefront_mtl(ModelText& p) {
    Item root = p.element("mtl");
    Item material = ItemNull;
    StrView line;
    while (p.next_line(&line, true) && !p.ctx.shouldStopParsing()) {
        if (!line.length) continue;
        StrView name = token(p, &line);
        if (is(name, "newmtl")) {
            strview_trim(&line);
            if (!line.length) p.ctx.addError("MTL input: missing material name");
            material = p.element("material");
            p.attr(material, "name", rest_text(p, line));
            p.append(root, material);
        } else {
            if (get_type_id(material) == LMD_TYPE_NULL) {
                p.ctx.addError("MTL input: property precedes newmtl");
                continue;
            }
            p.append(material, material_statement(p, name, line, false));
        }
    }
    p.finish(root);
}

static Item nonnegative(ModelText& p, StrView value) {
    Item result = number(p, value, true);
    if (get_type_id(result) != LMD_TYPE_NULL && ItemReader(result.to_const()).asInt() < 0)
        p.ctx.addError("A3D input: expected a nonnegative index or time");
    return result;
}

static bool color_code(ModelText& p, StrView value) {
    bool valid = value.length == 9 && value.str[0] == '#';
    for (size_t i = 1; valid && i < value.length; i++) valid = hex_decode_byte(value.str[i]) >= 0;
    if (!valid) p.ctx.addError("A3D input: expected #AARRGGBB color");
    return valid;
}

static Item bone_weights(ModelText& p, StrView* rest) {
    ArrayBuilder weights = p.ctx.builder.array();
    size_t count = 0;
    double total = 0.0;
    bool implicit = false;
    while ((strview_trim(rest), rest->length && rest->str[0] != '{')) {
        StrView value = token(p, rest);
        int colon = strview_find(&value, ":");
        StrView bone = colon < 0 ? value : StrView{value.str, (size_t)colon};
        Item weight = colon < 0 ? p.ctx.builder.createFloat(1.0)
            : number(p, {value.str + colon + 1, value.length - (size_t)colon - 1});
        double amount = ItemReader(weight.to_const()).asFloat();
        if (amount < 0.0 || amount > 1.0) p.ctx.addError("A3D input: bone weight outside 0..1");
        MapBuilder entry = p.ctx.builder.map();
        entry.put("bone", nonnegative(p, bone)).put("weight", weight);
        weights.append(entry.final());
        implicit |= colon < 0;
        total += amount;
        count++;
    }
    if (count > 8 || (count > 1 && implicit) || (count && fabs(total - 1.0) > 0.0001))
        p.ctx.addError("A3D input: expected at most eight normalized bone weights");
    return weights.final();
}

static Item a3d_vertex(ModelText& p, StrView rest) {
    Item vertex = p.element("vertex");
    ArrayBuilder coordinates = p.ctx.builder.array();
    for (size_t i = 0; i < 4; i++) coordinates.append(number(p, token(p, &rest)));
    p.append(vertex, coordinates.final());
    strview_trim(&rest);
    if (rest.length && rest.str[0] == '#') {
        StrView color = token(p, &rest);
        color_code(p, color);
        p.attr(vertex, "color", p.text(color));
    }
    p.attr(vertex, "weights", bone_weights(p, &rest));
    if (rest.length) p.ctx.addError("A3D input: unexpected vertex operands");
    return vertex;
}

static int64_t hex_integer(ModelText& p, StrView value, size_t digits) {
    if (!value.length || value.length > digits) {
        p.ctx.addError("A3D input: invalid hexadecimal field width");
        return 0;
    }
    int64_t result = 0;
    for (size_t i = 0; i < value.length; i++) {
        int digit = hex_decode_byte(value.str[i]);
        if (digit < 0) { p.ctx.addError("A3D input: invalid hexadecimal digit"); return 0; }
        result = result * 16 + digit;
    }
    return result;
}

static Item a3d_voxel_type(ModelText& p, StrView rest) {
    Item type = p.element("type");
    StrView fields = token(p, &rest);
    StrViewSplitIter split;
    strview_split_init(&split, fields, '/');
    StrView value;
    unsigned index = 0;
    while (strview_split_next(&split, &value)) {
        if (index == 0) { color_code(p, value); p.attr(type, "color", p.text(value)); }
        else if (index <= 2) p.attr(type, index == 1 ? "rotation" : "shape",
            p.ctx.builder.createInt(hex_integer(p, value, index == 1 ? 2 : 3)));
        else p.ctx.addError("A3D input: excess voxel type color fields");
        index++;
    }
    StrView name = token(p, &rest);
    if (!name.length) p.ctx.addError("A3D input: missing voxel type name");
    p.attr(type, "name", p.text(name));
    p.attr(type, "weights", bone_weights(p, &rest));
    ArrayBuilder inventory = p.ctx.builder.array();
    strview_trim(&rest);
    if (rest.length) {
        if (!is(token(p, &rest), "{")) p.ctx.addError("A3D input: expected inventory opening brace");
        bool closed = false;
        while ((strview_trim(&rest), rest.length)) {
            StrView count = token(p, &rest);
            if (is(count, "}")) { closed = true; break; }
            StrView item = token(p, &rest);
            if (!item.length || is(item, "}")) { p.ctx.addError("A3D input: missing inventory item"); break; }
            MapBuilder entry = p.ctx.builder.map();
            entry.put("count", nonnegative(p, count)).put("type", p.text(item));
            inventory.append(entry.final());
        }
        if (!closed) p.ctx.addError("A3D input: unterminated inventory");
        strview_trim(&rest);
        if (rest.length) p.ctx.addError("A3D input: excess inventory operands");
    }
    p.attr(type, "inventory", inventory.final());
    return type;
}

enum A3dChunk {
    A3D_PREVIEW, A3D_TEXTMAP, A3D_VERTEX, A3D_BONES, A3D_MATERIAL, A3D_PROCEDURAL,
    A3D_MESH, A3D_SHAPE, A3D_VOX_TYPES, A3D_VOXEL, A3D_LABELS, A3D_ACTION,
    A3D_ASSETS, A3D_EXTRA, A3D_UNKNOWN
};

static A3dChunk a3d_chunk(StrView keyword, const char** tag) {
    static const struct { const char* keyword; const char* tag; A3dChunk kind; } chunks[] = {
        {"Preview", "preview", A3D_PREVIEW}, {"Textmap", "textmap", A3D_TEXTMAP},
        {"Vertex", "vertices", A3D_VERTEX}, {"Bones", "bones", A3D_BONES},
        {"Material", "material", A3D_MATERIAL}, {"Procedural", "procedural", A3D_PROCEDURAL},
        {"Mesh", "mesh", A3D_MESH}, {"Shape", "shape", A3D_SHAPE},
        {"VoxTypes", "voxtypes", A3D_VOX_TYPES}, {"Voxel", "voxel", A3D_VOXEL},
        {"Labels", "labels", A3D_LABELS}, {"Action", "action", A3D_ACTION},
        {"Assets", "assets", A3D_ASSETS}, {"Extra", "extra", A3D_EXTRA}
    };
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        if (!is(keyword, chunks[i].keyword)) continue;
        *tag = chunks[i].tag;
        return chunks[i].kind;
    }
    *tag = "chunk";
    return A3D_UNKNOWN;
}

static void a3d_chunk_body(ModelText& p, Item chunk, A3dChunk kind, int64_t duration) {
    lam::ArrayList<int64_t> parents;
    int64_t bone_count = 0;
    Item frame = ItemNull, layer = ItemNull;
    int64_t previous_time = -1;
    int64_t dimensions[3] = {};
    int64_t rows = 0, layers = 0;
    StrBuf* extra = kind == A3D_EXTRA ? strbuf_new() : nullptr;
    StrView line;
    while (p.next_line(&line) && line.length && !p.ctx.shouldStopParsing()) {
        if (kind == A3D_TEXTMAP) p.append(chunk, numeric_array(p, line, 2, 2));
        else if (kind == A3D_VERTEX) p.append(chunk, a3d_vertex(p, line));
        else if (kind == A3D_BONES) {
            size_t depth = 0;
            while (depth < line.length && line.str[depth] == '/') depth++;
            line = {line.str + depth, line.length - depth};
            if (depth > parents.length()) p.ctx.addError("A3D input: skipped bone parent level");
            Item bone = p.element("bone");
            p.attr(bone, "position", nonnegative(p, token(p, &line)));
            p.attr(bone, "orientation", nonnegative(p, token(p, &line)));
            p.attr(bone, "parent", depth && depth <= parents.length()
                ? p.ctx.builder.createInt(parents[depth - 1]) : ItemNull);
            strview_trim(&line);
            if (!line.length) p.ctx.addError("A3D input: missing bone name");
            p.attr(bone, "name", rest_text(p, line));
            if (parents.length() > depth) parents.remove_range(depth, parents.length() - depth);
            parents.push_back(bone_count++);
            p.append(chunk, bone);
        } else if (kind == A3D_MATERIAL) {
            StrView name = token(p, &line);
            p.append(chunk, material_statement(p, name, line, true));
        } else if (kind == A3D_MESH) {
            StrView rest = line;
            StrView name = token(p, &rest);
            Item record;
            if (is(name, "use") || is(name, "par")) {
                record = p.element(name);
                strview_trim(&rest);
                p.append(record, rest.length ? rest_text(p, rest) : ItemNull);
            } else {
                record = p.element("face");
                corners(p, record, line, true, 4, 1, 15);
            }
            p.append(chunk, record);
        } else if (kind == A3D_ACTION) {
            StrView name = token(p, &line);
            if (is(name, "frame")) {
                Item time = nonnegative(p, token(p, &line));
                int64_t milliseconds = ItemReader(time.to_const()).asInt();
                strview_trim(&line);
                if (milliseconds < previous_time || milliseconds > duration || line.length)
                    p.ctx.addError("A3D input: invalid action frame time");
                previous_time = milliseconds;
                frame = p.element("frame");
                p.attr(frame, "time_ms", time);
                p.append(chunk, frame);
            } else {
                if (get_type_id(frame) == LMD_TYPE_NULL) {
                    p.ctx.addError("A3D input: pose precedes frame");
                    continue;
                }
                MapBuilder pose = p.ctx.builder.map();
                pose.put("bone", nonnegative(p, name));
                pose.put("position", nonnegative(p, token(p, &line)));
                pose.put("orientation", nonnegative(p, token(p, &line)));
                strview_trim(&line);
                if (line.length) p.ctx.addError("A3D input: excess pose operands");
                p.append(frame, pose.final());
            }
        } else if (kind == A3D_LABELS) {
            StrView name = token(p, &line);
            if (is(name, "color") || is(name, "lang")) {
                Item record = p.element(name);
                strview_trim(&line);
                if (!line.length) p.ctx.addError("A3D input: missing label property");
                if (is(name, "color")) color_code(p, line);
                p.append(record, rest_text(p, line));
                p.append(chunk, record);
            } else {
                Item label = p.element("label");
                p.attr(label, "vertex", nonnegative(p, name));
                p.append(label, rest_text(p, line));
                p.append(chunk, label);
            }
        } else if (kind == A3D_VOX_TYPES) p.append(chunk, a3d_voxel_type(p, line));
        else if (kind == A3D_VOXEL) {
            StrView rest = line;
            StrView name = token(p, &rest);
            if (is(name, "layer")) {
                if (layers && rows != dimensions[2]) p.ctx.addError("A3D input: wrong voxel layer depth");
                strview_trim(&rest);
                if (rest.length || dimensions[0] <= 0 || dimensions[1] <= 0 || dimensions[2] <= 0)
                    p.ctx.addError("A3D input: layer requires positive dimensions");
                layer = p.element("layer");
                p.append(chunk, layer);
                layers++;
                rows = 0;
            } else if (is(name, "pos") || is(name, "dim")) {
                Item values = numeric_array(p, rest, 3, 3, true);
                Item record = p.element(name);
                p.append(record, values);
                p.append(chunk, record);
                if (is(name, "dim")) {
                    ArrayReader array = ItemReader(values.to_const()).asArray();
                    for (size_t i = 0; i < 3; i++) dimensions[i] = array.get((int64_t)i).asInt();
                    if (layers) p.ctx.addError("A3D input: dimensions changed after a layer");
                }
            } else if (is(name, "uncertain")) {
                Item record = p.element("uncertain");
                p.append(record, numeric_array(p, rest, 1, 2));
                p.append(chunk, record);
            } else {
                if (get_type_id(layer) == LMD_TYPE_NULL) { p.ctx.addError("A3D input: voxel row precedes layer"); continue; }
                ArrayBuilder values = p.ctx.builder.array();
                int64_t width = 0;
                while ((strview_trim(&line), line.length)) {
                    StrView value = token(p, &line);
                    values.append(is(value, ".") ? p.ctx.builder.createInt(-1)
                        : is(value, "-") ? p.ctx.builder.createInt(-2) : nonnegative(p, value));
                    width++;
                }
                if (width != dimensions[0]) p.ctx.addError("A3D input: wrong voxel row width");
                p.append(layer, values.final());
                rows++;
            }
        } else if (kind == A3D_SHAPE) {
            StrView name = token(p, &line);
            Item record = p.element(name);
            if (is(name, "use")) {
                strview_trim(&line);
                p.append(record, line.length ? rest_text(p, line) : ItemNull);
            } else generic_arguments(p, record, line);
            p.append(chunk, record);
        } else if (kind == A3D_EXTRA) {
            while ((strview_trim(&line), line.length)) {
                StrView value = token(p, &line);
                if (value.length % 2) p.ctx.addError("A3D input: odd hexadecimal payload length");
                for (size_t i = 0; i + 1 < value.length; i += 2) {
                    unsigned char byte = 0;
                    if (!hex_decode(value.str + i, 2, &byte, nullptr))
                        p.ctx.addError("A3D input: invalid hexadecimal payload");
                    strbuf_append_char(extra, (char)byte);
                }
            }
        } else {
            // paths, procedural script references and unknown extension chunks
            // stay inert strings; their files are not read during parsing.
            Item record = p.element(kind == A3D_UNKNOWN ? "line" : "file");
            p.append(record, rest_text(p, line));
            p.append(chunk, record);
        }
    }
    if (kind == A3D_VOXEL && (rows != dimensions[2] || layers != dimensions[1]))
        p.ctx.addError("A3D input: voxel dimensions do not match the layers");
    if (extra) {
        // S2.2.2v2: a zero-length opaque chunk is childless, never an empty binary.
        if (extra->length) p.append(chunk, Item{.item = x2it(p.ctx.builder.createBinary(extra->str, extra->length))});
        strbuf_free(extra);
    }
}

static void ascii_model3d(ModelText& p) {
    Item root = p.element("a3d");
    StrView line;
    if (!p.next_line(&line) || !is(token(p, &line), "3dmodel")) {
        p.ctx.addError("A3D input: expected 3dmodel header; binary M3D is not supported");
        p.finish(root);
        return;
    }
    Item scale = number(p, token(p, &line));
    p.attr(root, "scale", scale);
    strview_trim(&line);
    if (ItemReader(scale.to_const()).asFloat() <= 0.0 || line.length) p.ctx.addError("A3D input: invalid model scale");
    const char* fields[] = {"name", "license", "author"};
    for (size_t i = 0; i < 3; i++) {
        if (!p.next_line(&line)) {
            p.ctx.addError("A3D input: incomplete header");
            line = {"", 0};
        }
        p.attr(root, fields[i], p.text(line));
    }
    StrBuf* description = strbuf_new();
    bool header_end = false;
    while (p.next_line(&line)) {
        if (!line.length) { header_end = true; break; }
        if (description->length) strbuf_append_char(description, '\n');
        strbuf_append_str_n(description, line.str, line.length);
    }
    p.attr(root, "description", p.ctx.builder.createStringItem(description->str, description->length));
    strbuf_free(description);
    if (!header_end) p.ctx.addError("A3D input: missing header terminator");
    bool ended = false;
    while (p.next_line(&line) && !p.ctx.shouldStopParsing()) {
        if (!line.length) continue;
        StrView keyword = token(p, &line);
        if (is(keyword, "End")) {
            strview_trim(&line);
            if (line.length) p.ctx.addError("A3D input: excess End operands");
            ended = true;
            break;
        }
        const char* tag;
        A3dChunk kind = a3d_chunk(keyword, &tag);
        Item chunk = p.element(tag);
        int64_t duration = 0;
        if (kind == A3D_ACTION) {
            Item value = nonnegative(p, token(p, &line));
            duration = ItemReader(value.to_const()).asInt();
            p.attr(chunk, "duration_ms", value);
        }
        strview_trim(&line);
        if (kind == A3D_UNKNOWN) {
            p.attr(chunk, "kind", p.text(keyword));
            p.ctx.addWarning("A3D input: preserving unknown chunk '%.*s'", (int)keyword.length, keyword.str);
        }
        if (line.length) p.attr(chunk, "name", rest_text(p, line));
        else if (kind == A3D_ACTION || kind == A3D_MATERIAL || kind == A3D_EXTRA)
            p.ctx.addError("A3D input: missing chunk name");
        if (kind == A3D_EXTRA) {
            bool valid = line.length == 4;
            for (size_t j = 0; valid && j < line.length; j++)
                valid = (unsigned char)line.str[j] > 32 && (unsigned char)line.str[j] < 127;
            if (!valid) p.ctx.addError("A3D input: Extra name must have four printable ASCII bytes");
        }
        a3d_chunk_body(p, chunk, kind, duration);
        p.append(root, chunk);
    }
    if (!ended) p.ctx.addError("A3D input: missing End chunk");
    while (p.next_line(&line)) if (line.length) p.ctx.addError("A3D input: content after End");
    p.finish(root);
}

static void gltf_document(ModelText& p) {
    bool ok = false;
    // JSON's existing parser owns the value mapping, including extensions and
    // extras. A bounded copy also permits non-NUL-terminated input spans.
    const char* source = p.ctx.source();
    if (p.ctx.source_length() >= 3 && memcmp(source, "\xef\xbb\xbf", 3) == 0) source += 3;
    Item root = parse_json_to_item_strict(p.ctx.input(), source, &ok);
    ItemReader value(root.to_const());
    if (!ok || !value.isMap()) p.ctx.addError("glTF input: expected one complete JSON object");
    else {
        ItemReader asset = value.asMap().get("asset");
        ItemReader version = asset.isMap() ? asset.asMap().get("version") : ItemReader(ItemNull.to_const());
        if (!version.isString() || !version.asString()->len)
            p.ctx.addError("glTF input: missing asset.version string");
    }
    p.finish(root);
}

} // namespace

bool input_parse_model(Input* input, const char* source, size_t length, const char* type) {
    typedef void (*Parser)(ModelText&);
    static const struct { const char* type; Parser parse; } parsers[] = {
        {"obj", wavefront_obj}, {"mtl", wavefront_mtl},
        {"gltf", gltf_document}, {"a3d", ascii_model3d}
    };
    for (size_t i = 0; i < sizeof(parsers) / sizeof(parsers[0]); i++) {
        if (strcmp(type, parsers[i].type) != 0) continue;
        if (!source) { source = ""; length = 0; }
        ModelText parser(input, source, length);
        // text model formats cannot contain NUL; reject rather than accepting
        // a valid prefix and silently dropping a binary or damaged suffix.
        if (memchr(source, '\0', length)) {
            parser.ctx.addError("model input: embedded NUL in textual input");
            parser.finish(ItemError);
        } else parsers[i].parse(parser);
        return true;
    }
    return false;
}
