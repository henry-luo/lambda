#include "input-character.hpp"
#include "input-parsers.h"
#include "../../lib/mime-detect.h"
#include "../../lib/url.h"

// `input(source, {type: 'character', flavor})`: a raw model tree converted
// into the common <character> schema (vibe/Lambda_Pkg_Motion.md §6-§7).

namespace lambda {

namespace {

// A BVH joint's facts the clip needs once the skeleton is built.
struct BvhJoint {
    const char* name;
    double offset[3];
    int64_t column;
    unsigned channel_count;
    int8_t position_axis[6];  // per channel: 0..2 for X/Y/Zposition, else -1
    int8_t rotation_axis[6];  // per channel: 0..2 for X/Y/Zrotation, else -1
};

static bool bvh_offset(ItemReader value, double out[3]) {
    if (!value.isArray() || value.asArray().length() != 3) return false;
    for (unsigned c = 0; c < 3; c++) {
        out[c] = asset_number(value.asArray().get(c));
        if (!isfinite(out[c])) return false;
    }
    return true;
}

// one <joint> per root/joint, its End Site folded into `tip`; joints are
// collected in preorder, the order clip channels follow
static bool bvh_skeleton(SceneAsset& a, ElementReader source, Item parent,
        lam::ArrayList<BvhJoint>& joints) {
    BvhJoint joint = {};
    joint.name = source.get_attr("name").cstring();
    if (!joint.name || !bvh_offset(source.get_attr("offset"), joint.offset)) return a.fail("BVH joint needs a name and offset");
    ItemReader channels = source.get_attr("channels");
    joint.channel_count = channels.isArray() ? (unsigned)channels.asArray().length() : 0;
    if (!joint.channel_count || joint.channel_count > 6) return a.fail("BVH joint needs 1..6 channels");
    joint.column = source.get_attr("column").asInt();
    for (unsigned i = 0; i < joint.channel_count; i++) {
        const char* name = channels.asArray().get(i).cstring();
        if (!name || strlen(name) != 9) return a.fail("unknown BVH channel");
        unsigned axis = (unsigned)(name[0] - 'X');
        bool position = !strcmp(name + 1, "position"), rotation = !strcmp(name + 1, "rotation");
        if (axis > 2 || (!position && !rotation)) return a.fail("unknown BVH channel");
        joint.position_axis[i] = position ? (int8_t)axis : -1;
        joint.rotation_axis[i] = rotation ? (int8_t)axis : -1;
    }
    Item out = a.element("joint");
    a.attr(out, "name", a.text(joint.name));
    a.attr(out, "position", a.array(joint.offset, 3));
    double identity[4] = {0, 0, 0, 1};
    a.attr(out, "quaternion", a.array(identity, 4));
    joints.append(joint);
    for (int64_t i = 0; i < source.childCount(); i++) {
        ElementReader child = source.childAt(i).asElement();
        if (child.hasTag("end")) {
            double tip[3];
            if (!bvh_offset(child.get_attr("offset"), tip)) return a.fail("BVH End Site needs an offset");
            a.attr(out, "tip", a.array(tip, 3));
        } else if (child.hasTag("joint")) {
            if (!bvh_skeleton(a, child, out, joints)) return false;
        }
    }
    a.append(parent, out);
    return true;
}

}  // namespace

// §7.3: rest = OFFSET with identity rotation; rotation channels compose in the
// listed order, the first outermost; position channels give the full local
// translation, replacing OFFSET; a channel equal to rest at every key is omitted
bool character_from_bvh(SceneAsset& a, Item raw, const char* clip_name) {
    ElementReader bvh(raw);
    ElementReader hierarchy = bvh.findChildElement("hierarchy");
    ElementReader motion = bvh.findChildElement("motion");
    a.attr(a.root, "source", a.symbol("bvh"));
    // BVH declares no unit; motion.load's `scale` converts and sets 'm'
    a.attr(a.root, "units", a.symbol("unknown"));
    Item skeleton = a.element("skeleton");
    lam::ArrayList<BvhJoint> joints;
    for (int64_t i = 0; i < hierarchy.childCount(); i++) {
        if (!bvh_skeleton(a, hierarchy.childAt(i).asElement(), skeleton, joints)) return false;
    }
    if (joints.empty()) return a.fail("BVH has no joints");
    a.append(a.root, skeleton);
    int64_t frames = motion.get_attr("frames").asInt();
    double frame_time = asset_number(motion.get_attr("frame_time"));
    Item data_item = motion.get_attr("data").item();
    if (frames <= 0) return !a.ctx.hasErrors();
    // the parser built `data` as one contiguous [frames, channels] float block
    if (get_type_id(data_item) != LMD_TYPE_ARRAY_NUM) return a.fail("BVH motion data is not numeric");
    ArrayNum* data = (ArrayNum*)(uintptr_t)data_item.item;
    if (data->get_elem_type() != ELEM_FLOAT64 || !data->data || data->length % frames) return a.fail("BVH motion data has the wrong shape");
    int64_t width = data->length / frames;
    const double* values = (const double*)data->data;

    lam::ArrayList<double> times;
    for (int64_t k = 0; k < frames; k++) times.append(k * frame_time);
    Item clip = a.element("clip");
    a.attr(clip, "name", a.text(clip_name));
    a.attr(clip, "duration", a.number((frames - 1) * frame_time));
    a.attr(clip, "rate", a.number(1.0 / frame_time));
    a.attr(clip, "times", a.floats(times.data(), times.size()));
    for (size_t j = 0; j < joints.size(); j++) {
        const BvhJoint& joint = joints[j];
        if (joint.column < 0 || joint.column + joint.channel_count > (uint64_t)width) return a.fail("BVH channel column out of range");
        bool has_position = false, has_rotation = false;
        for (unsigned i = 0; i < joint.channel_count; i++) {
            has_position |= joint.position_axis[i] >= 0;
            has_rotation |= joint.rotation_axis[i] >= 0;
        }
        lam::ArrayList<double> positions, quaternions;
        bool moved = false, turned = false;
        CharacterQuat previous = {0, 0, 0, 1};
        for (int64_t k = 0; k < frames; k++) {
            const double* row = values + k * width + joint.column;
            double position[3] = {joint.offset[0], joint.offset[1], joint.offset[2]};
            CharacterQuat q = {0, 0, 0, 1};
            for (unsigned i = 0; i < joint.channel_count; i++) {
                if (joint.position_axis[i] >= 0) position[joint.position_axis[i]] = row[i];
                else q = character_quat_mul(q, character_quat_axis((unsigned)joint.rotation_axis[i], row[i]));
            }
            q = character_quat_continue(previous, character_quat_normalized(q));
            previous = q;
            for (unsigned c = 0; c < 3; c++) {
                positions.append(position[c]);
                moved |= fabs(position[c] - joint.offset[c]) > 1e-9;
            }
            quaternions.append(q.x); quaternions.append(q.y); quaternions.append(q.z); quaternions.append(q.w);
            turned |= fabs(q.x) > 1e-9 || fabs(q.y) > 1e-9 || fabs(q.z) > 1e-9 || fabs(fabs(q.w) - 1) > 1e-9;
        }
        if (!(has_position && moved) && !(has_rotation && turned)) continue;
        Item channel = a.element("channel");
        a.attr(channel, "joint", a.text(joint.name));
        if (has_position && moved) a.attr(channel, "position", a.floats(positions.data(), positions.size()));
        if (has_rotation && turned) a.attr(channel, "quaternion", a.floats(quaternions.data(), quaternions.size()));
        a.append(clip, channel);
    }
    a.append(a.root, clip);
    return !a.ctx.hasErrors();
}

}  // namespace lambda

// the source's file stem names its clip; a string source has none
static const char* character_clip_name(Input* input, char* buffer, size_t capacity) {
    Url* url = (Url*)input->url;
    const char* path = url && url->pathname ? url->pathname->chars : nullptr;
    const char* base = path ? strrchr(path, '/') : nullptr;
    base = base ? base + 1 : path;
    if (!base || !*base) return "clip";
    const char* dot = strrchr(base, '.');
    size_t length = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length >= capacity) length = capacity - 1;
    memcpy(buffer, base, length);
    buffer[length] = 0;
    return buffer;
}

void input_character(Input* input, const char* source, size_t length, const char* flavor) {
    using namespace lambda;
    SceneAsset asset(input, "character");
    const char* type = flavor && strcmp(flavor, "auto") ? flavor : nullptr;
    if (!type) {
        Url* url = (Url*)input->url;
        const char* path = url && url->pathname ? url->pathname->chars : "";
        MimeDetector* detector = mime_detector_init();
        const char* mime = detect_mime_type(detector, path, source, length);
        type = asset_is(mime, "model/x-bvh") ? "bvh" : "unknown";
        mime_detector_destroy(detector);
    }
    char name[128];
    if (strcmp(type, "bvh")) asset.fail("character: expected a .bvh source (or an explicit format)");
    else if (input_parse_model(input, source, length, "bvh") && !input->parse_failed) {
        Item raw = input->root;
        if (!character_from_bvh(asset, raw, character_clip_name(input, name, sizeof(name))) &&
                !asset.ctx.hasErrors()) {
            asset.fail("invalid BVH structure");
        }
    }
    if (input->parse_failed && !asset.ctx.hasErrors()) {
        asset.ctx.addError("character: source parsing failed: %s",
            input->parse_error_message ? input->parse_error_message : "invalid syntax");
    }
    asset.finish();
}
