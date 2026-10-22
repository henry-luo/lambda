// FBX profile 1 into the common <character> schema (vibe/Lambda_Pkg_Motion.md
// §7.2). The raw <fbx> record tree's object graph is resolved once; joints,
// rest poses, skin binds, body geometry and baked clips are then written in
// character coordinates: metres, Y-up, right-handed, +Z forward.
#include "input-character.hpp"
#include "../../lib/base64.h"
#include "../../lib/hashmap_typed.hpp"
#include "../../lib/strbuf.h"

namespace lambda {

namespace {

static constexpr double FBX_TICKS = 46186158000.0;  // KTime ticks per second
static constexpr size_t FBX_JOINT_LIMIT = 1024, FBX_CLIP_LIMIT = 1024;
static constexpr size_t FBX_VERTEX_LIMIT = 262144, FBX_CHANNEL_LIMIT = 8388608;
// KeyAttrFlags bits
static constexpr int64_t FBX_CONSTANT = 0x2, FBX_LINEAR = 0x4, FBX_CUBIC = 0x8, FBX_CONSTANT_NEXT = 0x100,
    FBX_TANGENT_TCB = 0x200, FBX_TANGENT_USER = 0x400, FBX_CLAMP = 0x1000, FBX_TIME_INDEPENDENT = 0x2000,
    FBX_CLAMP_PROGRESSIVE = 0x4000, FBX_WEIGHTED_RIGHT = 0x1000000, FBX_WEIGHTED_NEXT_LEFT = 0x2000000;
enum FbxMode : uint8_t { MODE_CONSTANT, MODE_CONSTANT_NEXT, MODE_LINEAR, MODE_CUBIC };

// ---- affine matrices: column-major, double precision -------------------------

struct FbxMat { double m[16]; };

static FbxMat fbx_identity() { FbxMat r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static FbxMat operator*(const FbxMat& a, const FbxMat& b) {
    FbxMat r;
    for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
        double s = 0;
        for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
    }
    return r;
}
static FbxMat fbx_translation(const double* t, double sign = 1) {
    FbxMat r = fbx_identity(); r.m[12] = sign * t[0]; r.m[13] = sign * t[1]; r.m[14] = sign * t[2]; return r;
}
static FbxMat fbx_scaling(const double* s) { FbxMat r = fbx_identity(); r.m[0] = s[0]; r.m[5] = s[1]; r.m[10] = s[2]; return r; }
static FbxMat fbx_rotation(const CharacterQuat& q) {
    FbxMat r = fbx_identity();
    double x = q.x, y = q.y, z = q.z, w = q.w;
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w); r.m[2] = 2 * (x * z - y * w);
    r.m[4] = 2 * (x * y - z * w); r.m[5] = 1 - 2 * (x * x + z * z); r.m[6] = 2 * (y * z + x * w);
    r.m[8] = 2 * (x * z + y * w); r.m[9] = 2 * (y * z - x * w); r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}
static double fbx_det3(const FbxMat& a) {
    const double* m = a.m;
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}
static bool fbx_inverse(const FbxMat& a, FbxMat* out) {
    double det = fbx_det3(a);
    if (!isfinite(det) || fabs(det) < 1e-300) return false;
    const double* m = a.m;
    FbxMat r = fbx_identity();
    r.m[0] = (m[5] * m[10] - m[6] * m[9]) / det; r.m[4] = (m[8] * m[6] - m[4] * m[10]) / det; r.m[8] = (m[4] * m[9] - m[8] * m[5]) / det;
    r.m[1] = (m[9] * m[2] - m[1] * m[10]) / det; r.m[5] = (m[0] * m[10] - m[8] * m[2]) / det; r.m[9] = (m[8] * m[1] - m[0] * m[9]) / det;
    r.m[2] = (m[1] * m[6] - m[5] * m[2]) / det; r.m[6] = (m[4] * m[2] - m[0] * m[6]) / det; r.m[10] = (m[0] * m[5] - m[4] * m[1]) / det;
    for (int row = 0; row < 3; row++)
        r.m[12 + row] = -(r.m[row] * m[12] + r.m[4 + row] * m[13] + r.m[8 + row] * m[14]);
    *out = r;
    return true;
}
static void fbx_point(const FbxMat& a, const double* p, double* out, bool direction = false) {
    double w = direction ? 0 : 1;
    for (int row = 0; row < 3; row++) out[row] = a.m[row] * p[0] + a.m[4 + row] * p[1] + a.m[8 + row] * p[2] + a.m[12 + row] * w;
}
static double fbx_difference(const FbxMat& a, const FbxMat& b) {
    double scale = 1, worst = 0;
    for (int i = 0; i < 16; i++) scale = fmax(scale, fabs(a.m[i]));
    for (int i = 0; i < 16; i++) worst = fmax(worst, fabs(a.m[i] - b.m[i]));
    return worst / scale;
}

// FBX names rotation orders by application: XYZ is Rz · Ry · Rx
static const char* const FBX_ORDERS[] = {"XYZ", "XZY", "YZX", "YXZ", "ZXY", "ZYX"};
static CharacterQuat fbx_euler(const double* degrees, int order) {
    CharacterQuat q = {0, 0, 0, 1};
    for (int i = 0; i < 3; i++) {
        unsigned axis = (unsigned)(FBX_ORDERS[order][i] - 'X');
        q = character_quat_mul(character_quat_axis(axis, degrees[axis]), q);
    }
    return q;
}
// a rotation matrix (r[row][column]) as a unit quaternion
static CharacterQuat fbx_quat(double r[3][3]) {
    double trace = r[0][0] + r[1][1] + r[2][2];
    CharacterQuat q;
    if (trace > 0) {
        double s = sqrt(trace + 1) * 2;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, s / 4};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        double s = sqrt(1 + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {s / 4, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        double s = sqrt(1 + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][1] + r[1][0]) / s, s / 4, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        double s = sqrt(1 + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, s / 4, (r[1][0] - r[0][1]) / s};
    }
    return character_quat_normalized(q);
}
// translation, rotation and scale of an affine matrix; a reflection becomes a
// negative x scale, and shear beyond tolerance has no TRS form
static bool fbx_decompose(const FbxMat& a, double* t, CharacterQuat* q, double* s) {
    double axes[3][3];
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) axes[c][row] = a.m[c * 4 + row];
        s[c] = sqrt(axes[c][0] * axes[c][0] + axes[c][1] * axes[c][1] + axes[c][2] * axes[c][2]);
        if (!(s[c] > 1e-12)) return false;
        for (int row = 0; row < 3; row++) axes[c][row] /= s[c];
    }
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) {
        double dot = axes[i][0] * axes[j][0] + axes[i][1] * axes[j][1] + axes[i][2] * axes[j][2];
        if (fabs(dot) > 1e-5) return false;
    }
    if (fbx_det3(a) < 0) { s[0] = -s[0]; for (int row = 0; row < 3; row++) axes[0][row] = -axes[0][row]; }
    double r[3][3];
    for (int row = 0; row < 3; row++) for (int c = 0; c < 3; c++) r[row][c] = axes[c][row];
    *q = fbx_quat(r);
    for (int row = 0; row < 3; row++) t[row] = a.m[12 + row];
    return true;
}

// ---- raw tree access -----------------------------------------------------------

static ArrayReader fbx_props(ElementReader record) {
    ItemReader props = record.get_attr("props");
    return props.isArray() ? props.asArray() : ArrayReader();
}
// a record's one numeric array property (`Vertices: *N {…}`)
static bool fbx_numbers(ElementReader record, lam::ArrayList<double>& out) {
    out.clear();
    ArrayReader props = fbx_props(record);
    if (props.length() < 1) return false;
    ItemReader first = props.get(0);
    if (first.getType() == LMD_TYPE_ARRAY_NUM) {
        ArrayReader values = first.asArray();
        for (int64_t i = 0; i < values.length(); i++) out.append(asset_number(values.get(i)));
        return true;
    }
    // ASCII writes a one-element array as a lone value
    for (int64_t i = 0; i < props.length(); i++) {
        double number = asset_number(props.get(i));
        if (!isfinite(number)) return false;
        out.append(number);
    }
    return true;
}
static bool fbx_child_numbers(ElementReader record, const char* tag, lam::ArrayList<double>& out) {
    // a missing record reads as empty: callers reuse one array across records, and Mixamo
    // writes a cluster for every bone in every mesh, most of them without Indexes or Weights
    out.clear();
    ElementReader child = record.findChildElement(tag);
    return fbx_props(child).length() && fbx_numbers(child, out);
}
static const char* fbx_child_string(ElementReader record, const char* tag) {
    ArrayReader props = fbx_props(record.findChildElement(tag));
    return props.length() ? props.get(0).cstring() : nullptr;
}
// the values of the Properties70 entry `key`, after its four header strings
static bool fbx_values(ElementReader object, const char* key, double* out, unsigned count) {
    ElementReader table = object.findChildElement("Properties70");
    for (int64_t i = 0; i < table.childCount(); i++) {
        ArrayReader values = fbx_props(table.childAt(i).asElement());
        const char* name = values.length() ? values.get(0).cstring() : nullptr;
        if (!name || strcmp(name, key)) continue;
        if (values.length() < 4 + (int64_t)count) return false;
        for (unsigned c = 0; c < count; c++) {
            out[c] = asset_number(values.get(4 + c));
            if (!isfinite(out[c])) return false;
        }
        return true;
    }
    return false;
}
static double fbx_value(ElementReader object, const char* key, double fallback) {
    double value;
    return fbx_values(object, key, &value, 1) ? value : fallback;
}
static void fbx_vector(ElementReader object, const char* key, double* out, double fallback) {
    if (!fbx_values(object, key, out, 3)) out[0] = out[1] = out[2] = fallback;
}
static bool fbx_matrix(ElementReader record, const char* tag, FbxMat* out) {
    lam::ArrayList<double> values;
    if (!fbx_child_numbers(record, tag, values) || values.size() != 16) return false;
    for (int i = 0; i < 16; i++) out->m[i] = values[i];
    return true;
}

// ---- the object graph ------------------------------------------------------------

struct FbxObject {
    int64_t id;
    ElementReader node;
    const char* kind;      // record tag: Model, Geometry, Deformer, …
    const char* name;      // object name without its class
    const char* subclass;  // third property: LimbNode, Mesh, Skin, Cluster, …
    int sources = -1, sources_tail = -1;  // links whose parent is this object
    int targets = -1, targets_tail = -1;  // links whose child is this object
    int parent = -1;       // the Model this Model hangs under
    int joint = -1;        // joint index in skeleton preorder
    int material = -1;     // body material slot, once created
};
struct FbxLink { int child, parent; const char* property; int next_source, next_target; };
struct FbxIdEntry { int64_t id; int index; };
typedef TypedHashMap<FbxIdEntry, HashMapIntegralMemberKeyOps<FbxIdEntry, &FbxIdEntry::id>> FbxIdMap;

// a joint's static transform chain; T, R and S are the animatable values
struct FbxNode {
    double t[3], r[3], s[3], roff[3], rp[3], pre[3], post[3], soff[3], sp[3];
    int order, inherit;
};
struct FbxCurve { size_t first, count; };
// one joint's animated Lcl property: up to three curves, else fallbacks
struct FbxTrack { int joint, property; int curve[3]; double fallback[3]; };

struct FbxCharacter {
    SceneAsset& a;
    ElementReader root;
    lam::ArrayList<FbxObject> objects;
    lam::ArrayList<FbxLink> links;
    FbxIdMap ids = {};
    FbxMat to_character = fbx_identity(), from_character = fbx_identity();
    bool mirrored = false;
    double rate = 30;
    lam::ArrayList<int> joints;          // object index per joint, preorder
    lam::ArrayList<FbxNode> nodes;       // per joint
    lam::ArrayList<FbxMat> rest_world;   // file-space world per joint at rest
    lam::ArrayList<FbxMat> bind;         // character-space inverse bind per joint
    lam::ArrayList<char> bound;          // bind set by a skin cluster
    bool skinned = false;
    Item resources = ItemNull;
    lam::ArrayList<double> key_time, key_value, key_right, key_left;  // a key's outgoing and incoming slopes
    lam::ArrayList<double> key_reach_right, key_reach_left;  // their weights: fractions of the segment, 1/3 unweighted
    lam::ArrayList<uint8_t> key_mode;
    lam::ArrayList<Item> material_ids;  // per FBX material, once created
    Item default_material = ItemNull;

    FbxCharacter(SceneAsset& asset, Item raw) : a(asset), root(raw) {}
    ~FbxCharacter() { ids.destroy(); }

    bool fail(const char* message) { a.ctx.addError("character: FBX %s", message); return false; }
    bool fail_named(const char* message, const char* name) {
        a.ctx.addError("character: FBX %s '%s'", message, name ? name : "");
        return false;
    }
    int find(int64_t id) {
        FbxIdEntry key = {id, -1};
        const FbxIdEntry* entry = ids.get(key);
        return entry ? entry->index : -1;
    }
    bool is(int object, const char* kind) const { return object >= 0 && asset_is(objects[object].kind, kind); }
    bool sub(int object, const char* subclass) const { return object >= 0 && asset_is(objects[object].subclass, subclass); }
    // the first object of `kind` linked into `object`, optionally through `property`
    int source(int object, const char* kind, const char* property = nullptr, int after = -1) {
        for (int l = after >= 0 ? links[after].next_source : objects[object].sources; l >= 0; l = links[l].next_source) {
            if (is(links[l].child, kind) && (!property || asset_is(links[l].property, property))) return l;
        }
        return -1;
    }

    bool index_objects() {
        ElementReader table = root.findChildElement("Objects");
        if (!ids.init(1024)) return fail("object table allocation failed");
        for (int64_t i = 0; i < table.childCount(); i++) {
            ElementReader node = table.childAt(i).asElement();
            ArrayReader props = fbx_props(node);
            if (props.length() < 2 || !props.get(0).isInt()) return fail("object without an ID");
            const char* full = props.get(1).cstring();
            const char* split = full ? strstr(full, "::") : nullptr;
            FbxObject object = {};
            object.id = props.get(0).asInt();
            object.node = node;
            object.kind = node.tagName();
            object.name = split ? split + 2 : full ? full : "";
            object.subclass = props.length() > 2 ? props.get(2).cstring() : nullptr;
            FbxIdEntry entry = {object.id, (int)objects.size()};  // INT_CAST_OK: object count is bounded by the raw tree
            if (ids.get(entry)) return fail("object IDs must be unique");
            ids.set(entry);
            objects.append(object);
        }
        if (ids.oom()) return fail("object table allocation failed");
        return true;
    }

    bool index_links() {
        ElementReader table = root.findChildElement("Connections");
        for (int64_t i = 0; i < table.childCount(); i++) {
            ArrayReader props = fbx_props(table.childAt(i).asElement());
            const char* type = props.length() >= 3 ? props.get(0).cstring() : nullptr;
            if (!type || (strcmp(type, "OO") && strcmp(type, "OP"))) {
                if (type && (!strcmp(type, "PO") || !strcmp(type, "PP"))) continue;  // property-sourced links carry no objects here
                return fail("connection is not OO or OP");
            }
            int64_t child_id = props.get(1).asInt(), parent_id = props.get(2).asInt();
            int child = find(child_id), parent = parent_id ? find(parent_id) : -1;
            if (child < 0 || (parent_id && parent < 0)) return fail("connection names a missing object");
            if (parent < 0) continue;  // the scene root
            FbxLink link = {child, parent, props.length() > 3 ? props.get(3).cstring() : nullptr, -1, -1};
            int index = (int)links.size();  // INT_CAST_OK: link count is bounded by the raw tree
            links.append(link);
            FbxObject& p = objects[parent];
            if (p.sources_tail >= 0) links[p.sources_tail].next_source = index; else p.sources = index;
            p.sources_tail = index;
            FbxObject& c = objects[child];
            if (c.targets_tail >= 0) links[c.targets_tail].next_target = index; else c.targets = index;
            c.targets_tail = index;
            if (is(child, "Model") && is(parent, "Model")) {
                if (c.parent >= 0) return fail_named("model has two parents", c.name);
                c.parent = parent;
            }
        }
        // model parents must form a forest
        for (size_t i = 0; i < objects.size(); i++) {
            int depth = 0;
            for (int p = objects[i].parent; p >= 0; p = objects[p].parent)
                if (++depth > 64) return fail_named("model hierarchy is cyclic or deeper than 64 at", objects[i].name);
        }
        return true;
    }

    // GlobalSettings axes and units become one basis-and-scale into character space
    bool axes() {
        ElementReader settings = root.findChildElement("GlobalSettings");
        int up = (int)fbx_value(settings, "UpAxis", 1), front = (int)fbx_value(settings, "FrontAxis", 2),  // INT_CAST_OK: axis enum
            coord = (int)fbx_value(settings, "CoordAxis", 0);  // INT_CAST_OK: axis enum
        double up_sign = fbx_value(settings, "UpAxisSign", 1), front_sign = fbx_value(settings, "FrontAxisSign", 1),
            coord_sign = fbx_value(settings, "CoordAxisSign", 1);
        double unit = fbx_value(settings, "UnitScaleFactor", 1);
        if (up < 0 || up > 2 || front < 0 || front > 2 || coord < 0 || coord > 2 || up == front || up == coord || front == coord)
            return fail("GlobalSettings axes are not a permutation of X, Y and Z");
        if (!(unit > 0) || !isfinite(unit)) return fail("UnitScaleFactor must be positive");
        double scale = unit / 100;  // centimetres per unit to metres
        FbxMat c = {};
        int axis_of[3] = {coord, up, front};
        double sign[3] = {coord_sign < 0 ? -1.0 : 1.0, up_sign < 0 ? -1.0 : 1.0, front_sign < 0 ? -1.0 : 1.0};
        for (int row = 0; row < 3; row++) c.m[axis_of[row] * 4 + row] = sign[row] * scale;
        c.m[15] = 1;
        to_character = c;
        mirrored = fbx_det3(c) < 0;
        if (!fbx_inverse(c, &from_character)) return fail("GlobalSettings axes are singular");
        // TimeMode names the frame rate cubic segments are sampled at
        static const double modes[] = {24, 120, 100, 60, 50, 48, 30, 30, 30 / 1.001, 30 / 1.001, 25, 24, 1000,
            24 / 1.001, 0, 96, 72, 60 / 1.001, 120 / 1.001};
        int mode = (int)fbx_value(settings, "TimeMode", 6);  // INT_CAST_OK: time-mode enum
        rate = mode >= 0 && mode < 19 ? modes[mode] : 30;
        if (mode == 14) rate = fbx_value(settings, "CustomFrameRate", 30);
        if (!(rate > 0) || rate > 1000) return fail("frame rate must lie in (0, 1000]");
        return true;
    }
    // a file-space transform expressed in character space
    FbxMat character(const FbxMat& m) const { return to_character * m * from_character; }

    bool node(int object, FbxNode* n) {
        ElementReader model = objects[object].node;
        fbx_vector(model, "Lcl Translation", n->t, 0); fbx_vector(model, "Lcl Rotation", n->r, 0);
        fbx_vector(model, "Lcl Scaling", n->s, 1);
        fbx_vector(model, "RotationOffset", n->roff, 0); fbx_vector(model, "RotationPivot", n->rp, 0);
        fbx_vector(model, "PreRotation", n->pre, 0); fbx_vector(model, "PostRotation", n->post, 0);
        fbx_vector(model, "ScalingOffset", n->soff, 0); fbx_vector(model, "ScalingPivot", n->sp, 0);
        double order = fbx_value(model, "RotationOrder", 0), inherit = fbx_value(model, "InheritType", 1);
        if (order < 0 || order > 5) return fail_named("rotation order outside XYZ..ZYX (spheric XYZ is unsupported) on", objects[object].name);
        if (inherit != 0 && inherit != 1 && inherit != 2) return fail_named("InheritType outside RrSs, RSrs and Rrs (0-2) on", objects[object].name);
        n->order = (int)order;  // INT_CAST_OK: rotation-order enum
        n->inherit = (int)inherit;  // INT_CAST_OK: inherit-type enum
        return true;
    }
    // InheritType RrSs (0) and Rrs (2) equal ordinary composition (RSrs, 1) while the immediate
    // parent's local scale is uniform, as ufbx and the FBX SDK evaluate them; Rrs then cancels
    // that scale (Maya's segment scale compensation). A non-uniform parent scale has no exact
    // ordinary-hierarchy form and is refused.
    bool inherited(const FbxNode& n, int object, const double* parent_scale, FbxMat* m) {
        if (n.inherit == 1) return true;
        double big = fmax(fabs(parent_scale[0]), fmax(fabs(parent_scale[1]), fabs(parent_scale[2])));
        if (!(big > 0) || fabs(parent_scale[0] - parent_scale[1]) > 1e-6 * big || fabs(parent_scale[0] - parent_scale[2]) > 1e-6 * big)
            return fail_named("non-uniform parent scale under InheritType RrSs or Rrs on", objects[object].name);
        if (n.inherit == 2) for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) m->m[i * 4 + k] /= parent_scale[0];
        return true;
    }
    // T · Roff · Rp · Rpre · R · Rpost⁻¹ · Rp⁻¹ · Soff · Sp · S · Sp⁻¹
    static FbxMat local(const FbxNode& n, const double* t, const double* r, const double* s) {
        FbxMat post = fbx_rotation(fbx_euler(n.post, 0)), post_inverse = fbx_identity();
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) post_inverse.m[i * 4 + j] = post.m[j * 4 + i];
        return fbx_translation(t) * fbx_translation(n.roff) * fbx_translation(n.rp) * fbx_rotation(fbx_euler(n.pre, 0)) *
            fbx_rotation(fbx_euler(r, n.order)) * post_inverse * fbx_translation(n.rp, -1) * fbx_translation(n.soff) *
            fbx_translation(n.sp) * fbx_scaling(s) * fbx_translation(n.sp, -1);
    }
    // a model's file-space world matrix at rest
    bool world(int object, FbxMat* out) {
        FbxNode n;
        if (!node(object, &n)) return false;
        FbxMat m = local(n, n.t, n.r, n.s);
        int up = objects[object].parent;
        if (up >= 0) {
            FbxNode p;
            FbxMat parent;
            if (!node(up, &p) || !inherited(n, object, p.s, &m) || !world(up, &parent)) return false;
            m = parent * m;
        }
        *out = m;
        return true;
    }
    // a joint's rest local matrix, with its parent's rest scale applied by inheritance
    bool rest_local(size_t j, FbxMat* m) {
        const FbxNode& n = nodes[j];
        *m = local(n, n.t, n.r, n.s);
        int up = objects[joints[j]].parent;
        return up < 0 || inherited(n, joints[j], nodes[objects[up].joint].s, m);
    }

    // ---- joints ---------------------------------------------------------------------

    void visit(int object, lam::ArrayList<int>& order) {
        objects[object].joint = (int)order.size();  // INT_CAST_OK: joint count is capped below
        order.append(object);
        for (int l = objects[object].sources; l >= 0; l = links[l].next_source) {
            int child = links[l].child;
            if (is(child, "Model") && objects[child].parent == object && objects[child].joint == -2) visit(child, order);
        }
    }
    // LimbNodes and skin-cluster links, with every ancestor model, in preorder
    bool select_joints() {
        auto wanted = [&](int object) {
            for (int m = object; m >= 0 && objects[m].joint != -2; m = objects[m].parent) objects[m].joint = -2;
        };
        bool skeleton = false, animated = false;
        for (size_t i = 0; i < objects.size(); i++) {
            int object = (int)i;  // INT_CAST_OK: object index
            if (is(object, "Model") && sub(object, "LimbNode")) { wanted(object); skeleton = true; }
            else if (is(object, "Deformer") && sub(object, "Cluster")) {
                int l = source(object, "Model");
                if (l >= 0) { wanted(links[l].child); skeleton = true; }
            }
            animated |= is(object, "Model") && source(object, "AnimationCurveNode") >= 0;
        }
        // with neither but with animated models, the file is a rigid rig (Kenney's Cube Pets): every
        // Null, Root and Mesh model is a joint, and each mesh rides its own as a rigid prop (§7.2)
        if (!skeleton && animated) {
            for (size_t i = 0; i < objects.size(); i++) {
                int object = (int)i;  // INT_CAST_OK: object index
                if (is(object, "Model") && (sub(object, "Null") || sub(object, "Root") || sub(object, "Mesh"))) wanted(object);
            }
        }
        for (size_t i = 0; i < objects.size(); i++) {
            if (objects[i].joint == -2 && objects[i].parent < 0) visit((int)i, joints);  // INT_CAST_OK: object index
        }
        if (joints.empty()) return fail("file has no skeleton (no LimbNode models, skin clusters or animated models)");
        if (joints.size() > FBX_JOINT_LIMIT) return fail("skeleton exceeds 1024 joints");
        for (size_t j = 0; j < joints.size(); j++) {
            for (size_t k = 0; k < j; k++)
                if (!strcmp(objects[joints[j]].name, objects[joints[k]].name)) return fail_named("duplicate joint name", objects[joints[j]].name);
            FbxNode n;
            FbxMat w;
            if (!node(joints[j], &n) || !world(joints[j], &w)) return false;
            nodes.append(n);
            rest_world.append(w);
            FbxMat inverse;
            if (!fbx_inverse(character(w), &inverse)) return fail_named("joint has a singular rest transform", objects[joints[j]].name);
            bind.append(inverse);
            bound.append(0);
        }
        return true;
    }

    Item joint_element(size_t j) {
        double t[3], s[3];
        CharacterQuat q;
        FbxMat rest;
        if (!rest_local(j, &rest)) return ItemNull;
        if (!fbx_decompose(character(rest), t, &q, s)) {
            fail_named("joint rest transform has shear", objects[joints[j]].name);
            return ItemNull;
        }
        Item out = a.element("joint");
        double quaternion[4] = {q.x, q.y, q.z, q.w};
        a.attr(out, "name", a.text(objects[joints[j]].name));
        a.attr(out, "position", a.array(t, 3));
        a.attr(out, "quaternion", a.array(quaternion, 4));
        if (fabs(s[0] - 1) > 1e-9 || fabs(s[1] - 1) > 1e-9 || fabs(s[2] - 1) > 1e-9) a.attr(out, "scale", a.array(s, 3));
        if (skinned) a.attr(out, "inverse_bind", a.array(bind[j].m, 16));
        int object = joints[j];
        for (int l = objects[object].sources; l >= 0; l = links[l].next_source) {
            int child = links[l].child;
            if (objects[child].joint >= 0 && objects[child].parent == object) {
                Item element = joint_element((size_t)objects[child].joint);
                if (get_type_id(element) == LMD_TYPE_NULL) return ItemNull;
                a.append(out, element);
            }
        }
        return out;
    }

    // ---- body --------------------------------------------------------------------------

    // a geometry layer element's values for one polygon corner
    struct Layer {
        lam::ArrayList<double> data, index;
        unsigned width = 0;
        int mapping = -1;  // 0 polygon vertex, 1 control point, 2 polygon, 3 all same
        bool indexed = false;
    };
    bool layer(ElementReader geometry, const char* tag, const char* values, const char* indices, unsigned width, Layer* out) {
        ElementReader element;
        int count = 0;
        for (int64_t i = 0; i < geometry.childCount(); i++) {
            ElementReader child = geometry.childAt(i).asElement();
            if (!child.hasTag(tag)) continue;
            if (!count++) element = child;
        }
        if (!count) return true;
        if (count > 1) a.ctx.addWarning("character: FBX %s layers beyond the first are omitted", tag);
        const char* mapping = fbx_child_string(element, "MappingInformationType");
        const char* reference = fbx_child_string(element, "ReferenceInformationType");
        out->mapping = asset_is(mapping, "ByPolygonVertex") ? 0 :
            asset_is(mapping, "ByControlPoint") || asset_is(mapping, "ByVertice") || asset_is(mapping, "ByVertex") ? 1 :
            asset_is(mapping, "ByPolygon") ? 2 : asset_is(mapping, "AllSame") ? 3 : -1;
        if (out->mapping < 0) return fail_named("layer mapping is not supported:", mapping);
        out->indexed = asset_is(reference, "IndexToDirect") || asset_is(reference, "Index");
        if (!out->indexed && !asset_is(reference, "Direct")) return fail_named("layer reference is not supported:", reference);
        out->width = width;
        if (!fbx_child_numbers(element, values, out->data) || out->data.size() % width) return fail_named("layer data is malformed in", tag);
        if (out->indexed && !fbx_child_numbers(element, indices, out->index)) return fail_named("indexed layer has no index array in", tag);
        return true;
    }
    // the row of `l` used by one corner, or -1 when out of range
    static int64_t row(const Layer& l, size_t corner, size_t point, size_t polygon) {
        size_t i = l.mapping == 0 ? corner : l.mapping == 1 ? point : l.mapping == 2 ? polygon : 0;
        if (l.indexed) {
            if (i >= l.index.size()) return -1;
            double r = l.index[i];
            i = r >= 0 ? (size_t)r : SIZE_MAX;
        }
        return i < l.data.size() / l.width ? (int64_t)i : -1;
    }

    // srgb FBX colors through the shared linear-to-hex path
    Item color(const double* srgb) {
        double linear[3];
        for (int c = 0; c < 3; c++) linear[c] = srgb[c] <= 0.04045 ? srgb[c] / 12.92 : pow((srgb[c] + 0.055) / 1.055, 2.4);
        return asset_color(a, linear);
    }
    // an embedded Video's bytes become a data URI; an external file resolves against the FBX URL
    Item texture_for(int texture) {
        int video_link = source(texture, "Video");
        ElementReader video = video_link >= 0 ? objects[links[video_link].child].node : ElementReader();
        const char* uri = nullptr;
        StrBuf* data = strbuf_new();
        ArrayReader content = fbx_props(video.findChildElement("Content"));
        const uint8_t* bytes = nullptr;
        size_t length = 0;
        uint8_t* decoded = nullptr;
        StrBuf* text = strbuf_new();
        for (int64_t i = 0; i < content.length(); i++) {
            ItemReader part = content.get(i);
            if (part.getType() == LMD_TYPE_BINARY) {
                Binary* binary = part.item().get_binary();
                bytes = binary_data(binary);
                length = binary_length(binary);
            } else if (part.cstring()) strbuf_append_str(text, part.cstring());  // ASCII embeds base64 text
        }
        if (!bytes && text->length) {
            decoded = base64_decode(text->str, text->length, &length);
            bytes = decoded;
        }
        if (bytes && length) {
            const char* mime = length > 8 && !memcmp(bytes, "\x89PNG", 4) ? "image/png" :
                length > 3 && bytes[0] == 0xff && bytes[1] == 0xd8 ? "image/jpeg" : nullptr;
            if (!mime) a.ctx.addWarning("character: FBX embedded texture is neither PNG nor JPEG; trying its file name");
            else {
                char* encoded = text->length ? nullptr : base64_encode_alloc(bytes, length, BASE64_STD);
                strbuf_append_str(data, "data:"); strbuf_append_str(data, mime); strbuf_append_str(data, ";base64,");
                strbuf_append_str(data, encoded ? encoded : text->str);
                if (encoded) mem_free(encoded);
                uri = data->str;
            }
        }
        if (decoded) mem_free(decoded);
        strbuf_free(text);
        ElementReader node = objects[texture].node;
        if (!uri) uri = fbx_child_string(node, "RelativeFilename");
        if (!uri) uri = fbx_child_string(node, "FileName");
        if (!uri) uri = fbx_child_string(video, "RelativeFilename");
        // file names keep the writer's separators: Windows-written files (Kenney's Cube Pets) use '\'
        StrBuf* path = strbuf_new();
        if (uri && uri != data->str) {
            strbuf_append_str(path, uri);
            for (size_t i = 0; i < path->length; i++) if (path->str[i] == '\\') path->str[i] = '/';
            uri = path->str;
        }
        Item reference = ItemNull, element = ItemNull;
        if (uri) reference = a.texture(uri, nullptr, &element);
        else fail_named("texture has neither embedded content nor a file name:", objects[texture].name);
        strbuf_free(path);
        strbuf_free(data);
        return reference;
    }
    // a Lambert/Phong material's diffuse color, opacity and diffuse texture
    Item material_for(int object) {
        if (object >= 0 && objects[object].material >= 0) return material_ids[(size_t)objects[object].material];
        if (object < 0 && get_type_id(default_material) != LMD_TYPE_NULL) return default_material;
        double diffuse[3] = {0.8, 0.8, 0.8};
        if (object >= 0) {
            ElementReader node = objects[object].node;
            if (!fbx_values(node, "DiffuseColor", diffuse, 3)) fbx_values(node, "Diffuse", diffuse, 3);
            double factor = fbx_value(node, "DiffuseFactor", 1);
            for (int c = 0; c < 3; c++) diffuse[c] = fmin(1, fmax(0, diffuse[c] * factor));
        }
        Item material = a.material(asset_read(color(diffuse)).cstring(), "lambert");
        if (object >= 0) {
            a.attr(material, "name", a.text(objects[object].name));
            double opacity = fbx_value(objects[object].node, "Opacity", 1);
            if (opacity < 1 && opacity >= 0) a.attr(material, "opacity", a.number(opacity));
            for (int l = objects[object].sources; l >= 0; l = links[l].next_source) {
                if (!is(links[l].child, "Texture")) continue;
                if (!asset_is(links[l].property, "DiffuseColor")) {
                    // Blender writes a plain OO link beside the DiffuseColor one; only an unused texture is omitted
                    bool diffuse_too = false;
                    for (int k = objects[object].sources; k >= 0; k = links[k].next_source)
                        diffuse_too |= links[k].child == links[l].child && asset_is(links[k].property, "DiffuseColor");
                    if (!diffuse_too) a.ctx.addWarning("character: FBX %s textures are omitted", links[l].property ? links[l].property : "unnamed");
                    continue;
                }
                Item texture = texture_for(links[l].child);
                if (get_type_id(texture) != LMD_TYPE_NULL) a.attr(material, "texture", texture);
            }
        }
        Item id = ElementReader(material).get_attr("id").item();
        if (object < 0) default_material = id;
        else { objects[object].material = (int)material_ids.size(); material_ids.append(id); }  // INT_CAST_OK: material count
        return id;
    }

    struct Corner { int point; int64_t normal, uv, color; };
    // one <mesh> per material slot; `skin` holds four (joint, weight) pairs per control point
    bool mesh(Item body, int model, ElementReader geometry, const FbxMat& placement, const lam::ArrayList<double>& skin) {
        lam::ArrayList<double> points, polygon_index, materials_of;
        if (!fbx_child_numbers(geometry, "Vertices", points) || points.size() % 3) return fail_named("mesh has malformed Vertices:", objects[model].name);
        if (!fbx_child_numbers(geometry, "PolygonVertexIndex", polygon_index)) return fail_named("mesh has no PolygonVertexIndex:", objects[model].name);
        size_t point_count = points.size() / 3;
        Layer normals, uvs, colors, materials;
        if (!layer(geometry, "LayerElementNormal", "Normals", "NormalsIndex", 3, &normals) ||
            !layer(geometry, "LayerElementUV", "UV", "UVIndex", 2, &uvs) ||
            !layer(geometry, "LayerElementColor", "Colors", "ColorIndex", 4, &colors)) return false;
        // material indices are the layer's data itself
        ElementReader material_layer = geometry.findChildElement("LayerElementMaterial");
        if (material_layer.childCount()) fbx_child_numbers(material_layer, "Materials", materials_of);
        bool all_same = asset_is(fbx_child_string(material_layer, "MappingInformationType"), "AllSame");
        // the model's material slots in connection order
        lam::ArrayList<int> slots;
        for (int l = objects[model].sources; l >= 0; l = links[l].next_source)
            if (is(links[l].child, "Material")) slots.append(links[l].child);
        FbxMat normal_matrix;
        if (!fbx_inverse(placement, &normal_matrix)) return fail_named("mesh placement is singular:", objects[model].name);
        double transformed[3];
        lam::ArrayList<double> world_points;
        for (size_t i = 0; i < point_count; i++) {
            fbx_point(placement, points.data() + i * 3, transformed);
            for (int c = 0; c < 3; c++) world_points.append(transformed[c]);
        }
        size_t slot_count = slots.empty() ? 1 : slots.size();
        for (size_t slot = 0; slot < slot_count; slot++) {
            lam::ArrayList<Corner> corners;    // unwelded vertices of this slot
            lam::ArrayList<int> first, next;   // per control point: its variants
            lam::ArrayList<double> indices, polygon;
            lam::ArrayList<unsigned> triangles;
            lam::ArrayList<int> polygon_vertices;
            for (size_t i = 0; i < point_count; i++) first.append(-1);
            size_t start = 0, polygon_number = 0;
            for (size_t corner = 0; corner < polygon_index.size(); corner++) {
                if (polygon_index[corner] >= 0) continue;
                size_t end = corner + 1;
                double which = materials_of.empty() ? 0 : materials_of[all_same ? 0 : (polygon_number < materials_of.size() ? polygon_number : 0)];
                bool keep = slots.empty() || (size_t)which == slot;
                if (!slots.empty() && (which < 0 || (size_t)which >= slots.size())) return fail_named("polygon material index out of range in", objects[model].name);
                if (keep) {
                    polygon.clear(); polygon_vertices.clear(); triangles.clear();
                    for (size_t k = start; k < end; k++) {
                        double raw = polygon_index[k];
                        int64_t point = raw < 0 ? -(int64_t)raw - 1 : (int64_t)raw;
                        if (point < 0 || (size_t)point >= point_count) return fail_named("polygon vertex index out of range in", objects[model].name);
                        Corner c = {(int)point, normals.mapping >= 0 ? row(normals, k, point, polygon_number) : 0,
                            uvs.mapping >= 0 ? row(uvs, k, point, polygon_number) : 0,
                            colors.mapping >= 0 ? row(colors, k, point, polygon_number) : 0};
                        if (c.normal < 0 || c.uv < 0 || c.color < 0) return fail_named("layer index out of range in", objects[model].name);
                        int found = -1;
                        for (int v = first[point]; v >= 0; v = next[v]) {
                            const Corner& o = corners[v];
                            if (o.normal == c.normal && o.uv == c.uv && o.color == c.color) { found = v; break; }
                        }
                        if (found < 0) {
                            found = (int)corners.size();  // INT_CAST_OK: vertex count is capped below
                            corners.append(c); next.append(first[point]); first[point] = found;
                        }
                        polygon_vertices.append(found);
                        for (int axis = 0; axis < 3; axis++) polygon.append(world_points[point * 3 + axis]);
                    }
                    size_t count = end - start;
                    if (count < 3) return fail_named("polygon with fewer than three corners in", objects[model].name);
                    if (count == 3) { triangles.append(0); triangles.append(1); triangles.append(2); }
                    else if (!asset_triangulate(a, polygon.data(), count, triangles)) return false;
                    for (size_t k = 0; k < triangles.size(); k += 3) {
                        unsigned order[3] = {triangles[k], triangles[k + 1], triangles[k + 2]};
                        if (mirrored) { unsigned swap = order[1]; order[1] = order[2]; order[2] = swap; }
                        for (unsigned o : order) indices.append(polygon_vertices[o]);
                    }
                }
                start = end;
                polygon_number++;
            }
            if (start != polygon_index.size()) return fail_named("PolygonVertexIndex does not end its last polygon in", objects[model].name);
            if (indices.empty()) continue;
            if (corners.size() > FBX_VERTEX_LIMIT) return fail_named("mesh exceeds 262144 vertices per material:", objects[model].name);
            lam::ArrayList<double> positions, normal_values, uv_values, color_values, joints_of, weights;
            for (size_t v = 0; v < corners.size(); v++) {
                const Corner& c = corners[v];
                for (int axis = 0; axis < 3; axis++) positions.append(world_points[c.point * 3 + axis]);
                if (normals.mapping >= 0) {
                    // normals take the inverse transpose of the placement
                    const double* n = normals.data.data() + c.normal * 3;
                    double out[3];
                    for (int axis = 0; axis < 3; axis++)
                        out[axis] = normal_matrix.m[axis * 4] * n[0] + normal_matrix.m[axis * 4 + 1] * n[1] + normal_matrix.m[axis * 4 + 2] * n[2];
                    double length = sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
                    for (int axis = 0; axis < 3; axis++) normal_values.append(length > 0 ? out[axis] / length : 0);
                }
                if (uvs.mapping >= 0) for (int k = 0; k < 2; k++) uv_values.append(uvs.data[c.uv * 2 + k]);
                if (colors.mapping >= 0) for (int k = 0; k < 3; k++) color_values.append(fmin(1, fmax(0, colors.data[c.color * 4 + k])));
                if (!skin.empty()) for (int k = 0; k < 4; k++) {
                    joints_of.append(skin[c.point * 8 + k * 2]);
                    weights.append(skin[c.point * 8 + k * 2 + 1]);
                }
            }
            Item out = a.element("mesh"), shape = a.element("geometry");
            a.attr(out, "id", a.id("mesh", a.serial++));
            a.attr(out, "name", a.text(objects[model].name));
            Item material = material_for(slots.empty() ? -1 : slots[slot]);
            if (get_type_id(material) == LMD_TYPE_NULL) return false;
            a.attr(out, "material", material);
            a.attr(shape, "type", a.symbol("buffer"));
            a.attr(shape, "positions", a.floats(positions.data(), positions.size()));
            if (!normal_values.empty()) a.attr(shape, "normals", a.floats(normal_values.data(), normal_values.size()));
            if (!uv_values.empty()) a.attr(shape, "uvs", a.floats(uv_values.data(), uv_values.size()));
            if (!color_values.empty()) a.attr(shape, "colors", a.floats(color_values.data(), color_values.size()));
            a.attr(shape, "indices", a.floats(indices.data(), indices.size()));
            if (!skin.empty()) {
                a.attr(shape, "skin-indices", a.floats(joints_of.data(), joints_of.size()));
                a.attr(shape, "skin-weights", a.floats(weights.data(), weights.size()));
            }
            a.append(out, shape);
            a.append(body, out);
        }
        return !a.ctx.hasErrors();
    }

    // the nearest joint at or above a model
    int joint_above(int model) {
        for (int m = model; m >= 0; m = objects[m].parent) if (objects[m].joint >= 0) return objects[m].joint;
        return -1;
    }
    // a pose's world matrix for one model, if a BindPose lists it
    bool pose_matrix(int model, FbxMat* out) {
        for (size_t i = 0; i < objects.size(); i++) {
            if (!is((int)i, "Pose") || !sub((int)i, "BindPose")) continue;  // INT_CAST_OK: object index
            ElementReader pose = objects[i].node;
            for (int64_t k = 0; k < pose.childCount(); k++) {
                ElementReader entry = pose.childAt(k).asElement();
                if (!entry.hasTag("PoseNode")) continue;
                ArrayReader id = fbx_props(entry.findChildElement("Node"));
                if (id.length() && find(id.get(0).asInt()) == model && fbx_matrix(entry, "Matrix", out)) return true;
            }
        }
        return false;
    }
    bool geometric(int model, FbxMat* out) {
        ElementReader node = objects[model].node;
        double t[3], r[3], s[3];
        fbx_vector(node, "GeometricTranslation", t, 0); fbx_vector(node, "GeometricRotation", r, 0);
        fbx_vector(node, "GeometricScaling", s, 1);
        *out = fbx_translation(t) * fbx_rotation(fbx_euler(r, 0)) * fbx_scaling(s);
        return true;
    }

    // skinned meshes first, so rigid props see every cluster's bind
    bool body(Item* out) {
        Item body = a.element("body");
        resources = a.element("resources");
        a.resources = resources;
        a.append(body, resources);
        lam::ArrayList<int> rigid;
        bool any = false;
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = 0; i < objects.size(); i++) {
                int geometry = (int)i;  // INT_CAST_OK: object index
                if (!is(geometry, "Geometry")) continue;
                if (!sub(geometry, "Mesh")) {
                    if (pass == 0) a.ctx.addWarning("character: FBX %s geometry '%s' is omitted", objects[i].subclass ? objects[i].subclass : "non-mesh", objects[i].name);
                    continue;
                }
                int model_link = -1;
                for (int l = objects[geometry].targets; l >= 0; l = links[l].next_target)
                    if (is(links[l].parent, "Model")) { model_link = l; break; }
                if (model_link < 0) continue;
                int model = links[model_link].parent;
                int skin_link = source(geometry, "Deformer");
                while (skin_link >= 0 && !sub(links[skin_link].child, "Skin")) {
                    if (pass == 0 && sub(links[skin_link].child, "BlendShape")) a.ctx.addWarning("character: FBX blend shapes on '%s' are omitted", objects[model].name);
                    skin_link = source(geometry, "Deformer", nullptr, skin_link);
                }
                if ((skin_link >= 0) != (pass == 0)) continue;
                lam::ArrayList<double> skin;
                FbxMat placement, g;
                geometric(model, &g);
                if (skin_link >= 0) {
                    if (!skin_weights(model, links[skin_link].child, point_count(geometry), skin, &placement)) return false;
                    skinned = true;
                } else {
                    FbxMat mesh_world;
                    if (!world(model, &mesh_world)) return false;
                    int joint = joint_above(model);
                    if (joint >= 0) {
                        // a rigid prop rides its nearest joint: place it in that joint's bind frame
                        FbxMat rest_inverse, bind_world;
                        if (!fbx_inverse(rest_world[joint], &rest_inverse) || !fbx_inverse(bind[joint], &bind_world)) return fail("rigid prop placement is singular");
                        placement = from_character * bind_world * to_character * rest_inverse * mesh_world;
                        size_t points = point_count(geometry);
                        for (size_t p = 0; p < points; p++) {
                            skin.append(joint); skin.append(1);
                            for (int k = 0; k < 3; k++) { skin.append(0); skin.append(0); }
                        }
                        skinned = true;
                    } else placement = mesh_world;
                }
                if (!mesh(body, model, objects[geometry].node, to_character * placement * g, skin)) return false;
                any = true;
            }
        }
        *out = any ? body : ItemNull;
        return true;
    }
    size_t point_count(int geometry) {
        ArrayReader props = fbx_props(objects[geometry].node.findChildElement("Vertices"));
        Item value = props.length() ? props.get(0).item() : ItemNull;
        return get_type_id(value) == LMD_TYPE_ARRAY_NUM ? (size_t)value.array_num->length / 3 : 0;
    }
    // per control point the four largest influences, renormalized; sets each
    // linked joint's inverse bind and returns the mesh's bind placement
    bool skin_weights(int model, int skin, size_t points, lam::ArrayList<double>& out, FbxMat* placement) {
        lam::ArrayList<double> influence;  // per point: joint, weight pairs, unbounded
        lam::ArrayList<int> first, next;
        lam::ArrayList<double> indexes, weights;
        for (size_t p = 0; p < points; p++) first.append(-1);
        bool placed = false;
        for (int l = source(skin, "Deformer"); l >= 0; l = source(skin, "Deformer", nullptr, l)) {
            int cluster = links[l].child;
            if (!sub(cluster, "Cluster")) continue;
            int bone_link = source(cluster, "Model");
            if (bone_link < 0) continue;
            int joint = objects[links[bone_link].child].joint;
            ElementReader node = objects[cluster].node;
            // the file's Transform is the mesh relative to the bone at bind (ufbx's
            // mesh_node_to_bone, Blender's TransformLink⁻¹ · mesh world); the SDK's
            // GetTransformMatrix returns TransformLink · Transform, the mesh's bind world
            FbxMat relative, link, mesh_bind;
            bool linked = fbx_matrix(node, "TransformLink", &link) || pose_matrix(links[bone_link].child, &link);
            if (!linked) return fail_named("skin cluster has no joint bind transform:", objects[cluster].name);
            if (fbx_matrix(node, "Transform", &relative)) mesh_bind = link * relative;
            else if (!pose_matrix(model, &mesh_bind)) return fail_named("skin cluster has no mesh bind transform:", objects[cluster].name);
            fbx_child_numbers(node, "Indexes", indexes);
            fbx_child_numbers(node, "Weights", weights);
            // clusters that weight nothing (IK and control bones) cannot place the mesh
            if (!indexes.empty()) {
                if (!placed) { *placement = mesh_bind; placed = true; }
                else if (fbx_difference(*placement, mesh_bind) > 1e-4)
                    return fail_named("skin clusters disagree on the mesh bind transform of", objects[model].name);
            }
            FbxMat inverse;
            if (!fbx_inverse(character(link), &inverse)) return fail_named("joint bind transform is singular:", objects[joints[joint]].name);
            // profile 1 keeps one bind per joint across every mesh
            if (bound[joint] && fbx_difference(bind[joint], inverse) > 1e-4)
                return fail_named("meshes bind joint with different poses:", objects[joints[joint]].name);
            bind[joint] = inverse;
            bound[joint] = 1;
            if (indexes.size() != weights.size()) return fail_named("skin cluster index and weight counts differ:", objects[cluster].name);
            for (size_t k = 0; k < indexes.size(); k++) {
                if (indexes[k] < 0 || indexes[k] >= (double)points) return fail_named("skin cluster index out of range:", objects[cluster].name);
                if (!(weights[k] > 0)) continue;
                size_t p = (size_t)indexes[k];
                next.append(first[p]);
                first[p] = (int)(influence.size() / 2);  // INT_CAST_OK: influence count is bounded by the arrays
                influence.append(joint); influence.append(weights[k]);
            }
        }
        if (!placed) return fail_named("skin has no weighted clusters:", objects[model].name);
        size_t dropped = 0, unweighted = 0;
        int fallback = joint_above(model) >= 0 ? joint_above(model) : 0;
        for (size_t p = 0; p < points; p++) {
            double best_joint[4] = {0, 0, 0, 0}, best_weight[4] = {0, 0, 0, 0};
            size_t count = 0;
            for (int i = first[p]; i >= 0; i = next[i]) {
                double joint = influence[i * 2], weight = influence[i * 2 + 1];
                count++;
                // insertion into the four largest so far, earlier influences winning ties
                int s = 4;
                while (s > 0 && weight > best_weight[s - 1]) s--;
                if (s == 4) continue;
                for (int k = 3; k > s; k--) { best_joint[k] = best_joint[k - 1]; best_weight[k] = best_weight[k - 1]; }
                best_joint[s] = joint; best_weight[s] = weight;
            }
            if (count > 4) dropped += count - 4;
            double total = best_weight[0] + best_weight[1] + best_weight[2] + best_weight[3];
            if (!(total > 0)) { unweighted++; best_joint[0] = fallback; best_weight[0] = total = 1; }
            for (int k = 0; k < 4; k++) { out.append(best_joint[k]); out.append(best_weight[k] / total); }
        }
        if (dropped) a.ctx.addWarning("character: FBX mesh '%s' drops %zu skin influences beyond four per vertex", objects[model].name, dropped);
        if (unweighted) a.ctx.addWarning("character: FBX mesh '%s' has %zu unweighted vertices, bound to joint '%s'",
            objects[model].name, unweighted, objects[joints[fallback]].name);
        return true;
    }

    // ---- clips -------------------------------------------------------------------------

    bool curve(int object, FbxCurve* out) {
        ElementReader node = objects[object].node;
        lam::ArrayList<double> times, values, flags, data, refs;
        fbx_child_numbers(node, "KeyTime", times);
        if (!fbx_child_numbers(node, "KeyValueFloat", values)) fbx_child_numbers(node, "KeyValueDouble", values);
        if (times.size() != values.size()) return fail("animation curve key times and values differ in count");
        for (size_t k = 1; k < times.size(); k++) if (!(times[k] > times[k - 1])) return fail("animation curve key times must increase");
        fbx_child_numbers(node, "KeyAttrFlags", flags);
        fbx_child_numbers(node, "KeyAttrDataFloat", data);
        fbx_child_numbers(node, "KeyAttrRefCount", refs);
        out->first = key_time.size();
        out->count = times.size();
        size_t attribute = 0, used = 0, n = times.size();
        // the previous key's attribute holds this key's incoming slope and weight
        double carried_left = 0, carried_reach = 1.0 / 3;
        for (size_t k = 0; k < n; k++) {
            // KeyAttrRefCount runs give each attribute group a stretch of keys
            while (attribute < refs.size() && used >= refs[attribute]) { attribute++; used = 0; }
            used++;
            size_t at = attribute < flags.size() ? attribute : flags.empty() ? SIZE_MAX : flags.size() - 1;
            int64_t bits = at == SIZE_MAX ? FBX_LINEAR : (int64_t)flags[at];
            const double* stored = at != SIZE_MAX && at * 4 + 3 < data.size() ? data.data() + at * 4 : nullptr;
            double t = times[k] / FBX_TICKS, value = values[k];
            bool has_prev = k > 0, has_next = k + 1 < n;
            double t0 = has_prev ? times[k - 1] / FBX_TICKS : t, v0 = has_prev ? values[k - 1] : value;
            double t1 = has_next ? times[k + 1] / FBX_TICKS : t, v1 = has_next ? values[k + 1] : value;
            double left = carried_left, right = stored ? stored[0] : 0, next_left = stored ? stored[1] : 0;
            double reach_left = carried_reach, reach_right = 1.0 / 3, next_reach = 1.0 / 3;
            uint8_t mode;
            if (bits & FBX_CONSTANT) {
                mode = bits & FBX_CONSTANT_NEXT ? MODE_CONSTANT_NEXT : MODE_CONSTANT;
                right = next_left = 0;
            } else if (bits & FBX_CUBIC) {
                mode = MODE_CUBIC;
                // weights are two 0.4 decimal fixed-point values packed into the bits of the
                // third attribute float (after ufbx); Blender writes them (Kenney's Cube Pets)
                if ((bits & (FBX_WEIGHTED_RIGHT | FBX_WEIGHTED_NEXT_LEFT)) && stored) {
                    float packed_float = (float)stored[2];
                    uint32_t packed;
                    memcpy(&packed, &packed_float, sizeof(packed));
                    if (bits & FBX_WEIGHTED_RIGHT) reach_right = (packed & 0xffff) * 0.0001;
                    if (bits & FBX_WEIGHTED_NEXT_LEFT) next_reach = (packed >> 16) * 0.0001;
                }
                if (bits & FBX_TANGENT_TCB) {
                    // tension, continuity and bias in the stored data shape both slopes
                    double incoming = has_prev ? (value - v0) / (t - t0) : 0, outgoing = has_next ? (v1 - value) / (t1 - t) : 0;
                    double tension = stored ? stored[0] : 0, continuity = stored ? stored[1] : 0, bias = stored ? stored[2] : 0;
                    double factor = has_prev && has_next ? 0.5 : 1.0;
                    left = factor * (1 - tension) * ((1 + bias) * (1 - continuity) * incoming + (1 - bias) * (1 + continuity) * outgoing);
                    right = factor * (1 - tension) * ((1 + bias) * (1 + continuity) * incoming + (1 - bias) * (1 - continuity) * outgoing);
                    next_left = 0;
                    next_reach = 1.0 / 3;
                } else if (!(bits & FBX_TANGENT_USER)) {
                    // auto tangents are solved from the neighbours; the stored slopes act only as auto bias
                    if (has_prev && has_next) {
                        if (fabs(left + right) <= 0.0001) left = right = auto_tangent(t0, t, t1, v0, value, v1, right, bits, reach_left, reach_right);
                        else {
                            double solved_left = auto_tangent(t0, t, t1, v0, value, v1, -left, bits, reach_left, reach_right);
                            right = auto_tangent(t0, t, t1, v0, value, v1, right, bits, reach_left, reach_right);
                            left = solved_left;
                        }
                    } else if (has_prev) left = right = edge_tangent(t0, t, v0, value, -left, bits);
                    else if (has_next) left = right = edge_tangent(t, t1, value, v1, right, bits);
                    else left = right = 0;
                }
            } else {
                mode = MODE_LINEAR;
                right = next_left = has_next ? (v1 - value) / (t1 - t) : 0;
            }
            key_time.append(times[k]); key_value.append(value); key_mode.append(mode);
            key_right.append(right); key_left.append(left);
            key_reach_right.append(reach_right); key_reach_left.append(reach_left);
            carried_left = next_left;
            carried_reach = next_reach;
        }
        return true;
    }
    // an auto tangent from the neighbouring keys (after ufbx's ufbxi_solve_auto_tangent,
    // which follows the FBX SDK): clamped keys flatten, time-dependent tangents blend
    // the one- and two-sided slopes and lean toward `bias` percent of a side
    static double auto_tangent(double t0, double t, double t1, double v0, double v, double v1, double bias, int64_t bits,
                               double reach_left, double reach_right) {
        if ((bits & FBX_CLAMP) && fmin(fabs(v0 - v), fabs(v1 - v)) <= 0) return 0;
        double slope = (v1 - v0) / (t1 - t0);
        if (!(bits & FBX_TIME_INDEPENDENT)) {
            double incoming = (v - v0) / (t - t0), outgoing = (v1 - v) / (t1 - t), delta = (t - t0) / (t1 - t0);
            slope = slope * 0.5 + (incoming * (1 - delta) + outgoing * delta) * 0.5;
            double weight = fabs(bias) / 100;
            if (weight > 0.0001) {
                double target = bias > 0 ? outgoing : incoming, pull = target - slope;
                slope = slope * (1 - weight) + target * weight;
                double excess = weight - 5;
                if (excess > 0) {
                    double sign = fabs(pull) > 0.00001 ? pull : bias;
                    slope += excess * excess * (sign > 0 ? 1 : -1) * 40;
                }
            }
        }
        if (bits & FBX_CLAMP_PROGRESSIVE) {
            // never overshoot either neighbour within the tangents' weighted reach
            double sign = slope >= 0 ? 1 : -1, magnitude = sign * slope;
            double span_left = reach_left * (t - t0), span_right = reach_right * (t1 - t);
            double max_left = span_left > 0 ? sign * (v - v0) / span_left : 0, max_right = span_right > 0 ? sign * (v1 - v) / span_right : 0;
            if (!(max_left > 0)) max_left = 0;
            if (!(max_right > 0)) max_right = 0;
            slope = sign * fmin(magnitude, fmin(max_left, max_right));
        }
        return slope;
    }
    // the first or last key's auto tangent follows its one segment
    static double edge_tangent(double t0, double t1, double v0, double v1, double bias, int64_t bits) {
        if (bits & FBX_CLAMP_PROGRESSIVE) return 0;
        if ((bits & FBX_CLAMP) && fabs(v1 - v0) <= 0) return 0;
        double slope = (v1 - v0) / (t1 - t0);
        double excess = fabs(bias) / 100 - 5;
        if (!(bits & FBX_TIME_INDEPENDENT) && excess > 0) slope += excess * excess * (bias > 0 ? 1 : -1) * 40;
        return slope;
    }
    // the Bezier parameter whose time is x0, for inner control-point times p1 and p2:
    // Newton steps as ufbx's ufbxi_find_cubic_bezier_t takes them
    static double bezier_parameter(double p1, double p2, double x0) {
        double a = 3 * p1 - 3 * p2 + 1, b = 3 * p2 - 6 * p1, c = 3 * p1, t = x0;
        for (int i = 0; i < 11; i++) {
            double x = ((a * t + b) * t + c) * t - x0;
            t -= x / ((3 * a * t + 2 * b) * t + c);
            // converged within 4 ULP of 1, tested after the third step and then every second
            if (i >= 2 && i % 2 == 0 && fabs(x) <= 8.881784197001252e-16) break;
        }
        return t;
    }
    double evaluate(const FbxCurve& c, double t) const {
        const double* time = key_time.data() + c.first;
        const double* value = key_value.data() + c.first;
        size_t n = c.count;
        if (t <= time[0]) return value[0];
        if (t >= time[n - 1]) return value[n - 1];
        size_t lo = 0, hi = n - 1;
        while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (time[mid] <= t) lo = mid; else hi = mid; }
        double span = time[hi] - time[lo], u = (t - time[lo]) / span;
        switch (key_mode[c.first + lo]) {
        case MODE_CONSTANT: return value[lo];
        case MODE_CONSTANT_NEXT: return value[hi];
        case MODE_CUBIC: {
            // a Bezier whose inner control points sit at the tangents' weights along the segment, at
            // heights the slopes give (after ufbx); weights of 1/3 make it the Hermite curve, u itself
            double seconds = span / FBX_TICKS, x1 = key_reach_right[c.first + lo], x2 = 1 - key_reach_left[c.first + hi];
            double y0 = value[lo], y3 = value[hi];
            double y1 = y0 + key_right[c.first + lo] * x1 * seconds, y2 = y3 - key_left[c.first + hi] * (1 - x2) * seconds;
            double s = bezier_parameter(x1, x2, u), r = 1 - s;
            return r * r * r * y0 + 3 * (r * r * s * y1 + r * s * s * y2) + s * s * s * y3;
        }
        default: return value[lo] + (value[hi] - value[lo]) * u;
        }
    }
    void track_values(const FbxTrack& track, const lam::ArrayList<FbxCurve>& curves, double t, double* out) const {
        for (int c = 0; c < 3; c++) out[c] = track.curve[c] >= 0 ? evaluate(curves[track.curve[c]], t) : track.fallback[c];
    }

    bool clip(int stack, const char* name, Item* out) {
        int layer_link = source(stack, "AnimationLayer");
        if (layer_link >= 0 && source(stack, "AnimationLayer", nullptr, layer_link) >= 0)
            a.ctx.addWarning("character: FBX clip '%s' uses only its base animation layer", name);
        int layer = layer_link >= 0 ? links[layer_link].child : -1;
        lam::ArrayList<FbxTrack> tracks;
        lam::ArrayList<FbxCurve> curves;
        bool cubic = false;
        static const char* properties[] = {"Lcl Translation", "Lcl Rotation", "Lcl Scaling"};
        for (int l = layer >= 0 ? source(layer, "AnimationCurveNode") : -1; l >= 0; l = source(layer, "AnimationCurveNode", nullptr, l)) {
            int curve_node = links[l].child;
            for (int t = objects[curve_node].targets; t >= 0; t = links[t].next_target) {
                int model = links[t].parent;
                if (!is(model, "Model")) continue;
                int property = -1;
                for (int p = 0; p < 3; p++) if (asset_is(links[t].property, properties[p])) property = p;
                if (property < 0 || objects[model].joint < 0) {
                    a.ctx.addWarning("character: FBX animation of '%s' %s is omitted", objects[model].name,
                        links[t].property ? links[t].property : "");
                    continue;
                }
                FbxTrack track = {objects[model].joint, property, {-1, -1, -1}, {0, 0, 0}};
                const FbxNode& n = nodes[(size_t)track.joint];
                const double* rest = property == 0 ? n.t : property == 1 ? n.r : n.s;
                static const char* channels[] = {"d|X", "d|Y", "d|Z"};
                for (int c = 0; c < 3; c++) {
                    track.fallback[c] = fbx_value(objects[curve_node].node, channels[c], rest[c]);
                    int curve_link = source(curve_node, "AnimationCurve", channels[c]);
                    if (curve_link < 0) continue;
                    FbxCurve data;
                    if (!curve(links[curve_link].child, &data)) return false;
                    if (!data.count) continue;
                    for (size_t k = 0; k < data.count; k++) cubic |= key_mode[data.first + k] == MODE_CUBIC;
                    track.curve[c] = (int)curves.size();  // INT_CAST_OK: curve count is bounded by the raw tree
                    curves.append(data);
                }
                tracks.append(track);
            }
        }
        // the stack's range, else the span of its keys
        double first = INFINITY, last = -INFINITY;
        for (const FbxCurve& c : curves) { first = fmin(first, key_time[c.first]); last = fmax(last, key_time[c.first + c.count - 1]); }
        ElementReader node = objects[stack].node;
        double start = fbx_value(node, "LocalStart", isfinite(first) ? first : 0);
        double stop = fbx_value(node, "LocalStop", isfinite(last) ? last : start);
        if (!(stop >= start)) return fail_named("clip stops before it starts:", name);
        lam::ArrayList<double> times;
        times.append(start); times.append(stop);
        for (const FbxCurve& c : curves) for (size_t k = 0; k < c.count; k++) {
            double t = key_time[c.first + k];
            if (t > start && t < stop) times.append(t);
        }
        // cubic segments are sampled at the file's frame rate as well
        if (cubic) for (size_t frame = 1; frame < FBX_CHANNEL_LIMIT; frame++) {
            double t = start + frame * FBX_TICKS / rate;
            if (t >= stop) break;
            times.append(t);
        }
        // qsort: the list's quicksort degrades to quadratic depth on sorted key runs
        qsort(times.data(), times.size(), sizeof(double), [](const void* x, const void* y) {
            double p = *(const double*)x, q = *(const double*)y;
            return p < q ? -1 : p > q ? 1 : 0;
        });
        lam::ArrayList<double> keys;
        for (double t : times) if (keys.empty() || t - keys[keys.size() - 1] > 0.5) keys.append(t);
        // SLERP takes the short arc, so split any interval whose Euler change could reach half a turn
        lam::ArrayList<double> baked;
        for (size_t k = 0; k < keys.size(); k++) {
            if (k) subdivide(tracks, curves, keys[k - 1], keys[k], baked, 0);
            baked.append(keys[k]);
        }
        if (baked.size() * 4 > FBX_CHANNEL_LIMIT) return fail_named("clip exceeds 2097152 keys:", name);
        Item element = a.element("clip");
        lam::ArrayList<double> seconds;
        for (double t : baked) seconds.append((t - start) / FBX_TICKS);
        a.attr(element, "name", a.text(name));
        a.attr(element, "duration", a.number((stop - start) / FBX_TICKS));
        a.attr(element, "rate", a.number(rate));
        a.attr(element, "times", a.floats(seconds.data(), seconds.size()));
        for (size_t j = 0; j < joints.size(); j++) {
            const FbxNode& n = nodes[j];
            const FbxTrack* own[3] = {nullptr, nullptr, nullptr};
            for (const FbxTrack& track : tracks) if (track.joint == (int)j) own[track.property] = &track;  // INT_CAST_OK: joint index
            // an Rrs joint also moves when its parent's scale does
            int up = objects[joints[j]].parent, parent = up < 0 ? -1 : objects[up].joint;
            const FbxTrack* parent_scale = nullptr;
            if (parent >= 0 && n.inherit != 1) for (const FbxTrack& track : tracks) if (track.joint == parent && track.property == 2) parent_scale = &track;
            if (!own[0] && !own[1] && !own[2] && !(parent_scale && n.inherit == 2)) continue;
            double rest_t[3], rest_s[3];
            CharacterQuat rest_q;
            FbxMat rest;
            if (!rest_local(j, &rest)) return false;
            fbx_decompose(character(rest), rest_t, &rest_q, rest_s);
            lam::ArrayList<double> positions, quaternions, scales;
            bool moved = false, turned = false, sized = false;
            CharacterQuat previous = rest_q;
            for (double t : baked) {
                double tv[3], rv[3], sv[3];
                if (own[0]) track_values(*own[0], curves, t, tv); else memcpy(tv, n.t, sizeof(tv));
                if (own[1]) track_values(*own[1], curves, t, rv); else memcpy(rv, n.r, sizeof(rv));
                if (own[2]) track_values(*own[2], curves, t, sv); else memcpy(sv, n.s, sizeof(sv));
                double position[3], size[3], ps[3];
                CharacterQuat q;
                FbxMat m = local(n, tv, rv, sv);
                if (parent >= 0) {
                    if (parent_scale) track_values(*parent_scale, curves, t, ps); else memcpy(ps, nodes[parent].s, sizeof(ps));
                    if (!inherited(n, joints[j], ps, &m)) return false;
                }
                if (!fbx_decompose(character(m), position, &q, size)) return fail_named("animated transform has shear on", objects[joints[j]].name);
                q = character_quat_continue(previous, q);
                previous = q;
                double quaternion[4] = {q.x, q.y, q.z, q.w};
                for (int c = 0; c < 3; c++) {
                    positions.append(position[c]); scales.append(size[c]);
                    moved |= fabs(position[c] - rest_t[c]) > 1e-9;
                    sized |= fabs(size[c] - rest_s[c]) > 1e-9;
                }
                for (int c = 0; c < 4; c++) quaternions.append(quaternion[c]);
                double dot = fabs(q.x * rest_q.x + q.y * rest_q.y + q.z * rest_q.z + q.w * rest_q.w);
                turned |= 1 - dot > 1e-12;
            }
            if (!moved && !turned && !sized) continue;
            Item channel = a.element("channel");
            a.attr(channel, "joint", a.text(objects[joints[j]].name));
            if (moved) a.attr(channel, "position", a.floats(positions.data(), positions.size()));
            if (turned) a.attr(channel, "quaternion", a.floats(quaternions.data(), quaternions.size()));
            if (sized) a.attr(channel, "scale", a.floats(scales.data(), scales.size()));
            a.append(element, channel);
        }
        *out = element;
        return !a.ctx.hasErrors();
    }
    void subdivide(const lam::ArrayList<FbxTrack>& tracks, const lam::ArrayList<FbxCurve>& curves, double t0, double t1,
            lam::ArrayList<double>& out, int depth) {
        if (depth >= 16 || t1 - t0 < 2) return;
        bool split = false;
        for (const FbxTrack& track : tracks) {
            if (track.property != 1) continue;
            double r0[3], r1[3];
            track_values(track, curves, t0, r0);
            track_values(track, curves, t1, r1);
            if (fabs(r1[0] - r0[0]) + fabs(r1[1] - r0[1]) + fabs(r1[2] - r0[2]) >= 180) { split = true; break; }
        }
        if (!split) return;
        double mid = floor((t0 + t1) / 2);
        subdivide(tracks, curves, t0, mid, out, depth + 1);
        out.append(mid);
        subdivide(tracks, curves, mid, t1, out, depth + 1);
    }

    void omitted() {
        size_t cameras = 0, lights = 0, constraints = 0;
        for (size_t i = 0; i < objects.size(); i++) {
            int o = (int)i;  // INT_CAST_OK: object index
            cameras += (is(o, "NodeAttribute") || is(o, "Model")) && sub(o, "Camera");
            lights += (is(o, "NodeAttribute") || is(o, "Model")) && sub(o, "Light");
            constraints += is(o, "Constraint");
        }
        if (cameras) a.ctx.addWarning("character: FBX cameras are omitted");
        if (lights) a.ctx.addWarning("character: FBX lights are omitted");
        if (constraints) a.ctx.addWarning("character: FBX constraints are omitted");
    }

    bool convert() {
        a.attr(a.root, "source", a.symbol("fbx"));
        a.attr(a.root, "units", a.symbol("m"));
        if (!index_objects() || !index_links() || !axes() || !select_joints()) return false;
        omitted();
        Item body_element = ItemNull;
        if (!body(&body_element)) return false;
        Item skeleton = a.element("skeleton");
        for (size_t j = 0; j < joints.size(); j++) {
            if (objects[joints[j]].parent >= 0) continue;
            Item element = joint_element(j);
            if (get_type_id(element) == LMD_TYPE_NULL) return false;
            a.append(skeleton, element);
        }
        a.append(a.root, skeleton);
        if (get_type_id(body_element) != LMD_TYPE_NULL) a.append(a.root, body_element);
        lam::ArrayList<const char*> names;
        for (size_t i = 0; i < objects.size(); i++) {
            int stack = (int)i;  // INT_CAST_OK: object index
            if (!is(stack, "AnimationStack")) continue;
            if (names.size() >= FBX_CLIP_LIMIT) return fail("file exceeds 1024 clips");
            // duplicate stack names gain a numeric suffix
            char name[256];
            snprintf(name, sizeof(name), "%s", *objects[i].name ? objects[i].name : "clip");
            for (int n = 2; ; n++) {
                bool taken = false;
                for (const char* other : names) taken |= !strcmp(other, name);
                if (!taken) break;
                snprintf(name, sizeof(name), "%s-%d", *objects[i].name ? objects[i].name : "clip", n);
            }
            names.append(a.ctx.builder.createString(name)->chars);
            Item element = ItemNull;
            if (!clip(stack, name, &element)) return false;
            a.append(a.root, element);
        }
        return !a.ctx.hasErrors();
    }
};

}  // namespace

bool character_from_fbx(SceneAsset& a, Item raw) {
    FbxCharacter converter(a, raw);
    return converter.convert();
}

}  // namespace lambda
