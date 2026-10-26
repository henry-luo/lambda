// EXT_meshopt_compression bitstream decoding: the attribute, triangle and index
// codecs plus the octahedral/quaternion/exponential filters, following the
// Khronos extension specification. Every read is bounded by the declared
// compressed length because glTF buffers are untrusted input.
#include "input-scene3d.hpp"

namespace lambda {
namespace {
const size_t GROUP = 16;            // bytes per byte group
const size_t GROUP_LIMIT = 24;      // largest group payload: 8 packed bytes + 16 escapes
const size_t BLOCK_BYTES = 8192;    // attribute block scratch size
const size_t BLOCK_MAX = 256;       // attribute block element cap

uint8_t unzigzag8(uint8_t v) { return (uint8_t)(-(v & 1) ^ (v >> 1)); }

// one 16-byte group of deltas; earlier deltas use the more significant bits.
const uint8_t* group(const uint8_t* data, uint8_t* out, unsigned mode) {
    if (mode == 0) { memset(out, 0, GROUP); return data; }
    if (mode == 3) { memcpy(out, data, GROUP); return data + GROUP; }
    unsigned bits = mode == 1 ? 2 : 4, per_byte = 8 / bits, escape = (1u << bits) - 1;
    const uint8_t* extra = data + GROUP / per_byte;
    for (size_t i = 0; i < GROUP; i++) {
        unsigned value = (data[i / per_byte] >> (8 - bits * (i % per_byte + 1))) & escape;
        out[i] = value == escape ? *extra++ : (uint8_t)value;
    }
    return extra;
}

// one byte lane of a block: a 2-bit mode per group, then each group's payload.
const uint8_t* lane(const uint8_t* data, const uint8_t* end, uint8_t* out, size_t size) {
    const uint8_t* header = data; size_t header_size = (size / GROUP + 3) / 4;
    if ((size_t)(end - data) < header_size) return nullptr;
    data += header_size;
    for (size_t i = 0; i < size; i += GROUP) {
        // the tail block guarantees GROUP_LIMIT readable bytes for a valid stream.
        if ((size_t)(end - data) < GROUP_LIMIT) return nullptr;
        size_t g = i / GROUP;
        data = group(data, out + i, (header[g / 4] >> ((g % 4) * 2)) & 3);
    }
    return data;
}

const char* decode_attributes(uint8_t* out, size_t count, size_t stride, const uint8_t* data, size_t length) {
    const uint8_t* end = data + length;
    if (length < 1 + stride) return "truncated attribute stream";
    if (*data++ != 0xa0) return "unsupported attribute codec version";
    uint8_t last[256], deltas[BLOCK_MAX], block[BLOCK_BYTES];
    // the baseline element closes the stream, before any delta is applied.
    memcpy(last, end - stride, stride);
    size_t block_size = (BLOCK_BYTES / stride) & ~(GROUP - 1);
    if (block_size > BLOCK_MAX) block_size = BLOCK_MAX;
    for (size_t offset = 0; offset < count; offset += block_size) {
        size_t n = count - offset < block_size ? count - offset : block_size;
        size_t aligned = (n + GROUP - 1) & ~(GROUP - 1);
        for (size_t k = 0; k < stride; k++) {
            data = lane(data, end, deltas, aligned);
            if (!data) return "truncated attribute block";
            uint8_t p = last[k];
            for (size_t i = 0; i < n; i++) { p = (uint8_t)(unzigzag8(deltas[i]) + p); block[i * stride + k] = p; }
        }
        memcpy(out + offset * stride, block, n * stride);
        memcpy(last, block + (n - 1) * stride, stride);
    }
    size_t tail = stride < 32 ? 32 : stride;
    return (size_t)(end - data) == tail ? nullptr : "attribute stream has trailing or missing bytes";
}

// varint-7 (LEB128) of at most five bytes; callers guarantee the bytes are readable.
uint32_t vbyte(const uint8_t*& data) {
    uint32_t lead = *data++; if (lead < 128) return lead;
    uint32_t result = lead & 127; unsigned shift = 7;
    for (int i = 0; i < 4; i++) { uint32_t b = *data++; result |= (b & 127) << shift; shift += 7; if (b < 128) break; }
    return result;
}
uint32_t zigzag_index(const uint8_t*& data, uint32_t last) {
    uint32_t v = vbyte(data); return last + ((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
}
void write_index(uint8_t* out, size_t i, size_t size, uint32_t value) {
    for (size_t b = 0; b < size; b++) out[i * size + b] = (uint8_t)(value >> (b * 8));
}

const char* decode_triangles(uint8_t* out, size_t count, size_t size, const uint8_t* buffer, size_t length) {
    if (length < 1 + count / 3 + 16) return "truncated triangle stream";
    if (buffer[0] != 0xe1) return "unsupported triangle codec version";
    uint32_t edges[16][2], vertices[16]; memset(edges, 0xff, sizeof(edges)); memset(vertices, 0xff, sizeof(vertices));
    size_t edge_at = 0, vertex_at = 0; uint32_t next = 0, last = 0;
    const uint8_t* code = buffer + 1; const uint8_t* data = code + count / 3;
    const uint8_t* safe_end = buffer + length - 16; const uint8_t* codeaux = safe_end;
    auto push_edge = [&](uint32_t a, uint32_t b) { edges[edge_at][0] = a; edges[edge_at][1] = b; edge_at = (edge_at + 1) & 15; };
    auto push_vertex = [&](uint32_t v, bool advance = true) { vertices[vertex_at] = v; vertex_at = (vertex_at + advance) & 15; };
    for (size_t i = 0; i < count; i += 3) {
        // a triangle reads at most 16 data bytes, and the 16-byte codeaux table follows safe_end.
        if (data > safe_end) return "truncated triangle data";
        uint8_t tri = *code++; uint32_t a, b, c;
        if (tri < 0xf0) {
            unsigned fe = tri >> 4, fec = tri & 15;
            a = edges[(edge_at - 1 - fe) & 15][0]; b = edges[(edge_at - 1 - fe) & 15][1];
            if (fec < 13) {
                c = fec == 0 ? next : vertices[(vertex_at - 1 - fec) & 15]; next += fec == 0;
                push_vertex(c, fec == 0);
            } else {
                // 13 and 14 step the last free index by -1 and +1; 15 reads a delta.
                last = c = fec != 15 ? last + (fec == 13 ? -1 : 1) : zigzag_index(data, last);
                push_vertex(c);
            }
            push_edge(c, b); push_edge(a, c);
        } else {
            unsigned fea, feb, fec;
            if (tri < 0xfe) {
                uint8_t aux = codeaux[tri & 15]; feb = aux >> 4; fec = aux & 15;
                a = next++;
                b = feb == 0 ? next : vertices[(vertex_at - feb) & 15]; next += feb == 0;
                c = fec == 0 ? next : vertices[(vertex_at - fec) & 15]; next += fec == 0;
                push_vertex(a); push_vertex(b, feb == 0); push_vertex(c, fec == 0);
            } else {
                uint8_t aux = *data++; fea = tri == 0xfe ? 0 : 15; feb = aux >> 4; fec = aux & 15;
                if (aux == 0) next = 0;
                a = fea == 0 ? next++ : 0;
                b = feb == 0 ? next++ : vertices[(vertex_at - feb) & 15];
                c = fec == 0 ? next++ : vertices[(vertex_at - fec) & 15];
                if (fea == 15) last = a = zigzag_index(data, last);
                if (feb == 15) last = b = zigzag_index(data, last);
                if (fec == 15) last = c = zigzag_index(data, last);
                push_vertex(a); push_vertex(b, feb == 0 || feb == 15); push_vertex(c, fec == 0 || fec == 15);
            }
            push_edge(b, a); push_edge(c, b); push_edge(a, c);
        }
        write_index(out, i, size, a); write_index(out, i + 1, size, b); write_index(out, i + 2, size, c);
    }
    return data == safe_end ? nullptr : "triangle stream has trailing or missing bytes";
}

const char* decode_indices(uint8_t* out, size_t count, size_t size, const uint8_t* buffer, size_t length) {
    if (length < 1 + count + 4) return "truncated index stream";
    if (buffer[0] != 0xd1) return "unsupported index codec version";
    const uint8_t* data = buffer + 1; const uint8_t* safe_end = buffer + length - 4;
    uint32_t last[2] = {};
    for (size_t i = 0; i < count; i++) {
        // an index reads at most five bytes; the 4-byte tail follows safe_end.
        if (data >= safe_end) return "truncated index data";
        uint32_t v = vbyte(data), baseline = v & 1; v >>= 1;
        last[baseline] += (v >> 1) ^ (uint32_t)-(int32_t)(v & 1);
        write_index(out, i, size, last[baseline]);
    }
    return data == safe_end ? nullptr : "index stream has trailing or missing bytes";
}

int rounded(float value) { return (int)(value + (value >= 0 ? .5f : -.5f)); }
template <typename T> void filter_octahedral(T* data, size_t count) {
    const float one = (float)((1 << (sizeof(T) * 8 - 1)) - 1);
    for (size_t i = 0; i < count; i++) {
        float x = data[i * 4], y = data[i * 4 + 1], z = data[i * 4 + 2] - fabsf(x) - fabsf(y);
        // fold the lower hemisphere back onto the octahedron.
        float t = z < 0 ? z : 0;
        x += x >= 0 ? t : -t; y += y >= 0 ? t : -t;
        float s = one / sqrtf(x * x + y * y + z * z);
        data[i * 4] = (T)rounded(x * s); data[i * 4 + 1] = (T)rounded(y * s); data[i * 4 + 2] = (T)rounded(z * s);
    }
}
void filter_quaternion(int16_t* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        int16_t* q = data + i * 4;
        float scale = 1.f / sqrtf(2.f) / (float)(q[3] | 3);
        float x = q[0] * scale, y = q[1] * scale, z = q[2] * scale, ww = 1.f - x * x - y * y - z * z;
        float w = sqrtf(ww >= 0 ? ww : 0);
        // the low two bits name the omitted (largest) component's slot.
        unsigned largest = q[3] & 3;
        int16_t values[4] = {(int16_t)rounded(w * 32767.f), (int16_t)rounded(x * 32767.f),
            (int16_t)rounded(y * 32767.f), (int16_t)rounded(z * 32767.f)};
        for (unsigned k = 0; k < 4; k++) q[(largest + k) & 3] = values[k];
    }
}
void filter_exponential(uint8_t* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint32_t v; memcpy(&v, data + i * 4, 4);
        int32_t mantissa = (int32_t)(v << 8) >> 8, exponent = (int32_t)v >> 24;
        float value = ldexpf((float)mantissa, exponent);
        memcpy(data + i * 4, &value, 4);
    }
}
}

const char* meshopt_decode(uint8_t* out, size_t count, size_t stride, const uint8_t* data, size_t length,
    const char* mode, const char* filter) {
    bool attributes = asset_is(mode, "ATTRIBUTES"), triangles = asset_is(mode, "TRIANGLES"), indices = asset_is(mode, "INDICES");
    bool none = !filter || asset_is(filter, "NONE"), oct = asset_is(filter, "OCTAHEDRAL"),
        quat = asset_is(filter, "QUATERNION"), exp = asset_is(filter, "EXPONENTIAL");
    if (!attributes && !triangles && !indices) return "unknown meshopt mode";
    if (!none && !oct && !quat && !exp) return "unknown meshopt filter";
    if (!count) return "meshopt count must be positive";
    if (attributes && (stride % 4 || stride > 256 || !stride)) return "meshopt attribute stride must be a multiple of 4 up to 256";
    if (!attributes && ((stride != 2 && stride != 4) || !none)) return "meshopt index streams require stride 2 or 4 and no filter";
    if (triangles && count % 3) return "meshopt triangle count must be a multiple of three";
    if ((oct && stride != 4 && stride != 8) || (quat && stride != 8)) return "meshopt filter does not match the stride";
    // little-endian component reinterpretation below assumes the host byte order matches glTF.
    const char* error = attributes ? decode_attributes(out, count, stride, data, length) :
        triangles ? decode_triangles(out, count, stride, data, length) : decode_indices(out, count, stride, data, length);
    if (error) return error;
    if (oct && stride == 4) filter_octahedral((int8_t*)out, count);
    else if (oct) filter_octahedral((int16_t*)out, count);
    else if (quat) filter_quaternion((int16_t*)out, count);
    else if (exp) filter_exponential(out, count * stride / 4);
    return nullptr;
}
}
