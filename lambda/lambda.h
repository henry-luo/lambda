#pragma once
// #include <math.h>  // MIR has problem parsing math.h

// Include standard integer types from system
#include <stdint.h>
#include <stddef.h>

// Keep a fallback for freestanding C consumers that do not provide stddef.h.
#if !defined(__cplusplus) && !defined(_SIZE_T) && !defined(_SIZE_T_) && !defined(__SIZE_T__) && !defined(_SYS__TYPES_H_)
typedef uint64_t size_t;
#endif

#if !defined(__cplusplus) && !defined(_STDBOOL_H) && !defined(_STDBOOL_H_) && !defined(__bool_true_false_are_defined)
#define bool uint8_t
#define true 1
#define false 0
#endif

#define null 0

// C math declarations for freestanding C consumers.
#if !defined(__cplusplus)
extern double sin(double x);
extern double cos(double x);
extern double tan(double x);
extern double sqrt(double x);
extern double log(double x);
extern double log10(double x);
extern double exp(double x);
extern double fabs(double x);
extern double floor(double x);
extern double ceil(double x);
extern double round(double x);
// inverse trigonometric
extern double asin(double x);
extern double acos(double x);
extern double atan(double x);
extern double atan2(double y, double x);
// hyperbolic
extern double sinh(double x);
extern double cosh(double x);
extern double tanh(double x);
// inverse hyperbolic
extern double asinh(double x);
extern double acosh(double x);
extern double atanh(double x);
// exponential/logarithmic variants
extern double exp2(double x);
extern double expm1(double x);
extern double log2(double x);
// root
extern double cbrt(double x);
// truncation / misc
extern double trunc(double x);
extern double fmod(double x, double y);
extern double hypot(double y, double x);
extern double log1p(double x);
#endif

// Dry-run mode: when enabled, IO functions return fabricated results
// instead of performing actual network/filesystem operations
#if !defined(__cplusplus)
extern bool g_dry_run;
#endif

// Stack overflow protection (callable from JIT-compiled code)
#ifdef __cplusplus
extern "C" {
#endif
void lambda_stack_overflow_error(const char* func_name);
void lambda_root_frame_overflow_error(void);
#ifdef __cplusplus
}
#endif

// Name pool configuration
#define NAME_POOL_SYMBOL_LIMIT 32  // Max length for symbols in name_pool

// TCO (Tail Call Optimization) iteration limit
// Guards against infinite loops from tail-recursive functions that never terminate.
// TCO converts tail calls into goto loops, bypassing signal-based stack overflow
// detection, so we use an explicit counter.
#define LAMBDA_TCO_MAX_ITERATIONS 1000000

enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    // JavaScript-specific scalar sentinel, distinct from Lambda null.
    LMD_TYPE_UNDEFINED,  // JavaScript undefined (distinct from null)

    // scalar types
    LMD_TYPE_BOOL,
    // Sized numeric types
    LMD_TYPE_NUM_SIZED,  // inline sized numerics (i8..u32, f16, f32) — packed in Item
    LMD_TYPE_INT,    // int literal, just 32-bit
    LMD_TYPE_INT64,  // int literal, 64-bit
    LMD_TYPE_UINT64, // unsigned 64-bit integer (number-home or owned pointer)
    LMD_TYPE_FLOAT,  // float literal, 64-bit
    LMD_TYPE_FLOAT64, // legacy reserved tag; f64 syntax canonicalizes to LMD_TYPE_FLOAT
    LMD_TYPE_DECIMAL,
    LMD_TYPE_DTIME,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_STRING,
    LMD_TYPE_BINARY,
    // A GC-managed pair of binary64 components.  It is a direct pointer Item
    // so the payload stays distinct from the self-tagged float encoding.
    LMD_TYPE_COMPLEX,

    // Path type for file/URL paths
    LMD_TYPE_PATH,  // segmented path with scheme (file, http, https, sys, etc.)

    // container types, LMD_TYPE_CONTAINER
    LMD_TYPE_RANGE,
    LMD_TYPE_ARRAY_NUM,   // unified numeric array (elem_type selects int/int64/float)
    LMD_TYPE_ARRAY,  // array of Items
    LMD_TYPE_MAP,
    LMD_TYPE_VMAP,  // virtual map with vtable dispatch (hashmap, treemap, etc.)
    LMD_TYPE_ELEMENT,
    LMD_TYPE_OBJECT,  // object = map + type_name + methods (nominally-typed)
    LMD_TYPE_TYPE,
    LMD_TYPE_FUNC,

    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,

    LMD_TYPE_COUNT,  // number of type IDs — must be last before HEAP_START
    LMD_CONTAINER_HEAP_START, // special value for container heap entry start
};
typedef uint8_t TypeId;

// Pointer-backed semantic values keep their raw pointer carrier when nullable;
// zero is the lane spelling of null. Numeric/wide scalar tags are deliberately
// excluded because their pointer payloads have distinct ownership rules.
static inline bool lambda_type_id_has_pointer_lane(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_DTIME:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_STRING:
    case LMD_TYPE_BINARY:
    case LMD_TYPE_COMPLEX:
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_TYPE:
    case LMD_TYPE_FUNC:
        return true;
    default:
        return false;
    }
}

// Item tag-space partition for the self-tagged double representation
// (`vibe/Lambda_Type_Double_Boxing.md` §2.3) and the rotated inline-int
// representation (`vibe/Lambda_Type_Int_Boxing.md` §3). The 64-bit space splits
// by its top three bits:
//
//   001-011, 101-111  inline floats — raw IEEE bits, the Item *is* the double
//   000 (0x00-0x1F)   every TypeId tag, raw container pointers, sentinels
//   100 (0x80-0x9F)   inline ints — a double's bits rotated left by one
//
// Non-double Item tags must keep high-byte bits 6 and 5 clear; raw container
// pointers remain bit-identical pointers and are never tagged or masked.
#define ITEM_DBL_MASK        UINT64_C(0x6000000000000000)
#define ITEM_HIGH_BYTE_MASK  UINT64_C(0xFF00000000000000)
#define ITEM_TAG_IS_NON_DOUBLE(tag)  ((((uint8_t)(tag)) & 0x60u) == 0)

// The inline-int octant. A tagged Item is an inline int iff it is not a double
// (bits 62-61 clear) and bit 63 is set, so the discriminator is a sign test on
// the word — see `lambda_item_is_inline_int`. Tags therefore must stay in
// 0x00-0x1F; 0x80-0x9F is no longer tag headroom.
// v5 retired the `100` octant reservation: rotation is gone, so high bytes
// 0x80-0x9F are ordinary reserved tag headroom again. The predicate survives as
// the tag-space assert below (no tag may collide with the retired octant, which
// keeps the JS sentinel relocation of P2 honest and the space re-usable).
#define ITEM_TAG_IS_NOT_INLINE_INT(tag)  ((((uint8_t)(tag)) & 0x80u) == 0)

// Every legal tag byte, TypeIds and reserved sentinel tags alike. Per-tag
// tables must be sized by this, not by LMD_TYPE_COUNT.
#define LAMBDA_TAG_SPACE_SIZE  0x20

#ifdef __cplusplus
#define LAMBDA_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define LAMBDA_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

LAMBDA_STATIC_ASSERT(sizeof(uintptr_t) == sizeof(uint64_t),
                     "Lambda Item requires 64-bit pointers");
// TypeIds must fit the 000 octant (0x00-0x1F). This is now a hard ceiling, not
// a convenience: 0x80-0x9F used to be spare tag headroom but belongs to the
// inline-int encoding, so a TypeId may never be allocated there.
LAMBDA_STATIC_ASSERT(LMD_TYPE_COUNT <= 0x20,
                     "Lambda TypeIds must stay out of double discriminator space");
LAMBDA_STATIC_ASSERT(LMD_CONTAINER_HEAP_START <= 0x20,
                     "TypeId space must stay inside the 000 octant; 0x80-0x9F is inline-int space");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_RAW_POINTER), "raw pointer tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_NULL), "null tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_UNDEFINED), "undefined tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_BOOL), "bool tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_NUM_SIZED), "sized numeric tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_INT), "int tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_INT64), "int64 tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_UINT64), "uint64 tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_FLOAT), "float tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_FLOAT64), "float64 tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_DECIMAL), "decimal tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_DTIME), "datetime tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_SYMBOL), "symbol tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_STRING), "string tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_BINARY), "binary tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_COMPLEX), "complex tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_PATH), "path tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_RANGE), "range tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_ARRAY_NUM), "array-num tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_ARRAY), "array tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_MAP), "map tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_VMAP), "vmap tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_ELEMENT), "element tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_OBJECT), "object tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_TYPE), "type tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_FUNC), "function tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_ANY), "any tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE(LMD_TYPE_ERROR), "error tag must be non-double");

// ============================================================================
// Sized numeric sub-types (stored in bits [55:48] of NUM_SIZED Items)
// ============================================================================
enum EnumNumSizedType {
    NUM_INT8 = 0,     // signed 8-bit   [-128, 127]
    NUM_INT16,        // signed 16-bit  [-32768, 32767]
    NUM_INT32,        // signed 32-bit  [-2^31, 2^31-1]
    NUM_UINT8,        // unsigned 8-bit  [0, 255]
    NUM_UINT16,       // unsigned 16-bit [0, 65535]
    NUM_UINT32,       // unsigned 32-bit [0, 2^32-1]
    NUM_FLOAT16,      // IEEE 754 half-precision (16-bit)
    NUM_FLOAT32,      // IEEE 754 single-precision (32-bit)
    NUM_SIZED_COUNT
};
typedef uint8_t NumSizedType;

// ============================================================================
// ArrayNum element sub-types (stored in Container.map_kind/elem_type byte)
// Container lifecycle flags and ArrayNum layout flags live in separate bytes.
// ============================================================================
enum EnumArrayNumElemType {
    // Lambda's standard numeric types (8 bytes/element each):
    ELEM_INT   = 0x00,   // 8 bytes  — v5 IntLane i64 (finite values or poison sentinels)
    ELEM_FLOAT64 = 0x10, // 8 bytes  — canonical double lane (was ARRAY_FLOAT)
    ELEM_FLOAT = ELEM_FLOAT64, // source compatibility alias; not a distinct representation

    // Compact sized integer types:
    ELEM_INT8    = 0x20,  // 1 byte   — maps to NUM_INT8
    ELEM_INT16   = 0x30,  // 2 bytes  — maps to NUM_INT16
    ELEM_INT32   = 0x40,  // 4 bytes  — maps to NUM_INT32
    ELEM_INT64   = 0x50,  // 8 bytes  — int64 (was ARRAY_INT64)

    ELEM_UINT8   = 0x60,  // 1 byte   — maps to NUM_UINT8
    ELEM_UINT16  = 0x70,  // 2 bytes  — maps to NUM_UINT16
    ELEM_UINT32  = 0x80,  // 4 bytes  — maps to NUM_UINT32
    ELEM_UINT64  = 0x90,  // 8 bytes  — maps to NUM_UINT64

    // Compact sized float types:
    ELEM_FLOAT16 = 0xA0,  // 2 bytes  — maps to NUM_FLOAT16
    ELEM_FLOAT32 = 0xB0,  // 4 bytes  — maps to NUM_FLOAT32
    ELEM_RESERVED_C0 = 0xC0, // retired duplicate double lane; keep the slot unavailable

    // Boolean type (1 byte/element):
    ELEM_BOOL    = 0xD0,  // 1 byte   — bool values, distinct from UINT8 for any()/all() semantics

    // JS-compatible clamped byte type:
    ELEM_UINT8_CLAMPED = 0xE0,  // 1 byte — Uint8ClampedArray storage semantics

    ELEM_NUM_COUNT = 14
};
typedef uint8_t ArrayNumElemType;

// Bytes per element, indexed by (elem_type >> 4)
static const uint8_t ELEM_TYPE_SIZE[16] = {
    8, // 0x00 ELEM_INT     — int64_t (v5: the int lane is the machine i64)
    8, // 0x10 ELEM_FLOAT64 — double
    1, // 0x20 ELEM_INT8
    2, // 0x30 ELEM_INT16
    4, // 0x40 ELEM_INT32
    8, // 0x50 ELEM_INT64   — int64_t
    1, // 0x60 ELEM_UINT8
    2, // 0x70 ELEM_UINT16
    4, // 0x80 ELEM_UINT32
    8, // 0x90 ELEM_UINT64
    2, // 0xA0 ELEM_FLOAT16
    4, // 0xB0 ELEM_FLOAT32
    0, // 0xC0 reserved retired duplicate double lane
    1, // 0xD0 ELEM_BOOL
    1, // 0xE0 ELEM_UINT8_CLAMPED
    0, // 0xF0 reserved
};

// Convert NumSizedType to ArrayNumElemType
static inline ArrayNumElemType num_sized_to_elem_type(NumSizedType nst) {
    switch (nst) {
        case NUM_INT8:    return ELEM_INT8;
        case NUM_INT16:   return ELEM_INT16;
        case NUM_INT32:   return ELEM_INT32;
        case NUM_UINT8:   return ELEM_UINT8;
        case NUM_UINT16:  return ELEM_UINT16;
        case NUM_UINT32:  return ELEM_UINT32;
        case NUM_FLOAT16: return ELEM_FLOAT16;
        case NUM_FLOAT32: return ELEM_FLOAT32;
        default:          return ELEM_INT;
    }
}

// Check if an elem_type uses compact (sub-8-byte) storage
static inline int elem_type_is_compact(ArrayNumElemType et) {
    return ELEM_TYPE_SIZE[et >> 4] < 8;
}

// Sized numeric packing: value in [31:0], sub-type in [55:48], type_id in [63:56]
// Bits [47:32] are unused padding.
#define NUM_SIZED_PACK(num_type, val32) \
    (((uint64_t)LMD_TYPE_NUM_SIZED << 56) | ((uint64_t)(num_type) << 48) | ((uint32_t)(val32)))

// Unpack sub-type and raw 32-bit value from a NUM_SIZED Item
#define NUM_SIZED_SUBTYPE(item)  ((uint8_t)(((uint64_t)(item) >> 48) & 0xFF))
#define NUM_SIZED_RAW32(item)    ((uint32_t)((uint64_t)(item) & 0xFFFFFFFF))

// Convenience packers for each sub-type
#define i8_to_item(v)   NUM_SIZED_PACK(NUM_INT8,   (uint32_t)(uint8_t)(int8_t)(v))
#define i16_to_item(v)  NUM_SIZED_PACK(NUM_INT16,  (uint32_t)(uint16_t)(int16_t)(v))
#define i32_to_item(v)  NUM_SIZED_PACK(NUM_INT32,  (uint32_t)(int32_t)(v))
#define u8_to_item(v)   NUM_SIZED_PACK(NUM_UINT8,  (uint32_t)(uint8_t)(v))
#define u16_to_item(v)  NUM_SIZED_PACK(NUM_UINT16, (uint32_t)(uint16_t)(v))
#define u32_to_item(v)  NUM_SIZED_PACK(NUM_UINT32, (uint32_t)(v))

// uint64 Items point at a raw 64-bit payload owned by a number home or an
// explicit persistent owner. Transient producers must use box_uint64_value().
#define u64_to_item(uint64_ptr) \
    lambda_uint64_ptr_to_item_bits((const uint64_t*)(uint64_ptr))

// Get human-readable name for a NumSizedType sub-type
#ifdef __cplusplus
extern "C"
#endif
const char* get_num_sized_type_name(NumSizedType num_type);

// TypeKind enum moved to lambda.hpp (not needed by JIT)

// Get human-readable name for a TypeId (for error messages)
// Implemented in lambda-data.cpp (not inlined — saves ~30 lines from JIT-embedded header)
#ifdef __cplusplus
extern "C"
#endif
const char* get_type_name(TypeId type_id);

// 3-state boolean:
typedef enum {
    BOOL_FALSE = 0,
    BOOL_TRUE = 1,
    BOOL_ERROR = 2
} BoolEnum;
typedef uint8_t Bool;

#define  LMD_TYPE_CONTAINER LMD_TYPE_RANGE

// System function identifiers (moved from lambda-data.hpp for C compatibility)
typedef enum SysFunc {
    SYSFUNC_LEN,
    SYSFUNC_TYPE,
    SYSFUNC_NAME,       // name(item) - get local name of element, function, or type
    SYSFUNC_INT,
    SYSFUNC_INT64,
    SYSFUNC_FLOAT,
    SYSFUNC_COMPLEX,
    SYSFUNC_COMPLEX2,
    SYSFUNC_REAL,
    SYSFUNC_IMAG,
    SYSFUNC_CONJ,
    SYSFUNC_DECIMAL,
    SYSFUNC_NUMBER,
    SYSFUNC_STRING,
    //SYSFUNC_CHAR,
    SYSFUNC_SYMBOL,
    SYSFUNC_SYMBOL2,    // symbol(name, url) - 2 args, create namespaced symbol
    SYSFUNC_BINARY,
    SYSFUNC_DATETIME,
    SYSFUNC_DATETIME0,  // datetime() - 0 args, current datetime
    SYSFUNC_DATE,
    SYSFUNC_DATE0,      // date() - 0 args, current date
    SYSFUNC_DATE3,      // date(y,m,d) - 3 args, construct from components
    SYSFUNC_TIME,
    SYSFUNC_TIME0,      // time() - 0 args, current time
    SYSFUNC_TIME3,      // time(h,m,s) - 3 args, construct from components
    SYSFUNC_JUSTNOW,
    SYSFUNC_SET,
    SYSFUNC_SLICE,
    SYSFUNC_VIEW,             // subview(arr, start, end) - read-only view sharing arr's storage
    SYSFUNC_IS_VIEW,          // is_view(arr) - check if arr is a view
    SYSFUNC_RESHAPE,          // reshape(arr, shape_list) - view with new shape
    SYSFUNC_SHAPE,            // shape(arr) - list of dimensions
    SYSFUNC_NDIM,             // ndim(arr) - number of dimensions
    SYSFUNC_SUM2,             // sum(arr, axis) - reduce along axis
    SYSFUNC_AVG2,             // avg(arr, axis) - mean along axis
    SYSFUNC_PROD2,            // math.prod(arr, axis)
    SYSFUNC_MEAN2,            // math.mean(arr, axis)
    SYSFUNC_CUMSUM2,          // math.cumsum(arr, axis)
    SYSFUNC_CUMPROD2,         // math.cumprod(arr, axis)
    SYSFUNC_TRANSPOSE,        // transpose(arr) - view with reversed axes (zero-copy)
    SYSFUNC_FLATTEN,          // flatten(arr) - owned 1-D contiguous copy
    SYSFUNC_RAVEL,            // ravel(arr) - 1-D view if contiguous, else copy
    SYSFUNC_MATMUL,           // matmul(a, b) - matrix product (2-D·2-D, 1-D dot)
    SYSFUNC_CONCAT,           // concat(a, b) - join along axis 0
    SYSFUNC_STACK,            // stack(a, b) - stack along a new leading axis
    // image stencil engine (N-D windowed neighbourhood ops over ArrayNum)
    SYSFUNC_CONVOLVE,         // convolve(img, kernel) - weighted-sum (correlation)
    SYSFUNC_BLUR,             // blur(img, ksize) - box (mean) blur
    SYSFUNC_ERODE,            // erode(img, ksize) - morphological min
    SYSFUNC_DILATE,           // dilate(img, ksize) - morphological max
    SYSFUNC_MEDIAN_FILT,      // median_filter(img, ksize) - rank (median) filter
    SYSFUNC_MAXPOOL,          // maxpool(img, ksize) - strided max pooling
    SYSFUNC_AVGPOOL,          // avgpool(img, ksize) - strided mean pooling
    // image I/O bridge
    SYSFUNC_LOAD_IMAGE,       // load(path) - decode PNG/JPEG/GIF to (H,W,4) ubyte
    SYSFUNC_SAVE_IMAGE,       // save(img, path) - encode an image to PNG
    SYSFUNC_AS_FLOAT,         // as_float(img) - ubyte [0,255] -> float [0,1]
    SYSFUNC_AS_UBYTE,         // as_ubyte(img) - float [0,1] -> ubyte [0,255]
    // point / colour / geometric image ops
    SYSFUNC_INVERT,           // invert(img) - photographic negative
    SYSFUNC_GAMMA,            // gamma(img, g) - gamma correction
    SYSFUNC_THRESHOLD,        // threshold(img, t) - binarize at t
    SYSFUNC_GRAYSCALE,        // grayscale(img) - RGB -> luma (H,W)
    SYSFUNC_FLIP,             // flip(img, axis) - mirror along axis 0/1
    SYSFUNC_ROT90,            // rot90(img, k) - rotate CCW by 90*k
    SYSFUNC_CROP,             // crop(img, rows, cols) - region copy
    // histogram / segmentation / resize / warp
    SYSFUNC_HISTOGRAM,        // histogram(img, bins) - value counts
    SYSFUNC_OTSU,             // otsu(img) - optimal threshold value
    SYSFUNC_LABEL,            // label(mask) - 4-connected components
    SYSFUNC_RESIZE,           // resize(img, h, w) - bilinear resample
    SYSFUNC_ROTATE,           // rotate(img, deg) - bilinear rotation
    SYSFUNC_AFFINE_WARP,      // affine_warp(img, M) - 2x3 affine gather
    SYSFUNC_ALL,
    SYSFUNC_ANY,
    SYSFUNC_MIN1,
    SYSFUNC_MIN2,
    SYSFUNC_MAX1,
    SYSFUNC_MAX2,
    SYSFUNC_SUM,
    SYSFUNC_AVG,
    SYSFUNC_ABS,
    SYSFUNC_ROUND,
    SYSFUNC_FLOOR,
    SYSFUNC_CEIL,
    SYSFUNC_INPUT1,
    SYSFUNC_INPUT2,
    SYSFUNC_FORMAT1,
    SYSFUNC_FORMAT2,
    SYSFUNC_ERROR,
    SYSFUNC_EXISTS,         // exists(path) - check if file/dir exists
    SYSFUNC_NORMALIZE,
    SYSFUNC_NORMALIZE2,     // normalize(str, form) with 2 args
    // string functions
    SYSFUNC_CONTAINS,
    SYSFUNC_STARTS_WITH,
    SYSFUNC_ENDS_WITH,
    SYSFUNC_INDEX_OF,
    SYSFUNC_LAST_INDEX_OF,
    SYSFUNC_TRIM,
    SYSFUNC_TRIM_START,
    SYSFUNC_TRIM_END,
    SYSFUNC_LOWER,
    SYSFUNC_UPPER,
    SYSFUNC_URL_RESOLVE,
    SYSFUNC_SPLIT,
    SYSFUNC_SPLIT3,         // split(str, sep, keep_delim) with 3 args
    SYSFUNC_JOIN,           // join(strs, sep) for strings
    SYSFUNC_REPLACE,
    SYSFUNC_REPLACE4,       // replace(str, old, new, options) - with options
    SYSFUNC_FIND,           // find(str, pattern) - find all matches
    SYSFUNC_FIND3,          // find(str, pattern, options) - with options
    SYSFUNC_ORD,            // ord(str) - Unicode code point of first character
    SYSFUNC_CHR,            // chr(int) - character from Unicode code point
    // vector functions
    SYSFUNC_PROD,
    SYSFUNC_CUMSUM,
    SYSFUNC_CUMPROD,
    SYSFUNC_ARGMIN,
    SYSFUNC_ARGMAX,
    SYSFUNC_FILL,
    SYSFUNC_DOT,
    SYSFUNC_NORM,
    // statistical functions
    SYSFUNC_MEAN,
    SYSFUNC_MEDIAN,
    SYSFUNC_MEDIAN2,
    SYSFUNC_VARIANCE,
    SYSFUNC_VARIANCE2,
    SYSFUNC_DEVIATION,
    SYSFUNC_DEVIATION2,
    // element-wise math functions
    SYSFUNC_SQRT,
    SYSFUNC_LOG,
    SYSFUNC_LOG10,
    SYSFUNC_EXP,
    SYSFUNC_SIN,
    SYSFUNC_COS,
    SYSFUNC_TAN,
    // inverse trigonometric
    SYSFUNC_ASIN,
    SYSFUNC_ACOS,
    SYSFUNC_ATAN,
    SYSFUNC_ATAN2,
    // hyperbolic
    SYSFUNC_SINH,
    SYSFUNC_COSH,
    SYSFUNC_TANH,
    // inverse hyperbolic
    SYSFUNC_ASINH,
    SYSFUNC_ACOSH,
    SYSFUNC_ATANH,
    // exponential/logarithmic variants
    SYSFUNC_EXP2,
    SYSFUNC_EXPM1,
    SYSFUNC_LOG2,
    // power/root
    SYSFUNC_POW_MATH,
    SYSFUNC_CBRT,
    SYSFUNC_TRUNC,
    SYSFUNC_HYPOT,
    SYSFUNC_LOG1P,
    SYSFUNC_SIGN,
    SYSFUNC_CLIP,           // clip(x, lo, hi) - element-wise clamp to [lo, hi]
    // random number generation
    SYSFUNC_RANDOM,
    // vector manipulation functions
    SYSFUNC_REVERSE,
    SYSFUNC_SORT,
    SYSFUNC_SORT2,
    SYSFUNC_UNIQUE,
    SYSFUNC_TAKE,
    SYSFUNC_DROP,
    SYSFUNC_ZIP,
    SYSFUNC_RANGE3,
    SYSFUNC_QUANTILE,
    SYSFUNC_QUANTILE3,
    SYSFUNC_REDUCE,         // reduce(collection, fn) - fold/accumulate
    // parse string functions
    SYSFUNC_PARSE1,         // parse(str) - parse string, auto-detect format
    SYSFUNC_PARSE2,         // parse(str, format) - parse string with format
    SYSFUNC_PARSE_HTML_FRAGMENT,
    // variadic parameter access
    SYSFUNC_VARG0,          // varg() - get all variadic args as list
    SYSFUNC_VARG1,          // varg(n) - get nth variadic arg
    // bitwise functions
    SYSFUNC_BAND,
    SYSFUNC_BOR,
    SYSFUNC_BXOR,
    SYSFUNC_BNOT,
    SYSFUNC_SHL,
    SYSFUNC_SHR,
    SYSFUNC_USHR,
    SYSFUNC_TO_PROMISE,       // toPromise(handle) - JS Promise adapter
    // procedural functions
    SYSPROC_NOW,
    SYSPROC_TODAY,
    SYSPROC_PRINT,
    SYSPROC_FETCH,
    SYSPROC_OUTPUT2,         // output(source, target) - writes data to target, returns bytes
    SYSPROC_OUTPUT3,         // output(source, target, options) - with options map
    SYSPROC_CMD,
    SYSPROC_CMD1,            // cmd(command) - no args version
    // io module functions (unified I/O - supports local and remote targets)
    SYSPROC_IO_COPY,
    SYSPROC_IO_READ,          // io.read(target) - async text read
    SYSPROC_IO_MOVE,
    SYSPROC_IO_DELETE,
    SYSPROC_IO_MKDIR,
    SYSPROC_IO_TOUCH,
    SYSPROC_IO_SYMLINK,
    SYSPROC_IO_CHMOD,
    SYSPROC_IO_RENAME,
    SYSPROC_IO_FETCH,        // io.fetch(target, options) - fetch data from URL or file
    // io.http module (web server)
    SYSPROC_IO_HTTP_CREATE_SERVER,  // io.http.create_server(config?) - create HTTP server
    SYSPROC_IO_HTTP_LISTEN,         // io.http.listen(server, port) - start listening
    SYSPROC_IO_HTTP_ROUTE,          // io.http.route(server, method, path, handler)
    SYSPROC_IO_HTTP_USE,            // io.http.use(server, middleware) - add middleware
    SYSPROC_IO_HTTP_STATIC,         // io.http.static(server, url_path, dir_path)
    SYSPROC_IO_HTTP_STOP,           // io.http.stop(server) - graceful shutdown
    // vmap functions
    SYSFUNC_VMAP_NEW,        // map() or map([k1,v1,...]) - create VMap
    SYSPROC_VMAP_SET,        // m.set(k, v) - in-place insert on VMap (procedural)
    SYSFUNC_JUBE_MODULE,     // descriptor-backed native module function
    SYSPROC_CLOCK,           // clock() - high-resolution monotonic time in seconds (float)
    // file-based find/replace (procedural)
    SYSPROC_REPLACE_FILE,    // pn replace(path, pattern, repl) - sed-like file replace
    SYSPROC_REPLACE_FILE4,   // pn replace(path, pattern, repl, options)
    // view/edit template apply
    SYSFUNC_APPLY1,          // apply(target) - apply view templates to target
    SYSFUNC_APPLY2,          // apply(target, options) - apply with options map
    // edit bridge — MarkEditor operations (Phase 4)
    SYSFUNC_EDIT_UNDO,       // undo() - undo last edit commit
    SYSFUNC_EDIT_REDO,       // redo() - redo last undone commit
    SYSFUNC_EDIT_COMMIT,     // commit() - commit current edits as version
    SYSFUNC_EDIT_COMMIT1,    // commit(description) - commit with description
    // reactive UI event dispatch
    SYSPROC_EMIT,            // emit(event_name, data) - dispatch event to parent template handler
    SYSPROC_SET_SELECTION,   // set_selection(sel) - push editor selection back to DomSelection (Phase R4 §7.4)
    SYSFUNC_PDF_PARSE_CONTENT_STREAM,  // pdf_parse_content_stream(bytes) - fast PDF content tokenizer
    SYSFUNC_PDF_REGISTER_SVG_IMAGE_RESOLVER,  // pdf_register_svg_image_resolver(svg, pdf) - bind PDF image handles to SVG root
    SYSPROC_PUSH,            // push(arr, val) - append val to a growable array in place (procedural)
    SYSPROC_SPLICE,          // splice(arr, start, count) - remove count elements at start, in place (procedural)
    SYSPROC_SEND,
    SYSPROC_RECEIVE,
    SYSPROC_WAIT,
    SYSPROC_SELECT,
    SYSPROC_SLEEP,
    SYSPROC_SELF,
    SYSPROC_CANCEL,
} SysFunc;

typedef struct Type {
    TypeId type_id;
    uint8_t kind:4;      // TypeKind: sub-classification (SIMPLE, UNARY, BINARY, PATTERN)
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} Type;

typedef struct Container Container;
typedef struct Range Range;
typedef struct List List;
typedef struct List Array;
typedef struct ArrayNum ArrayNum;
typedef ArrayNum ArrayInt;    // compat alias: `int` arrays, double lane (elem_type == ELEM_INT)
typedef ArrayNum ArrayInt64;  // compat alias: int64 arrays (elem_type == ELEM_INT64)
typedef ArrayNum ArrayFloat;  // compat alias: float arrays (elem_type == ELEM_FLOAT64)
typedef struct Map Map;
typedef struct SparseArrayMap SparseArrayMap;
typedef struct VMap VMap;
typedef struct Element Element;
typedef struct Object Object;
typedef struct Function Function;
typedef struct Decimal Decimal;
typedef struct Complex Complex;
typedef struct TypePattern TypePattern;
typedef struct ByteStorage ByteStorage;
typedef struct ByteBufferHandle ByteBufferHandle;

/*
* The C verion of Lambda Item and data structures are defined primarily for MIR JIT ciompiler
*/

// only define DateTime if not already defined by lib/datetime.h
#ifndef __cplusplus
typedef uint64_t DateTime;
typedef uint64_t Item;
#else
#include "../lib/datetime.h"
typedef struct Item Item;
#endif

// a fat string with prefixed length and flags
#ifndef STRING_STRUCT_DEFINED
typedef struct String {
    uint32_t len;       // byte length of the string
    union {
        uint8_t flags;
        struct {
            uint8_t is_ascii:1;   // enables O(1) indexing when every byte is ASCII
            uint8_t is_buffer:1;  // exclusively owned GC string-builder storage
            uint8_t is_pooled:1;  // NameRecord metadata prefix immediately precedes this String
        };
    };
    char chars[];       // UTF-8 string data (null-terminated)
} String;
#define STRING_STRUCT_DEFINED
LAMBDA_STATIC_ASSERT(sizeof(String) == 8, "String header ABI must remain 8 bytes");
LAMBDA_STATIC_ASSERT(offsetof(String, chars) == 5, "String chars ABI must remain byte 5");
#endif

typedef struct Target Target;  // forward declaration for Symbol.ns

typedef struct Symbol {
    uint32_t len;       // symbol name length
    Target* ns;         // namespace target (NULL for unqualified symbols)
    char chars[];       // symbol name characters
} Symbol;
enum BinaryFlags {
    BINARY_FLAG_NONE = 0,
    BINARY_FLAG_INLINE = 1u << 0,
};

// Binary is an immutable byte span. Runtime values retain storage; compiler
// constants use inline_bytes so their lifetime remains owned by the script pool.
typedef struct Binary {
    uint32_t len;
    uint8_t is_ascii;
    uint8_t flags;
    uint16_t reserved;
    ByteStorage* storage;
    size_t offset;
    uint8_t inline_bytes[];
} Binary;
typedef struct LambdaSymbolKeyList SymbolKeyList;

// MapKind: discriminates exotic Map sub-types so js_property_get can
// skip 9 cascading sentinel-pointer checks for plain JS objects.
enum MapKind {
    MAP_KIND_PLAIN       = 0,  // regular JS/Lambda object (default, zero-init safe)
    MAP_KIND_TYPED_ARRAY = 1,  // Int8Array, Float64Array, etc.
    MAP_KIND_ARRAYBUFFER = 2,  // ArrayBuffer / SharedArrayBuffer
    MAP_KIND_DATAVIEW    = 3,  // DataView
    MAP_KIND_WEB_API_RESOURCE = 4,  // non-node Web API resources: Range, Selection, styles
    MAP_KIND_CSSOM       = 5,  // Stylesheet, CSSRule, RuleStyleDeclaration
    MAP_KIND_ITERATOR    = 6,  // Synthetic iterator (array, string, typed array)
    MAP_KIND_PROCESS_ENV = 7,  // process.env — coerces all values to strings on set
    MAP_KIND_COLLECTION  = 8,  // Map/Set/WeakMap/WeakSet with native internal data
    MAP_KIND_PROXY       = 9,  // ES6 Proxy object
    MAP_KIND_RESERVED_10 = 10, // retired foreign-document map carrier
    MAP_KIND_ARRAY_PROPS = 11, // array reserved-tail companion map: stores literal
                               // legacy markers (__get_N/__set_N/__nw_N/...)
                               // — bypasses Phase 4 accessor-marker intercept.
    MAP_KIND_CSS_NAMESPACE = 12, // CSS namespace object; ordinary shape-backed
                                 // properties plus CSS-specific method dispatch.
    MAP_KIND_DESC       = 13, // regular JS/Lambda object with descriptor metadata
    MAP_KIND_ARRAY_SPARSE = 14, // array companion map plus numeric sparse hash table
    MAP_KIND_ERROR       = 15, // resting-state LambdaError presented as a JS object
    MAP_KIND_REGEXP      = 16, // RegExp carrier with typed trailing native payload
};

#define CONTAINER_FLAG_IMMORTAL (1u << 5)
// raw masks remain part of the container ABI for code that snapshots `flags`
// without a Container pointer (notably the moving GC).
#define CONTAINER_FLAG_JS_PROPS (1u << 6)
#define CONTAINER_FLAG_CTOR_RESERVED (1u << 7)

// Lambda COW ownership state lives in the Container header padding. Raw
// mutation APIs deliberately never inspect this byte; only *_cow wrappers do.
enum ContainerCowState {
    COW_STATE_SHARED = 1u << 0,
};

static inline bool map_kind_is_array_props(uint8_t map_kind) {
    return map_kind == MAP_KIND_ARRAY_PROPS || map_kind == MAP_KIND_ARRAY_SPARSE;
}

// Array and List struct defintions needed for for-loop
struct Container {
    TypeId type_id;
    union {
        uint8_t flags;
        struct {
            // lifecycle / allocation flags
            uint8_t is_content:1;        // whether it is a content list, or value list
            uint8_t is_spreadable:1;     // whether this array should be spread when added to collections
            uint8_t is_heap:1;           // whether allocated from runtime heap (vs arena for input docs)
            uint8_t is_data_migrated:1;  // data buffer migrated from input pool to runtime pool (for mutated markup containers)
            uint8_t is_static:1;         // read-only const-pool/static data container
            uint8_t is_immortal:1;       // storage outlives every execution frame (input arena/const pool)
            uint8_t has_js_props:1;      // array/list owns a reserved-tail JS property companion
            uint8_t has_ctor_reserved:1; // map has constructor slots awaiting initialization
        };
    };
    union {
        uint8_t array_flags; // ArrayNum flags
        struct {
            uint8_t is_ndim:1;           // bit 0: has shape side-table in `extra` (n-d owned array)
            uint8_t is_view:1;           // bit 1: aliases another container's storage (implies is_ndim)
            uint8_t is_pinned:1;         // bit 2: reserved legacy pin marker; nursery data is always relocated
            uint8_t is_mutable_view:1;   // bit 3: a view writable through to its base (procedural in-place updates)
            uint8_t is_native_lane_array:1; // bit 4: List.items holds native lane words
            uint8_t array_flag_reserved:3;
        };
    };    
    uint8_t map_kind;      // MapKind tag (0 = plain, only used for map/object/element)
    // these named state bytes occupy the former padding without changing the
    // public eight-byte header or any derived-container field offset.
    uint8_t cow_state;
    uint8_t ctor_reserved_mask_lo;
    uint8_t ctor_reserved_mask_hi;
    uint8_t reserved_state;
};

LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, type_id) == 0,
                     "Container TypeId must remain at byte zero");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, flags) == 1,
                     "Container flags ABI offset changed");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, array_flags) == 2,
                     "Container array flags ABI offset changed");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, map_kind) == 3,
                     "Container map kind ABI offset changed");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, cow_state) == 4,
                     "Container COW state must reuse padding byte zero");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, ctor_reserved_mask_lo) == 5,
                     "Container constructor mask low-byte ABI offset changed");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, ctor_reserved_mask_hi) == 6,
                     "Container constructor mask high-byte ABI offset changed");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Container, reserved_state) == 7,
                     "Container reserved-state ABI offset changed");
LAMBDA_STATIC_ASSERT(sizeof(Container) == 8,
                     "Container header must remain eight bytes");
LAMBDA_STATIC_ASSERT(CONTAINER_FLAG_JS_PROPS == (1u << 6),
                     "Container JS-properties mask must match its bitfield");
LAMBDA_STATIC_ASSERT(CONTAINER_FLAG_CTOR_RESERVED == (1u << 7),
                     "Container constructor-reserved mask must match its bitfield");

// Tune5 §5.1: the high three ArrayNum flag bits carry the per-instance
// ordinary-JS elements state.  Low layout/view bits remain ArrayNum-owned;
// zero deliberately means unmanaged or excluded from JS element semantics.
#define JS_ELEMENTS_STATE_MASK ((uint8_t)0xe0u)
typedef enum JsElementsKind {
    JS_ELEMENTS_NONE = 0x00,
    JS_ELEMENTS_PACKED_NUMERIC = 0x20,
    JS_ELEMENTS_PACKED_TAGGED = 0x40,
    JS_ELEMENTS_HOLEY_TAGGED = 0x60,
    JS_ELEMENTS_SPARSE_TAGGED = 0x80,
} JsElementsKind;

static inline JsElementsKind container_js_elements_kind(const Container* c) {
    return c ? (JsElementsKind)(c->array_flags & JS_ELEMENTS_STATE_MASK)
             : JS_ELEMENTS_NONE;
}

static inline void container_set_js_elements_kind(Container* c,
                                                   JsElementsKind kind) {
    if (!c) return;
    c->array_flags = (uint8_t)((c->array_flags & (uint8_t)~JS_ELEMENTS_STATE_MASK) |
                               ((uint8_t)kind & JS_ELEMENTS_STATE_MASK));
}

static inline bool container_js_elements_kind_is_tagged(JsElementsKind kind) {
    return kind == JS_ELEMENTS_PACKED_TAGGED ||
        kind == JS_ELEMENTS_HOLEY_TAGGED || kind == JS_ELEMENTS_SPARSE_TAGGED;
}

#if defined(__cplusplus)
static_assert(JS_ELEMENTS_STATE_MASK == (uint8_t)(0x20u | 0x40u | 0x80u),
              "Tune5 elements state must occupy only reserved high bits");
#else
_Static_assert(JS_ELEMENTS_STATE_MASK == (uint8_t)(0x20u | 0x40u | 0x80u),
               "Tune5 elements state must occupy only reserved high bits");
#endif

// A descriptor carries the full semantic contract for a packed carrier. TypeId
// alone cannot distinguish int from int?, even though both have LMD_TYPE_INT.
typedef enum LaneStorageKind {
    LANE_STORAGE_INVALID = 0,
    LANE_STORAGE_ITEM,
    LANE_STORAGE_INT,
    LANE_STORAGE_BOOL,
    LANE_STORAGE_SIZED_I64,
    LANE_STORAGE_FLOAT64,
    LANE_STORAGE_POINTER,
} LaneStorageKind;

typedef struct LaneStorageDesc {
    Type* semantic_contract;
    Type* base_contract;
    uint8_t kind;       // LaneStorageKind
    uint8_t nullable;
    uint8_t byte_size;  // packed map width; native Array slots remain 8 bytes
    uint8_t reserved[5];
} LaneStorageDesc;

// List/Array flags (stored in List.flags / Array.flags field)

#ifndef __cplusplus
    struct Range {
        TypeId type_id;
        uint16_t flags;
        uint8_t padding[5];  // padding to align to 8 bytes
        //---------------------
        int64_t start;  // inclusive start
        int64_t end;    // inclusive end
        int64_t length;
    };

    struct List {
        TypeId type_id;
        uint16_t flags;
        uint8_t padding[5];  // padding to align to 8 bytes
        //---------------------
        Item* items;  // pointer to items
        int64_t length;  // number of items
        int64_t extra;   // count of reserved tail items (wide scalars plus optional JS props slot)
        int64_t capacity;  // allocated capacity
    };

    struct ArrayNum {
        TypeId type_id;
        uint16_t flags;  // ArrayNum flags
        uint8_t elem_type;     // ArrayNumElemType (replaces flags for typed arrays)
        uint8_t padding[4];  // padding to align to 8 bytes
        //---------------------
        union {
            int64_t* items;        // for ELEM_INT, ELEM_INT64
            double* float_items;   // for ELEM_FLOAT64
            void* data;            // for compact types (ELEM_INT8, ELEM_UINT8, etc.)
        };
        int64_t length;  // number of elements
        int64_t extra;   // for is_ndim/is_view: ArrayNumShape* in this slot; else count of extra elements
        int64_t capacity;  // allocated capacity
    };

    // ArrayList definition for MIR runtime (item_keys return type)
#ifndef _ARRAYLIST_STRUCT_DEFINED
#define _ARRAYLIST_STRUCT_DEFINED
    typedef void *ArrayListValue;
    struct _ArrayList {
        ArrayListValue *data;
        int length;
        int _alloced;
    };
#endif

    // Map, Object, Element struct definitions for direct field access optimization
    // Layout must match the C++ structs in lambda.hpp exactly
    struct Map {
        TypeId type_id;
        uint16_t flags;
        uint8_t map_kind;
        uint8_t padding[4];  // padding to align to 8 bytes
        //---------------------
        void* type;       // TypeMap* — shape/type info
        void* data;       // packed data struct of the map fields
        int data_cap;     // capacity of the data buffer
    };

    struct SparseArrayMap {
        struct Map base;              // must remain first; array props slot points to base
        struct hashmap* sparse_indices; // numeric sparse array data entries
        int64_t sparse_version;       // increments on numeric sparse mutations
    };

    struct Object {
        TypeId type_id;
        uint16_t flags;
        uint8_t map_kind;
        uint8_t padding[4];  // padding to align to 8 bytes
        //---------------------
        void* type;       // TypeObject* — shape + methods + type_name
        void* data;       // packed field data (same layout as Map)
        int data_cap;     // data buffer capacity
    };

    struct Element {
        TypeId type_id;
        uint16_t flags;
        uint8_t map_kind;
        uint8_t padding[4];  // padding to align to 8 bytes
        //---------------------
        Item* items;       // list content items
        int64_t length;    // number of content items
        int64_t extra;     // count of extra items
        int64_t capacity;  // allocated capacity
        //---------------------
        void* type;        // TypeElmt* — attr type/shape
        void* data;        // packed data struct of the attrs
        int data_cap;      // capacity of the data buffer
    };

#endif

// ============================================================================
// ArrayNumShape — side table for N-D arrays and views.
// Pointer stored in ArrayNum.extra when Container.is_ndim or is_view is set.
// Layout:
//   [header fields ...]
//   int64_t shape[ndim]      — element count per dimension
//   int64_t strides[ndim]    — stride per dimension, in *elements* (not bytes)
// Allocated via pool/heap, freed in ArrayNum finalizer.
// ============================================================================
typedef enum ArrayNumBackingKind {
    ARRAY_NUM_BACKING_GC_OWNED = 0,
    ARRAY_NUM_BACKING_GC_VIEW = 1,
    ARRAY_NUM_BACKING_EXTERNAL_BORROWED = 2,
    ARRAY_NUM_BACKING_BUFFER_HANDLE = 3,
    ARRAY_NUM_BACKING_BYTE_STORAGE = 4,
} ArrayNumBackingKind;

typedef struct ArrayNumShape {
    uint8_t  ndim;            // number of dimensions (1..32)
    uint8_t  is_c_contig:1;   // contiguous in row-major
    uint8_t  is_f_contig:1;   // contiguous in column-major
    uint8_t  reserved:6;
    uint8_t  backing_kind;    // ArrayNumBackingKind; never infer backing from base
    uint8_t  backing_padding[5];
    int64_t  offset;          // element offset within base->data (0 for owned)
    void*    base;            // Container* — non-NULL for views; NULL for owned N-D arrays
    void*    backing;         // ByteBufferHandle*/ByteStorage* for tagged external modes
    uint64_t resolved_generation; // last ByteBufferHandle generation cached in ArrayNum.data
    int64_t  data[];          // shape[ndim] followed by strides[ndim] — total 2*ndim entries
} ArrayNumShape;

static inline int64_t* array_num_shape_dims(ArrayNumShape* s) { return s->data; }
static inline int64_t* array_num_shape_strides(ArrayNumShape* s) { return s->data + s->ndim; }

Range* range();
long range_get(Range *range, int64_t index);

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
void list_push_spread(List *list, Item item);  // push item, spreading if spreadable array
Item list_end(List *list);

// Spreadable array functions for for-expression results
Array* array_plain();  // constructs a plain empty array (no frame management)
#ifdef __cplusplus
extern "C" {
#endif
void array_set(Array* arr, int64_t index, Item item);
void array_copy_owned_items(Array* destination, int64_t destination_index,
                            const Item* source, int64_t count);
bool js_array_has_props(const Array* arr);
Map* js_array_props(const Array* arr);
int64_t container_tail_reserved(const Array* arr);
int64_t container_dense_capacity(const Array* arr);
void js_elements_set_props(Array* arr, Map* props);
void list_relocate_owned_tail(List* list, Item* old_items, int64_t old_capacity,
                              Item* new_items, int64_t new_capacity);
void owned_item_slot_store(Item* storage, int64_t item_count,
                           int64_t index, Item item);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
Item owned_item_slot_read(Item* storage, int64_t item_count,
                          int64_t index, bool immortal);
Item lambda_item_adopt_scalar_home(Item item, uint64_t* home);
// v3 (RV5): resolve a shape-2 call result whose lane 1 came back pending.
// Boxes the lane-2 payload into the CALLING frame's number extent.
Item lambda_item_resolve_pending(Item pending, uint64_t payload);
// v3 (RV12): same, reading lane 2 from `Context::mir_companion_slot`. This is
// how C callers of a boxed Lambda entry resolve; a resolved Item passes
// through untouched, so no caller-side test is required.
Item lambda_item_resolve_pending_slot(Item value);
int64_t lambda_restore_number_frame_top(uint64_t* top);
// A terminal native consumer owns this word only until it unboxes, discards, or
// copies the result into destination-owned storage; never return its Item.
#define LAMBDA_SCALAR_HOME(name) uint64_t name = 0
// Counts actual GC rehomes at ownerless Item boundaries, by wide scalar type.
// Small inline values and already-GC values do not contribute.
#ifdef __cplusplus
}
#endif
void array_drop_inplace(Array* arr, int64_t n);  // drop first n items in-place
void array_limit_inplace(Array* arr, int64_t n);  // limit to first n items in-place
void array_limit_last_inplace(Array* arr, int64_t n);  // limit to last n items in-place
Array* array_spreadable();  // constructs a spreadable empty array
void array_push(Array* arr, Item item);  // push item to array
void array_push_spread(Array* arr, Item item);      // push item, spreading if spreadable array
void array_push_spread_all(Array* arr, Item item);  // push item, spreading any array (for pipe exprs in array literals)
Item array_end(Array* arr);  // finalize and return array as Item
#ifdef __cplusplus
extern "C" {
#endif
uint64_t lambda_item_hash(Item key, uint64_t seed0, uint64_t seed1);
int lambda_item_compare(Item a, Item b);
Array* fn_group_by_keys(Item rows_item, Item keys_item, const char** aliases, int64_t alias_count);
Array* fn_group_by_keys_items(Item rows_item, Item keys_item, Item aliases_item);
Array* fn_join_seed_tuples(Item rows_item, Item name_item, Item idx_name_item, Item idx_vals_item);
Array* fn_hash_join_tuples(Item prior_tuples_item, Item prior_keys_item, Item rows_item,
    Item row_keys_item, Item name_item, int64_t optional, Item idx_name_item, Item idx_vals_item);
Array* fn_cross_join_tuples(Item prior_tuples_item, Item rows_item, Item name_item,
    Item idx_name_item, Item idx_vals_item);
#ifdef __cplusplus
}
#endif

// Mark an item as spreadable (for spread operator *expr)
Item item_spread(Item item);

typedef void* (*fn_ptr)();

// Core Lambda's native dynamic dispatcher expands individual boxed ABI
// operands.  This is a language limit, not storage capacity: longer values
// must travel in an array/map or a single rest argument.
enum { LAMBDA_MAX_FUNCTION_ARGS = 16 };

// Every Core Function value explicitly describes the entry stored in ptr.
// Keep this unversioned layout local to the current JIT process: persistent
// AOT compatibility is deliberately deferred rather than guessed.
typedef uint8_t FunctionEntryAbi;
enum {
    FN_ENTRY_ABI_UNKNOWN = 0,
    FN_ENTRY_ABI_LAMBDA_DIRECT_ONLY,
    FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION,
    FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE,
    FN_ENTRY_ABI_FOREIGN,
    FN_ENTRY_ABI_HOST_ADAPTER,
    // T0 (AI7): a cold Lambda function with no native entry. `ptr` is NULL and
    // `def` carries the AST definition site that `interp_call` evaluates.
    FN_ENTRY_ABI_LAMBDA_INTERPRETED,
};

// RVO13: public boxed entries publish their post-call companion contract so
// dynamic dispatch can skip slot resolution for proven shape-1 results.
enum {
    LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN = 0,
    LAMBDA_MIR_PUBLIC_RETURN_ITEM = 1,
    LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION = 2,
};
#define LAMBDA_MIR_PUBLIC_RETURN_SHAPE_SHIFT 7

// Function as first-class value
// Supports both direct function references and closures
struct Function {
    uint8_t type_id;
    uint8_t arity;               // number of parameters (0-255)
    uint8_t closure_field_count;  // number of Item fields in closure_env (0 if not a closure)
    FunctionEntryAbi entry_abi;  // FunctionEntryAbi; checked before ptr dispatch
    union {
        uint32_t flags;          // whole-word initialization/copy only
        struct {
            uint32_t returns_ret_item : 1;
            uint32_t has_kwargs : 1;
            uint32_t is_generator : 1;
            uint32_t is_coroutine : 1;
            uint32_t is_system_function_ref : 1;
            uint32_t requires_scalar_result_home : 1;
            uint32_t requires_runtime_context : 1;
            uint32_t mir_public_return_shape : 2;
            uint32_t reserved_flags : 23;
        };
    };
    void* fn_type;        // fn type definition (TypeFunc*)
    fn_ptr ptr;           // native function pointer
    void* closure_env;    // closure environment (NULL if no captures)
    const char* name;     // function name for stack traces (may be NULL)
    struct Context* runtime_context; // owner passed through generated calls
    // Trailing only: generated code pokes type_id at offset 0 and
    // closure_field_count at offset 2, so no field may shift.
    // AST definition site (AstFuncNode*) — the T0 body plus, with `module`,
    // the (module, node) identity D6.2.1/S5.5.1 already require. NULL for
    // natively-compiled and foreign entries.
    const void* def;
    struct Script* def_module;  // owner of def's const_list / type_list / slab
};

LAMBDA_STATIC_ASSERT(__builtin_offsetof(Function, type_id) == 0,
                     "Function TypeId must remain at byte zero");
LAMBDA_STATIC_ASSERT(__builtin_offsetof(Function, fn_type) == 8,
                     "Function metadata must preserve pointer alignment");

// Dynamic function invocation for first-class functions
Item fn_call(Function* fn, List* args);
Item fn_call0(Function* fn);
Item fn_call1(Function* fn, Item a);
Item fn_call2(Function* fn, Item a, Item b);
Item fn_call3(Function* fn, Item a, Item b, Item c);
// Hosted-language result-boundary forms. First-party Lambda/LambdaJS v3
// entries resolve their companion lane in Context; hosted callbacks may still
// supply an explicit payload owner across this C boundary.
Item fn_call_into(Function* fn, List* args, uint64_t* result_home);
Item fn_call0_into(Function* fn, uint64_t* result_home);
Item fn_call1_into(Function* fn, Item a, uint64_t* result_home);
Item fn_call2_into(Function* fn, Item a, Item b, uint64_t* result_home);
Item fn_call3_into(Function* fn, Item a, Item b, Item c, uint64_t* result_home);

// Forward declaration for Pool (full definition at line ~359)
typedef struct Pool Pool;

// Path: segmented symbol for file/URL paths
// A path is a linked chain of segments from leaf to root
// Example: file.etc.hosts -> Path("hosts") -> Path("etc") -> Path("file") -> ROOT
typedef struct Path Path;
typedef struct PathMeta PathMeta;

// Path/PathMeta full definitions, path enums, macros, and most Path API
// moved to lambda-path.h (not needed by JIT-compiled code).
// Target/Name structs and APIs moved to lambda.hpp.

// Path construction API (called by JIT-generated code)
Path* path_new(Pool* pool, int scheme);                           // Create new path with scheme
Path* path_extend(Pool* pool, Path* base, const char* segment);   // Extend path with segment
Path* path_concat(Pool* pool, Path* base, Path* suffix);          // Concatenate two paths
Path* path_wildcard(Pool* pool, Path* base);                      // Add * wildcard segment
Path* path_wildcard_recursive(Pool* pool, Path* base);            // Add ** wildcard segment

// System function: exists() - check if file/directory exists (called by JIT)
Bool fn_exists(Item path);

// Create function wrappers for first-class usage
Function* to_fn(fn_ptr ptr);
Function* to_fn_n(fn_ptr ptr, int arity);
Function* to_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_sys_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_closure(fn_ptr ptr, int arity, void* env);
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name);
#ifdef __cplusplus
extern "C" {
#endif
void lambda_function_mark_mir_context_abi(Function* fn);
void lambda_function_mark_lambda_boxed_function(Function* fn);
void lambda_function_mark_lambda_boxed_procedure(Function* fn);
void lambda_function_mark_mir_public_return_shape(Function* fn, uint32_t shape);
void lambda_function_set_type(Function* fn, void* fn_type);
#ifdef __cplusplus
}
#endif

// Memory allocation for closure environments
typedef struct Context Context;

#ifdef __cplusplus
extern "C" {
#endif
void* heap_calloc(size_t size, TypeId type_id);
// Closure environments use scalar side storage so captured wide numbers remain
// valid after their creating invocation's number frame is reclaimed.
void* heap_calloc_closure_env(size_t size);
void* heap_calloc_class(size_t size, TypeId type_id, int cls);  // allocate with pre-computed size class
typedef struct LambdaRegion LambdaRegion;
LambdaRegion* lambda_region_begin(void);
void lambda_region_end(LambdaRegion* region);
void* lambda_region_calloc(LambdaRegion* region, size_t size, TypeId type_id);
void* heap_data_calloc(size_t size);  // allocate GC-managed data buffer (for map/object data)
uint64_t* heap_gc_root_slot_new(uint64_t value);
bool heap_try_register_gc_root(uint64_t* slot);
void heap_unregister_gc_root(uint64_t* slot);
bool heap_try_register_gc_root_range(uint64_t* base, int count);
void heap_no_gc_scope_begin(void);
void heap_no_gc_scope_end(void);
void heap_gc_defer_collection_begin(void);
void heap_gc_defer_collection_end(void);
// String creation for name pooling
String* heap_create_name(const char* name);
// String creation for runtime strings
String* heap_strcpy(const char* src, int64_t len);
const uint8_t* binary_data(const Binary* binary);
uint32_t binary_length(const Binary* binary);
bool binary_is_ascii(const Binary* binary);
Binary* pool_binary_from_bytes(Pool* pool, const void* src, size_t len);
Binary* heap_binary_from_bytes(const char* src, int64_t len);
Binary* heap_binary_from_storage(ByteStorage* storage, size_t offset,
    size_t length, bool is_ascii);
Binary* heap_binary_slice(Binary* source, size_t offset, size_t length);
Binary* heap_binary_copy(Binary* source);
Binary* heap_binary_concat(Binary* left, Binary* right);
// Symbol creation for runtime symbols
Symbol* heap_create_symbol(const char* symbol, size_t len);
#ifdef __cplusplus
}
#endif

#define INT64_ERROR           INT64_MAX
#define LAMBDA_INT64_MAX    (INT64_MAX - 1)

// DateTime error sentinel — all bits set = clearly invalid
// month=15 (impossible: months are 1-12), every field at maximum
// Used by DateTime-returning functions to signal errors
#define DATETIME_ERROR_VALUE  0xFFFFFFFFFFFFFFFFULL

// Check if a DateTime value is the error sentinel
// Works in both C (uint64_t) and C++ (struct with int64_val)
#ifdef __cplusplus
#define DATETIME_IS_ERROR(dt) ((dt).int64_val == DATETIME_ERROR_VALUE)
#else
#define DATETIME_IS_ERROR(dt) ((dt) == DATETIME_ERROR_VALUE)
#endif

// Create a DateTime error value
#ifdef __cplusplus
#define DATETIME_MAKE_ERROR() (DateTime{.int64_val = DATETIME_ERROR_VALUE})
#else
#define DATETIME_MAKE_ERROR() ((DateTime)DATETIME_ERROR_VALUE)
#endif

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_NULL_SPREADABLE ((uint64_t)LMD_TYPE_NULL << 56 | 1)  // spreadable null (skip when spreading)
#define ITEM_JS_UNDEFINED   ((uint64_t)LMD_TYPE_UNDEFINED << 56)  // JavaScript undefined
#define ITEM_JS_TDZ         ((uint64_t)LMD_TYPE_UNDEFINED << 56 | 1)  // TDZ sentinel for let/const
#define ITEM_TASK_SUSPENDED ((uint64_t)LMD_TYPE_UNDEFINED << 56 | 2)  // internal resumable-call sentinel
// Internal call-ABI marker.  It never reaches a Lambda binding: public MIR
// wrappers replace it with an optional null or evaluate the declared default.
#define ITEM_MISSING_ARGUMENT ((uint64_t)LMD_TYPE_UNDEFINED << 56 | 3)
// Reserved non-type tag byte for internal Item sentinels that must not collide
// with any value. It sits above every TypeId, so `type_id()` can never produce
// it, and below 0x80, because the 0x80-0x9F octant is reserved for the rotated
// inline-int encoding (`Lambda_Type_Int_Boxing.md` §3) rather than for tags.
#define ITEM_SENTINEL_TAG   UINT64_C(0x1F)
#define ITEM_JS_DELETED_SENTINEL   ((ITEM_SENTINEL_TAG << 56) | UINT64_C(0x00DEAD00DEAD00))
#define ITEM_JS_ITER_DONE_SENTINEL ((ITEM_SENTINEL_TAG << 56) | UINT64_C(0x00DEAD00000000))

// ---------------------------------------------------------------------------
// Pending Items — return-value convention v3 (RV3, see
// `vibe/Lambda_Design_Compiling_Return_Value.md`, formal spec D5.2.1v3 /
// D8.4.2v3).
//
// A shape-2 return is the pair `[item, scalar]`: lane 1 carries an ordinary
// Item unless the returned value is a WIDE scalar (int64 / uint64 /
// out-of-band double), in which case lane 1 is a PENDING Item and lane 2
// carries the raw 64-bit payload. The caller resolves the pair at the first
// resolution point (consume, patch on escape, or tail-forward).
//
// Two protocol invariants (RV4) that every consumer may rely on:
//   1. A pending Item NEVER lives in memory. It exists only in registers
//      between a call returning and its first resolution point, so anything
//      loaded from a variable, container, field or constant is resolved by
//      construction.
//   2. At most one pending value is live at any point — lane 2 is a single
//      location clobbered by the next call.
//
// 0x1E is the last free byte of the `000` octant (TypeIds run through 0x1B,
// COUNT/HEAP_START take 0x1C-0x1D, ITEM_SENTINEL_TAG takes 0x1F). Spending it
// exhausts the octant; the next tag-byte consumer must take the reserved
// 0x80-0x9F headroom (`Lambda_Type_Double_Boxing.md` Part 8).
//
// The tag sits above every TypeId, so a leaked pending Item indexes outside
// the valid TypeId range and dies loudly at the first per-tag table bound —
// this is a guard property, not an accident.
//
// Return-value convention v3 is the only generated-function ABI. Shape-2
// payloads use a MIR pair or Context::mir_companion_slot; side-number-stack
// addresses remain only at explicit native/host ownership boundaries
// (D5.2.1v3, D5.2.2v3). The fixed revision rejects stale cached modules.
#define LAMBDA_RETURN_CONVENTION_REVISION 3

#define ITEM_PENDING_TAG    UINT64_C(0x1E)
#define ITEM_PENDING        (ITEM_PENDING_TAG << 56)
// Payload kind lives in the low bits of lane 1; lane 2 holds the raw bits.
// Kind 3 stays reserved: RV8 rules DTIME in-band (pointer Item, GC-managed),
// and every other pointer-backed scalar is likewise never pending.
#define PENDING_KIND_INT64   UINT64_C(0)
#define PENDING_KIND_UINT64  UINT64_C(1)
#define PENDING_KIND_FLOAT   UINT64_C(2)  // out-of-band double, raw bits on lane 2
#define PENDING_KIND_MASK    UINT64_C(3)
#define ITEM_PENDING_INT64   (ITEM_PENDING | PENDING_KIND_INT64)
#define ITEM_PENDING_UINT64  (ITEM_PENDING | PENDING_KIND_UINT64)
#define ITEM_PENDING_FLOAT   (ITEM_PENDING | PENDING_KIND_FLOAT)

// The discriminator the JIT mirrors in two instructions:
//   and t, item, ITEM_HIGH_BYTE_MASK; beq L_pending, t, ITEM_PENDING
static inline bool lambda_item_is_pending(uint64_t bits) {
    return (bits & ITEM_HIGH_BYTE_MASK) == ITEM_PENDING;
}

static inline uint64_t lambda_item_pending_kind(uint64_t bits) {
    return bits & PENDING_KIND_MASK;
}
// ---------------------------------------------------------------------------

#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_INT64          ((uint64_t)LMD_TYPE_INT64 << 56)
// BigInt reuses LMD_TYPE_DECIMAL; distinguished by Decimal.unlimited == DECIMAL_BIGINT
#define DECIMAL_BIGINT      2
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

// numeric type check: `number` is a type-language union, not a runtime TypeId.
static inline bool is_numeric_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64 ||
           type_id == LMD_TYPE_UINT64 || type_id == LMD_TYPE_FLOAT ||
           type_id == LMD_TYPE_DECIMAL || type_id == LMD_TYPE_NUM_SIZED ||
           type_id == LMD_TYPE_COMPLEX;
}

// Can a value of this DECLARED type ever be a wide scalar — one that needs a
// number home (v1) or the companion lane (v3)? Everything else is inline,
// pointer-backed or a container, and so needs no rehoming at any boundary.
// This is the single source of the "NONE" decision: the emitter's
// `em_scalar_return_mode_for_type()` defers to it, and the C-side sys-func
// metadata fallback in mir.c uses it directly, so the two cannot drift.
static inline bool lambda_type_id_may_be_wide_scalar(TypeId type_id) {
    return type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64 ||
           type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64 ||
           type_id == LMD_TYPE_ANY;
}

static inline bool is_native_numeric_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64 ||
           type_id == LMD_TYPE_FLOAT;
}

static inline bool is_integer_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64;
}

static inline bool is_float_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64;
}

static inline bool is_native_numeric_or_bool_type_id(TypeId type_id) {
    return is_native_numeric_type_id(type_id) || type_id == LMD_TYPE_BOOL;
}

static inline bool is_text_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL;
}

static inline bool is_array_family_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_NUM;
}

static inline bool is_map_family_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP ||
           type_id == LMD_TYPE_ELEMENT || type_id == LMD_TYPE_OBJECT;
}

static inline bool is_container_type_id(TypeId type_id) {
    return type_id >= LMD_TYPE_CONTAINER && type_id < LMD_TYPE_ANY;
}

static inline bool is_native_param_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_FLOAT ||
           type_id == LMD_TYPE_BOOL || type_id == LMD_TYPE_STRING ||
           type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64;
}

static inline bool is_typed_wrapper_param_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64 ||
           type_id == LMD_TYPE_UINT64 ||
           type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_BOOL ||
           type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_BINARY ||
           type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_DECIMAL ||
           type_id == LMD_TYPE_DTIME || type_id == LMD_TYPE_MAP ||
           type_id == LMD_TYPE_OBJECT || type_id == LMD_TYPE_ELEMENT;
}

static inline bool is_fn_call_wrapper_return_type_id(TypeId type_id) {
    return is_integer_type_id(type_id) ||
           type_id == LMD_TYPE_UINT64 ||
           type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_BOOL ||
           type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_BINARY ||
           type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_DECIMAL ||
           type_id == LMD_TYPE_DTIME;
}

#define IS_NUMERIC_ID(t) is_numeric_type_id((TypeId)(t))

#define ITEM_TRUE           (((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)1)
#define ITEM_FALSE          (((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)0)
#define ITEM_FLOAT_P0       ((uint64_t)LMD_TYPE_FLOAT << 56)
#define ITEM_FLOAT_N0       (ITEM_FLOAT_P0 | UINT64_C(1))

#ifndef __cplusplus
LAMBDA_STATIC_ASSERT(sizeof(Item) == sizeof(uint64_t), "C Item must remain one word");
#endif
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_NULL >> 56)), "ITEM_NULL tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_NULL_SPREADABLE >> 56)), "ITEM_NULL_SPREADABLE tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_UNDEFINED >> 56)), "ITEM_JS_UNDEFINED tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_TDZ >> 56)), "ITEM_JS_TDZ tag must be non-double");
// (the Jube lazy marker is an ordinary int value, so its encoding is whatever
//  the int encoder produces — nothing tag-specific left to assert here)
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_DELETED_SENTINEL >> 56)), "ITEM_JS_DELETED_SENTINEL tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_ITER_DONE_SENTINEL >> 56)), "ITEM_JS_ITER_DONE_SENTINEL tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_INT >> 56)), "ITEM_INT tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_ERROR >> 56)), "ITEM_ERROR tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_TRUE >> 56)), "ITEM_TRUE tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_FALSE >> 56)), "ITEM_FALSE tag must be non-double");
LAMBDA_STATIC_ASSERT((ITEM_FLOAT_P0 & ITEM_DBL_MASK) == 0, "packed +0 must be outside double space");
LAMBDA_STATIC_ASSERT((ITEM_FLOAT_N0 & ITEM_DBL_MASK) == 0, "packed -0 must be outside double space");
LAMBDA_STATIC_ASSERT((uint8_t)(ITEM_FLOAT_P0 >> 56) == LMD_TYPE_FLOAT, "packed +0 must carry float tag");
LAMBDA_STATIC_ASSERT((uint8_t)(ITEM_FLOAT_N0 >> 56) == LMD_TYPE_FLOAT, "packed -0 must carry float tag");
// The reserved sentinel tag must stay unreachable from `type_id()` (above every
// TypeId) and out of the inline-int octant, or a sentinel would decode as a value.
LAMBDA_STATIC_ASSERT(ITEM_SENTINEL_TAG >= LMD_CONTAINER_HEAP_START,
                     "sentinel tag must sit above every TypeId");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NOT_INLINE_INT((uint8_t)ITEM_SENTINEL_TAG),
                     "sentinel tag must stay out of the inline-int octant");
LAMBDA_STATIC_ASSERT(ITEM_JS_DELETED_SENTINEL != ITEM_JS_ITER_DONE_SENTINEL,
                     "internal sentinels must stay distinct");
// The pending tag (RV3) has the same unreachability requirements as the
// sentinel tag, plus it must stay distinct from the sentinel family: JS
// deleted/iter-done sentinels are legitimately STORED (sparse arrays), so a
// shared high byte would false-positive the 2-instruction pending test.
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)ITEM_PENDING_TAG),
                     "pending tag must be non-double");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NOT_INLINE_INT((uint8_t)ITEM_PENDING_TAG),
                     "pending tag must stay out of the inline-int octant");
LAMBDA_STATIC_ASSERT(ITEM_PENDING_TAG >= LMD_CONTAINER_HEAP_START,
                     "pending tag must sit above every TypeId");
LAMBDA_STATIC_ASSERT(ITEM_PENDING_TAG < LAMBDA_TAG_SPACE_SIZE,
                     "pending tag must stay inside the 000 octant tag space");
LAMBDA_STATIC_ASSERT(ITEM_PENDING_TAG != ITEM_SENTINEL_TAG,
                     "pending tag must stay distinct from the sentinel tag");
LAMBDA_STATIC_ASSERT((uint8_t)(ITEM_JS_DELETED_SENTINEL >> 56) != (uint8_t)ITEM_PENDING_TAG &&
                     (uint8_t)(ITEM_JS_ITER_DONE_SENTINEL >> 56) != (uint8_t)ITEM_PENDING_TAG,
                     "storable JS sentinels must not share the pending high byte");
LAMBDA_STATIC_ASSERT(PENDING_KIND_FLOAT <= PENDING_KIND_MASK,
                     "pending kinds must fit the reserved low bits");
// The four wide-scalar tags are CONTIGUOUS, and the JIT's pending-pair builder
// depends on it: `(unsigned)(tag - LMD_TYPE_INT64) <= 3` is the whole fast-path
// test, and `tag - LMD_TYPE_INT64` IS the pending kind for the two integer
// tags. Reordering EnumTypeId without preserving this run silently
// mis-classifies returns, so pin it here.
LAMBDA_STATIC_ASSERT(LMD_TYPE_UINT64 == LMD_TYPE_INT64 + 1 &&
                     LMD_TYPE_FLOAT == LMD_TYPE_INT64 + 2 &&
                     LMD_TYPE_FLOAT64 == LMD_TYPE_INT64 + 3,
                     "wide scalar tags must stay contiguous from LMD_TYPE_INT64");
LAMBDA_STATIC_ASSERT((uint64_t)(LMD_TYPE_INT64 - LMD_TYPE_INT64) == PENDING_KIND_INT64 &&
                     (uint64_t)(LMD_TYPE_UINT64 - LMD_TYPE_INT64) == PENDING_KIND_UINT64,
                     "integer pending kinds must equal their tag offset");
// Tags 0x06-0x09 have bits 6 and 5 clear, so an Item whose high byte lands in
// the wide-scalar run can never be an inline double. The pair builder relies on
// that to skip the ITEM_DBL_MASK test on the wide arm.
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)LMD_TYPE_INT64) &&
                     ITEM_TAG_IS_NON_DOUBLE((uint8_t)LMD_TYPE_FLOAT64),
                     "wide scalar tags must be outside inline-double space");

static inline void lambda_item_debug_trap(void) {
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    *((volatile int*)0) = 0;
#endif
}

// Tripwire for RV4.1: a pending Item must never reach a value accessor. It is
// a return-lane transport, resolved by the caller before any consume/store, so
// seeing one here means an emitter site skipped its resolution point.
static inline void assert_item_not_pending(uint64_t bits) {
#if !defined(NDEBUG)
    if (lambda_item_is_pending(bits)) {
        lambda_item_debug_trap();
    }
#else
    (void)bits;
#endif
}

static inline void assert_raw_item_pointer(const void* ptr) {
#if !defined(NDEBUG)
    if (!ptr) return;
    uint64_t word = (uint64_t)(uintptr_t)ptr;
    // Raw-pointer Items must be bit-identical native pointers, never tagged.
    if ((word & ITEM_HIGH_BYTE_MASK) != 0 || (word & ITEM_DBL_MASK) != 0 ||
        (const void*)(uintptr_t)word != ptr) {
        lambda_item_debug_trap();
    }
#else
    (void)ptr;
#endif
}

// v5 (`vibe/Lambda_Semantics_Int_Type.md` §5.4): `int` is the contiguous band
// +/-(2^53 - 1), and a FINITE int boxes as a 56-bit two's-complement payload
// under the LMD_TYPE_INT tag byte:
//
//     box(n)   = (LMD_TYPE_INT << 56) | (n & MASK56)     // AND + OR
//     unbox(i) = ((int64_t)(i << 8)) >> 8                // sign-extend low 56
//
// One instruction each way on aarch64 (bfi / sbfx), no FP unit on the int path.
// The payload has 56 bits and the band needs 54, so every int value round-trips
// exactly and the encoding is canonical.
//
// POISON IS NOT PACKED. `inf`/`-inf`/`nan` are the values `int` SHARES with
// `float` (formal semantics 4, kept by v5), stored as ordinary inline IEEE bits
// in the float octants. So a tag-byte-INT Item ALWAYS decodes to a finite band
// value -- the payload invariant every consumer may rely on.
//
// History: v4 (C16) instead carried the value's own IEEE bits rotated left by
// one, which put int in the `100` octant and made int/float share a native
// lane. That is retired with the double lane; see the design doc §1 for the
// v1-v5 arc and `Lambda_Type_Int_Boxing.md` for the rotation scheme itself.
#define ITEM_INT_PAYLOAD_MASK  UINT64_C(0x00FFFFFFFFFFFFFF)  // low 56 bits

// The band. Under v5 this is the WHOLE int domain -- not just the literal
// ingestion rule -- so it is simultaneously the carrier capacity, the
// saturation point, and the `int <= float` subtyping edge (§5.1: every band
// value converts to double exactly, which is why int53 beats int56).
#define INT53_MAX  ((int64_t)9007199254740991LL)   // +(2^53 - 1)
#define INT53_MIN  ((int64_t)-9007199254740991LL)  // -(2^53 - 1)

// Raw IEEE bit patterns for the three shared poison values.
#define LAMBDA_IEEE_INF_BITS      UINT64_C(0x7FF0000000000000)
#define LAMBDA_IEEE_NEG_INF_BITS  UINT64_C(0xFFF0000000000000)
#define LAMBDA_IEEE_NAN_BITS      UINT64_C(0x7FF8000000000000)

// LANE SENTINELS -- PRIVATE to the native i64 lane (§5.1). IEEE bits cannot
// live in an i64, so within the lane only, poison is three reserved i64 values.
// They are converted at box/unbox and NEVER escape the lane: no Item, no
// container, no guest bridge ever sees one.
//
// Placed at the two's-complement extremes for two reasons. (1) Degradation:
// they sit >= 2^63 - 2^54 from the band, so a sentinel that leaks past a
// forgotten check lands far out of band and re-poisons at the next band check
// instead of laundering into a finite value. (2) `neg`/`abs` become BRANCH-FREE
// total -- -(INT64_MIN) wraps to itself so nan stays nan, -(INT64_MAX) is
// INT64_MIN+1 so +inf becomes -inf, and the band is symmetric so every finite
// case closes. The classic two's-complement negation trap becomes the mechanism
// that propagates poison correctly.
#define INT_LANE_NAN      INT64_MIN
#define INT_LANE_NEG_INF  (INT64_MIN + 1)
#define INT_LANE_INF      INT64_MAX
// This fourth sentinel is valid only in an int? lane. It shares ItemNull's
// word so box/unbox is exact, but ArrayNum must demote before it is stored.
#define INT_LANE_NULL     ((int64_t)ITEM_NULL)
// Widened i8?…u32? lanes use the same out-of-domain bit pattern. Keep a
// separate name so sized-lane checks cannot accidentally be routed through
// int poison arithmetic.
#define SIZED_LANE_NULL   INT_LANE_NULL

// ItemNull is not an IEEE double bit-pattern, so float? reserves one quiet
// NaN payload in its native lane. Ordinary Lambda NaN uses the canonical
// zero-payload quiet NaN above; normalize this one payload on stores so a
// guest numeric NaN can never be mistaken for nullable absence.
#define FLOAT_LANE_NULL_BITS UINT64_C(0x7FF8000000000001)

static inline bool lambda_float_lane_is_null(uint64_t bits) {
    return bits == FLOAT_LANE_NULL_BITS;
}

static inline uint64_t lambda_float_lane_from_double(double value) {
    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits == FLOAT_LANE_NULL_BITS ? LAMBDA_IEEE_NAN_BITS : bits;
}

static inline double lambda_float_lane_to_double(uint64_t bits) {
    double value;
    __builtin_memcpy(&value, &bits, sizeof(value));
    return value;
}

// An IEEE special: exponent all ones, so inf (mantissa 0) or nan.
#define LAMBDA_ITEM_IS_IEEE_SPECIAL(b) \
    (((b) & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000))

// The merged poison values (inf, -inf, nan) are the ones `int` and `float`
// share. They are PHYSICALLY doubles, so get_type_id() must keep reporting
// FLOAT -- it is the decoder dispatch, and every site that reads an Item
// relies on it to pick the right lane. The C16 ruling that `type(nan) == int`
// is a SURFACE claim, applied in fn_type()/item_static_type_for_is() only.
// Conflating the two routes nan into integer decoders (LambdaJS alone has 423
// such sites, e.g. js_is_symbol()'s `it2i(v) <= -JS_SYMBOL_BASE`).
static inline bool lambda_item_is_merged_poison(uint64_t bits) {
    return (bits & ITEM_DBL_MASK) && LAMBDA_ITEM_IS_IEEE_SPECIAL(bits);
}

// Is this Item word a packed (finite) int? The tag byte alone decides -- but
// the double test must come first, because an inline double can carry any high
// byte. Callers inside type_id() have already excluded doubles.
#define LAMBDA_ITEM_IS_PACKED_INT(item_bits) \
    ((((item_bits) & ITEM_HIGH_BYTE_MASK) >> 56) == (uint64_t)LMD_TYPE_INT)

// Box a LANE value as an int Item. TOTAL: the three sentinels become their
// shared IEEE values, and an out-of-band finite value SATURATES by sign rather
// than truncating (formal semantics 4.9 -- an int result that cannot be carried
// as an int is +/-inf, never a wrapped or error value). So `i2it` never fails,
// which is the O1-class safety property v4 established and v5 keeps.
static inline uint64_t lambda_int_box_lane(int64_t lane) {
    if (lane == INT_LANE_NULL) return ITEM_NULL;
    if (lane >= INT53_MIN && lane <= INT53_MAX) {
        return ITEM_INT | ((uint64_t)lane & ITEM_INT_PAYLOAD_MASK);
    }
    if (lane == INT_LANE_NAN) return LAMBDA_IEEE_NAN_BITS;
    // covers both +/-inf sentinels AND ordinary out-of-band saturation
    return lane > 0 ? LAMBDA_IEEE_INF_BITS : LAMBDA_IEEE_NEG_INF_BITS;
}

// Box a double known to be integral (or poison) as an int Item. The caller's
// contract is integrality; magnitude is handled here by saturation.
static inline uint64_t lambda_int_box_double(double value) {
    if (value != value) return LAMBDA_IEEE_NAN_BITS;
    if (value > (double)INT53_MAX) return LAMBDA_IEEE_INF_BITS;
    if (value < (double)INT53_MIN) return LAMBDA_IEEE_NEG_INF_BITS;
    return ITEM_INT | ((uint64_t)(int64_t)value & ITEM_INT_PAYLOAD_MASK);
}

// The LANE value of an int Item: finite payloads sign-extend, poison maps to
// its lane sentinel. This is the ONLY place an Item becomes a lane value.
static inline int64_t lambda_int_item_to_lane(uint64_t item_bits) {
    if (item_bits == ITEM_NULL) return INT_LANE_NULL;
    if (item_bits & ITEM_DBL_MASK) {  // shared poison, inline IEEE
        if ((item_bits & UINT64_C(0x000FFFFFFFFFFFFF)) != 0) return INT_LANE_NAN;
        return (item_bits >> 63) ? INT_LANE_NEG_INF : INT_LANE_INF;
    }
    return ((int64_t)(item_bits << 8)) >> 8;  // sign-extend the 56-bit payload
}

// The value of an int Item as a double. Poison comes back as the IEEE special
// it denotes, so numeric consumers need no poison branch.
// The value of an int Item as a double. Poison comes back as the IEEE special
// it denotes, so numeric consumers need no poison branch. Exact for every
// finite value: the band is chosen precisely so that int -> double never
// rounds (§5.1 -- this is what `int <= float` rests on).
static inline double lambda_int_unbox_double(uint64_t item_bits) {
    double result;
    if (item_bits & ITEM_DBL_MASK) {  // shared inf/nan, stored as raw IEEE bits
        __builtin_memcpy(&result, &item_bits, sizeof(result));
        return result;
    }
    return (double)(((int64_t)(item_bits << 8)) >> 8);
}

// Lane <-> double conversions. Header-only because `core/` uses them and must
// not link `runtime/`; the runtime keeps thin `_c` wrappers so the JIT registry
// has a callable symbol.
static inline double lambda_int_lane_to_double(int64_t lane) {
    return lambda_int_unbox_double(lambda_int_box_lane(lane));
}

static inline int64_t lambda_double_to_int_lane(double value) {
    if (value != value) return INT_LANE_NAN;
    if (value > (double)INT53_MAX) return INT_LANE_INF;
    if (value < (double)INT53_MIN) return INT_LANE_NEG_INF;
    return (int64_t)value;
}


// (The retired int-poison classifiers lived here. With one shared inf/nan
// representation, "is this poison?" is lambda_item_is_merged_poison() and
// nan-ness survives every unbox, so no sentinel-aware predicate is needed.)

// Classify an int *value* held as a double. Kept as the value-side spelling of
// the same question so numeric code can ask before it re-boxes.
#define LAMBDA_INT_VALUE_IS_POISON(v)  (!((v) == (v)) || (v) > 1.7976931348623157e308 || (v) < -1.7976931348623157e308)
#define LAMBDA_INT_VALUE_IS_NAN(v)     (!((v) == (v)))
#define LAMBDA_INT_VALUE_IS_INF(v)     ((v) > 1.7976931348623157e308)
#define LAMBDA_INT_VALUE_IS_NEG_INF(v) ((v) < -1.7976931348623157e308)

// Unresolved lazy global. Unlike the sentinels above it is stored in ordinary
// property storage, which round-trips a scalar through its *value*, so this
// marker has to be a real int value ("JUBELZ") rather than a magic payload on
// some tag — a payload would be re-encoded to a different word on the way back.
#define ITEM_JS_LAZY_GLOBAL_MAGIC     ((int64_t)0x004A5542454C5A)
#define ITEM_JS_LAZY_GLOBAL_SENTINEL  i2it(ITEM_JS_LAZY_GLOBAL_MAGIC)

static inline uint64_t lambda_int64_ptr_to_item_bits(const int64_t* ptr) {
    if (!ptr) return ITEM_NULL;
    uint64_t payload = (uint64_t)(uintptr_t)ptr;
    if (payload & ITEM_HIGH_BYTE_MASK) return ITEM_ERROR;
    return ITEM_INT64 | payload;
}

static inline uint64_t lambda_uint64_ptr_to_item_bits(const uint64_t* ptr) {
    if (!ptr) return ITEM_NULL;
    uint64_t payload = (uint64_t)(uintptr_t)ptr;
    if (payload & ITEM_HIGH_BYTE_MASK) return ITEM_ERROR;
    return ((uint64_t)LMD_TYPE_UINT64 << 56) | payload;
}

inline uint64_t b2it(uint8_t bool_val) {
    return bool_val >= BOOL_ERROR ? ITEM_ERROR : ((((uint64_t)LMD_TYPE_BOOL)<<56) | bool_val);
}
// Box an int64 as an `int` Item. v5 closes the finite domain at int53, so
// out-of-band input saturates to the shared IEEE infinity instead of wrapping,
// silently rounding, or producing an error Item.
#define i2it(int_val)        lambda_int_box_lane((int64_t)(int_val))
// BigInt: same as decimal tagged pointer (Decimal.unlimited == DECIMAL_BIGINT)
#define bi2it(decimal_ptr)   c2it(decimal_ptr)
#define l2it(long_ptr)       lambda_int64_ptr_to_item_bits((const int64_t*)(long_ptr))
#define d2it(double_ptr)     ((double_ptr)? ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr)): ITEM_NULL)
// f64 is a type-language alias for binary64; runtime Items use canonical float encoding.
#define f642it(double_ptr)   lambda_float_ptr_to_item(double_ptr)

#ifdef __cplusplus
static inline Item lambda_float_ptr_to_item(const double* double_ptr);
#else
static inline Item lambda_float_ptr_to_item(const double* double_ptr) {
    if (!double_ptr) return ITEM_NULL;
    double value = *double_ptr;
    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    if (value == 0.0) return ITEM_FLOAT_P0 | ((bits >> 63) ? UINT64_C(1) : UINT64_C(0));
    if (bits & ITEM_DBL_MASK) return bits;
    return d2it(double_ptr);
}
#endif
#define c2it(decimal_ptr)    ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr)): ITEM_NULL)
#define s2it(str_ptr)        ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): ITEM_NULL)
#define y2it(sym_ptr)        ((sym_ptr)? ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr)): ITEM_NULL)
#define x2it(bin_ptr)        ((bin_ptr)? ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr)): ITEM_NULL)
#define k2it(dtime_ptr)      ((dtime_ptr)? ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr)): ITEM_NULL)
#define u2it(uint64_ptr)     lambda_uint64_ptr_to_item_bits((const uint64_t*)(uint64_ptr))

// Float16/Float32 packing into NUM_SIZED Items
// float32: store IEEE 754 binary32 bit pattern in low 32 bits
// Use compiler builtins for type-safe bit conversion.
static inline uint32_t f32_to_bits(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float bits_to_f32(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
#define f32_to_item(v) NUM_SIZED_PACK(NUM_FLOAT32, f32_to_bits((float)(v)))

// float16: software conversion (IEEE 754 binary16)
static inline uint16_t f32_to_f16_bits(float f) {
    uint32_t b = f32_to_bits(f);
    uint32_t sign = (b >> 16) & 0x8000;
    int32_t  expo = ((b >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (b >> 13) & 0x03FF;
    if (expo <= 0) return (uint16_t)sign;         // underflow → ±0
    if (expo >= 31) return (uint16_t)(sign | 0x7C00); // overflow → ±inf
    return (uint16_t)(sign | (expo << 10) | mant);
}
static inline float f16_bits_to_f32(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000) << 16;
    uint32_t expo = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;
    if (expo == 0) { if (mant == 0) return bits_to_f32(sign); /* denorm → 0 */ }
    if (expo == 31) return bits_to_f32(sign | 0x7F800000 | (mant << 13)); // inf/nan
    uint32_t result = sign | ((expo - 15 + 127) << 23) | (mant << 13);
    return bits_to_f32(result);
}
#define f16_to_item(v) NUM_SIZED_PACK(NUM_FLOAT16, (uint32_t)f32_to_f16_bits((float)(v)))

// Unpack sized numeric value from Item
static inline int8_t   item_to_i8(uint64_t it)  { return (int8_t)(NUM_SIZED_RAW32(it) & 0xFF); }
static inline int16_t  item_to_i16(uint64_t it) { return (int16_t)(NUM_SIZED_RAW32(it) & 0xFFFF); }
static inline int32_t  item_to_i32(uint64_t it) { return (int32_t)NUM_SIZED_RAW32(it); }
static inline uint8_t  item_to_u8(uint64_t it)  { return (uint8_t)(NUM_SIZED_RAW32(it) & 0xFF); }
static inline uint16_t item_to_u16(uint64_t it) { return (uint16_t)(NUM_SIZED_RAW32(it) & 0xFFFF); }
static inline uint32_t item_to_u32(uint64_t it) { return NUM_SIZED_RAW32(it); }
static inline float    item_to_f32(uint64_t it) { return bits_to_f32(NUM_SIZED_RAW32(it)); }
static inline float    item_to_f16(uint64_t it) { return f16_bits_to_f32((uint16_t)(NUM_SIZED_RAW32(it) & 0xFFFF)); }

// Nullable sized-integer lanes widen to i64 so no source-domain bit pattern
// is reserved. Floating-sized values have their own IEEE representation and
// are intentionally not admitted by this integer-lane helper.
static inline bool lambda_num_sized_is_integer(NumSizedType num_type) {
    return num_type >= NUM_INT8 && num_type <= NUM_UINT32;
}

static inline int64_t lambda_num_sized_item_to_lane(uint64_t item) {
    switch ((NumSizedType)NUM_SIZED_SUBTYPE(item)) {
    case NUM_INT8: return (int64_t)item_to_i8(item);
    case NUM_INT16: return (int64_t)item_to_i16(item);
    case NUM_INT32: return (int64_t)item_to_i32(item);
    case NUM_UINT8: return (int64_t)item_to_u8(item);
    case NUM_UINT16: return (int64_t)item_to_u16(item);
    case NUM_UINT32: return (int64_t)item_to_u32(item);
    default: return 0;
    }
}

// ============================================================================
// Forward declaration for structured error handling
// ============================================================================
typedef struct LambdaError LambdaError;

// ============================================================================
// Container unboxing helpers: Item → native container pointer
// These validate the type tag and extract the pointer; return NULL on mismatch.
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
// ============================================================================

#ifndef __cplusplus
// Container unboxing: Item → native pointer (simple cast).
// Container Items store direct pointers (no type tag in the high bits),
// so no masking is needed — just cast to the target pointer type.
#define it2map(item)    ((Map*)(uintptr_t)(item))
#define it2list(item)   ((List*)(uintptr_t)(item))
#define it2elmt(item)   ((Element*)(uintptr_t)(item))
#define it2obj(item)    ((Object*)(uintptr_t)(item))
#define it2arr(item)    ((Array*)(uintptr_t)(item))
#define it2range(item)  ((Range*)(uintptr_t)(item))
#define it2path(item)   ((Path*)(uintptr_t)(item))
#define it2p(item)      ((void*)(uintptr_t)(item))

// Container boxing helper: native pointer → Item
static inline Item p2it(void* ptr) {
    if (!ptr) return ITEM_NULL;
    assert_raw_item_pointer(ptr);
    return (Item)(uint64_t)(uintptr_t)ptr;
}

// Convert LambdaError* → Error Item (LMD_TYPE_ERROR-tagged pointer)
static inline Item err2it(LambdaError* err) {
    if (!err) return ITEM_NULL;
    return (Item)(((uint64_t)LMD_TYPE_ERROR << 56) | (uint64_t)(uintptr_t)err);
}

// Convert Error Item → LambdaError* (extract pointer from tagged Item)
static inline LambdaError* it2err(Item item) {
    uint8_t tag = (uint64_t)item >> 56;
    if (tag != LMD_TYPE_ERROR) return null;
    return (LambdaError*)(uintptr_t)((uint64_t)item & 0x00FFFFFFFFFFFFFFULL);
}
#endif // !__cplusplus

// ============================================================================
// Per-type Ret* result structs (2-field: value + err)
// Used by can_raise functions to return typed native values with error info.
// Size: 16 bytes — fits in rax+rdx on x86-64, x0+x1 on ARM64.
// err == NULL means success; err != NULL means error (check err->code).
// ============================================================================

typedef struct RetBool   { bool         value; LambdaError* err; } RetBool;
typedef struct RetInt56  { int64_t      value; LambdaError* err; } RetInt56;   // Lambda int (name predates C16)
typedef struct RetInt64  { int64_t      value; LambdaError* err; } RetInt64;   // int64 (heap-allocated)
typedef struct RetFloat  { double       value; LambdaError* err; } RetFloat;
typedef struct RetString { String*      value; LambdaError* err; } RetString;
typedef struct RetSymbol { Symbol*      value; LambdaError* err; } RetSymbol;
typedef struct RetMap    { Map*         value; LambdaError* err; } RetMap;
typedef struct RetList   { List*        value; LambdaError* err; } RetList;
typedef struct RetElmt   { Element*     value; LambdaError* err; } RetElmt;
typedef struct RetObj    { Object*      value; LambdaError* err; } RetObj;
typedef struct RetArray  { Array*       value; LambdaError* err; } RetArray;
typedef struct RetRange  { Range*       value; LambdaError* err; } RetRange;
typedef struct RetPath   { Path*        value; LambdaError* err; } RetPath;
#ifndef __cplusplus
typedef struct RetItem   { Item         value; LambdaError* err; } RetItem;
#endif
#ifdef __cplusplus
struct RetItem;  // full definition in lambda.hpp
#endif

// ============================================================================
// Ret* constructor helpers
// Naming: r + type_abbreviation + _ok / _err
// ============================================================================

// RetItem (boxed, universal — used by _b trampolines)
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
#ifndef __cplusplus
static inline RetItem ri_ok(Item value) {
    RetItem r; r.value = value; r.err = null; return r;
}
static inline RetItem ri_err(LambdaError* error) {
    RetItem r; r.value = ITEM_ERROR; r.err = error; return r;
}
#endif

// RetBool
static inline RetBool rb_ok(bool value) {
    RetBool r; r.value = value; r.err = null; return r;
}
static inline RetBool rb_err(LambdaError* error) {
    RetBool r; r.value = false; r.err = error; return r;
}

// RetInt56 (Lambda int)
static inline RetInt56 ri56_ok(int64_t value) {
    RetInt56 r; r.value = value; r.err = null; return r;
}
static inline RetInt56 ri56_err(LambdaError* error) {
    RetInt56 r; r.value = 0; r.err = error; return r;
}

// RetInt64
static inline RetInt64 ri64_ok(int64_t value) {
    RetInt64 r; r.value = value; r.err = null; return r;
}
static inline RetInt64 ri64_err(LambdaError* error) {
    RetInt64 r; r.value = 0; r.err = error; return r;
}

// RetFloat
static inline RetFloat rf_ok(double value) {
    RetFloat r; r.value = value; r.err = null; return r;
}
static inline RetFloat rf_err(LambdaError* error) {
    RetFloat r; r.value = 0.0; r.err = error; return r;
}

// RetString
static inline RetString rs_ok(String* value) {
    RetString r; r.value = value; r.err = null; return r;
}
static inline RetString rs_err(LambdaError* error) {
    RetString r; r.value = null; r.err = error; return r;
}

// RetSymbol
static inline RetSymbol rsy_ok(Symbol* value) {
    RetSymbol r; r.value = value; r.err = null; return r;
}
static inline RetSymbol rsy_err(LambdaError* error) {
    RetSymbol r; r.value = null; r.err = error; return r;
}

// RetMap
static inline RetMap rm_ok(Map* value) {
    RetMap r; r.value = value; r.err = null; return r;
}
static inline RetMap rm_err(LambdaError* error) {
    RetMap r; r.value = null; r.err = error; return r;
}

// RetList
static inline RetList rl_ok(List* value) {
    RetList r; r.value = value; r.err = null; return r;
}
static inline RetList rl_err(LambdaError* error) {
    RetList r; r.value = null; r.err = error; return r;
}

// RetElmt
static inline RetElmt re_ok(Element* value) {
    RetElmt r; r.value = value; r.err = null; return r;
}
static inline RetElmt re_err(LambdaError* error) {
    RetElmt r; r.value = null; r.err = error; return r;
}

// RetObj
static inline RetObj ro_ok(Object* value) {
    RetObj r; r.value = value; r.err = null; return r;
}
static inline RetObj ro_err(LambdaError* error) {
    RetObj r; r.value = null; r.err = error; return r;
}

// RetArray
static inline RetArray ra_ok(Array* value) {
    RetArray r; r.value = value; r.err = null; return r;
}
static inline RetArray ra_err(LambdaError* error) {
    RetArray r; r.value = null; r.err = error; return r;
}

// RetRange
static inline RetRange rr_ok(Range* value) {
    RetRange r; r.value = value; r.err = null; return r;
}
static inline RetRange rr_err(LambdaError* error) {
    RetRange r; r.value = null; r.err = error; return r;
}

// RetPath
static inline RetPath rp_ok(Path* value) {
    RetPath r; r.value = value; r.err = null; return r;
}
static inline RetPath rp_err(LambdaError* error) {
    RetPath r; r.value = null; r.err = error; return r;
}

// ============================================================================
// Compatibility shims for incremental migration
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
// ============================================================================
#ifndef __cplusplus

// C helpers use the same merged-lane tag test as the C++ runtime.  Keep the
// implementation here so native host boundaries do not need the C++ Item API.
static inline bool item_is_error(Item item) {
    return ((uint64_t)item >> 56) == LMD_TYPE_ERROR;
}

// Wrap a legacy Item-returning function result into RetItem.
// Error Items may be either the historical sentinel (pointer=0) or a tagged
// LambdaError* created at runtime. Preserve the pointer when present and use
// .err as a boolean sentinel only for pointer-less errors.
static inline RetItem item_to_ri(Item item) {
    RetItem r;
    r.value = item;
    if ((uint64_t)item >> 56 == LMD_TYPE_ERROR) {
        LambdaError* err = it2err(item);
        r.err = err ? err : (LambdaError*)1;
    } else {
        r.err = null;
    }
    return r;
}

// Extract Item from RetItem (for legacy callers expecting plain Item)
// .value always holds the actual Item — whether error or normal value.
static inline Item ri_to_item(RetItem ri) {
    return ri.value;
}

#endif // !__cplusplus

Array* array_fill(Array* arr, int count, ...);
ArrayNum* array_int_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_int64_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_float_fill(ArrayNum* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);
// Same fill from a caller-rooted Item span; the T0 walker has no varargs.
Map* map_fill_items(Map* map, const Item* values, int value_count);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);
// Same fill from a caller-rooted Item span; the T0 walker has no varargs.
Element* elmt_fill_items(Element *elmt, const Item* values, int value_count);

typedef struct Url Url;
typedef struct Pool Pool;
typedef struct Arena Arena;

// Forward declaration of ArrayList (defined in lib/arraylist.h)
typedef struct _ArrayList ArrayList;

struct Context {
    Pool* pool;
    Arena* arena;  // arena allocator (for input parsing path; also result arena in ui_mode)
    void** consts;
    void* type_list;  // type definitions list (ArrayList* at runtime, void* for JIT access)
    Url* cwd;  // current working directory
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main; // whether to run main procedure on start
    bool disable_string_merging; // disable automatic string merging in list_push
    uintptr_t stack_limit; // stack overflow check limit (from lambda_stack_init)
    bool ui_mode; // allocate fat DomElement/DomText on arena for unified DOM tree
    uint64_t* side_root_base;
    uint64_t* side_root_top;
    uint64_t* side_root_commit_limit;
    uint64_t* side_root_limit;
    uint64_t* side_number_base;
    uint64_t* side_number_top;
    uint64_t* side_number_commit_limit;
    uint64_t* side_number_limit;
    uint64_t mir_return_lane;
    // Scratch cell for JIT double<->bits reinterpretation. MIR has no reg<->reg
    // bitcast, so the pair is a typed store + differently-typed load; routing it
    // through the Context the frame already holds costs no per-function stack
    // setup. Never GC-scanned — raw double bits in a scanned slot would be read
    // as a tagged Item. Single-thread-owned by the same invariant as the
    // side-stack pointers above, and dead between the store and its load.
    uint64_t mir_bitcast_scratch;
    // Return-value convention v3, RV12: lane 2 as a *location* rather than a
    // register. A C prototype has no portable spelling for MIR's two-result
    // convention, so every entry reachable from C — the public `_b` wrappers
    // called through the boxed-call trampolines and `fn->invoke` — hands its
    // wide payload back here instead, with lane 1 carrying the pending Item.
    // The protocol is otherwise byte-identical to the register form: single
    // live value, dead at the next call, resolved at the caller's first
    // resolution point.
    //
    // Same contract as the two cells above: never GC-scanned (raw payload
    // bits, never a pointer — RV8), single-thread-owned, and dead outside the
    // window between a shape-2 return and its resolution.
    uint64_t mir_companion_slot;
};

// A property key specification is compiler-neutral data stored in the sealed
// MIR module image. `name_offset` is relative to the beginning of the
// specification array and is valid only for an ordinary STRING key.
typedef struct PropertyKeySpec {
    uint32_t predefined_id;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t reserved;
} PropertyKeySpec;

// Immutable BSS metadata for one sealed MIR module. It describes code only;
// mutable bindings are allocated per EvalContext.
typedef struct LambdaModuleLayout {
    uint32_t module_id;
    uint32_t var_count;
    uint32_t property_key_count;
    uint32_t property_key_bytes_size;
    uint32_t reserved;
    const PropertyKeySpec* property_key_specs;
} LambdaModuleLayout;

typedef struct LambdaModuleVarRef {
    uint32_t module_id;
    uint32_t slot;
} LambdaModuleVarRef;


#ifndef LAMBDA_STATIC
#ifdef __cplusplus
extern "C" {
#endif
    Array* array();
    ArrayNum* array_int();
    ArrayNum* array_int64();
    ArrayNum* array_float();

    ArrayNum* array_num_new(ArrayNumElemType elem_type, int64_t length);
    ArrayNum* array_num_new_with_extra(ArrayNumElemType elem_type, int64_t length,
                                       int64_t extra);
    ArrayNum* array_num_new_external_view(Container* base, void* data_base,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    ArrayNum* array_num_new_buffer_view(Container* base, ByteBufferHandle* handle,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    ArrayNum* array_num_new_storage_view(ByteStorage* storage,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    bool array_num_init_derived_view(ArrayNum* view, ArrayNumShape* shape,
        ArrayNum* source, int64_t relative_elem_offset);
    void* array_num_resolve_data(ArrayNum* array, bool write);
    ArrayNum* array_int_new(int64_t length);
    ArrayNum* array_int64_new(int64_t length);
    ArrayNum* array_float_new(int64_t length);

    void array_float_set(ArrayNum *arr, int64_t index, double value);
    void array_int_set(ArrayNum *arr, int64_t index, int64_t lane);
    void array_num_set_item(ArrayNum *arr, int64_t index, Item value);
    Item array_num_read_item(ArrayNum *arr, int64_t index);
    double array_num_read_double(ArrayNum *arr, int64_t index);
    double array_num_get_number_value(ArrayNum *arr, int64_t index);
    void array_num_set_int64_value(ArrayNum *arr, int64_t index, int64_t value);
    void array_num_set_double_value(ArrayNum *arr, int64_t index, double value);
    bool array_num_copy_same_type_bytes(ArrayNum *dst, int64_t dst_index,
        ArrayNum *src, int64_t src_index, int64_t count);
    bool array_num_copy_equal_size_bytes(ArrayNum *dst, int64_t dst_index,
        ArrayNum *src, int64_t src_index, int64_t count);
    bool array_num_reverse_bytes(ArrayNum *arr);
    bool array_num_copy_reversed_bytes(ArrayNum *dst, ArrayNum *src);

    Map* map(int64_t type_index);
    Map* map_with_data(int64_t type_index);
    Map* map_with_tl(int64_t type_index, void* type_list_ptr);
    Map* map_with_region_tl(LambdaRegion* region, int64_t type_index,
        void* type_list_ptr);
    Map* map_with_type_tl(struct TypeMap* map_type, void* type_list_ptr);
    Map* map_with_region_type_tl(LambdaRegion* region, struct TypeMap* map_type,
        void* type_list_ptr);
    Element* elmt(int64_t type_index);
    Element* elmt_with_tl(int64_t type_index, void* type_list_ptr);
    Object* object(int64_t type_index);
    Object* object_with_data(int64_t type_index);
    Object* object_with_tl(int64_t type_index, void* type_list_ptr);
    Object* object_fill(Object* obj, ...);

    // these getters use the runtime number side stack
    Item array_get(Array *array, int64_t index);
    Item array_num_get(ArrayNum *array, int64_t index);
    Item array_int_get(ArrayNum *array, int64_t index);
    Item array_int64_get(ArrayNum* array, int64_t index);
    Item array_float_get(ArrayNum* array, int64_t index);
    // fast-path getters: return native types, skip boxing
    int64_t array_int_get_raw(ArrayNum *array, int64_t index);
    int64_t array_int64_get_raw(ArrayNum *array, int64_t index);
    double array_float_get_value(ArrayNum *arr, int64_t index);
    Item list_get(List *list, int64_t index);
    Item fn_string_ascii_at(Item str, int64_t index);
    Item map_get(Map* map, Item key);
    Item elmt_get(Element *elmt, Item key);
    Item object_get(Object* obj, Item key);
    void object_type_set_method(int64_t type_index, const char* method_name,
                                fn_ptr func_ptr, int64_t arity, int64_t is_proc);
    void object_type_set_constraint(int64_t type_index, fn_ptr constraint_func);
    Item item_at(Item data, int64_t index);
    Item item_attr(Item data, const char* key);  // get attribute by name
    Item path_property_get(Path* path, const char* key);  // built-in Path properties (shared by fn_member/item_attr)
    SymbolKeyList* item_keys(Item data);     // get typed list of Symbol* attribute names
    SymbolKeyList* symbol_key_list_new(int64_t initial_capacity);
    bool symbol_key_list_append(SymbolKeyList* keys_ptr, Symbol* symbol);
    int64_t symbol_key_list_len(void* keys_ptr);
    Symbol* symbol_key_list_at(void* keys_ptr, int64_t index);
    void symbol_key_list_free(void* keys_ptr);

    // Unified for-loop iteration helpers (key_filter: 0=ALL, 1=INT, 2=SYMBOL)
    int64_t iter_len(Item data, void* keys_ptr, int key_filter);
    Item iter_key_at(Item data, void* keys_ptr, int64_t idx, int key_filter);
    Item iter_val_at(Item data, void* keys_ptr, int64_t idx, int key_filter);

    Bool is_truthy(Item item);
    Item v2it(List *list);

    Item flt2it(double dval);  // canonical double -> Item encoder
    Item int2it(double value);     // integral double -> int Item (boundary form)
    Item int2it_lane(int64_t lane); // LANE value -> int Item (v5 canonical encoder)
    double lambda_int_lane_to_double_c(int64_t lane);
    double lambda_float_null_lane_c(void);
    int64_t lambda_double_to_int_lane_c(double value);
    int64_t lambda_item_to_int_lane_c(uint64_t item_bits);
    int64_t lambda_int_lane_add_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_sub_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_mul_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_divmod_slow(int64_t a, int64_t b, int64_t is_mod);
    Item int2it_i64(int64_t value); // same encoder, native-int64 caller
    Item int2it_i64_or_error(int64_t value); // + legacy INT64_ERROR boundary
    Item push_d(double dval);
    Item box_int64_value(int64_t lval);
    // Compatibility boundary for legacy native helpers whose raw int64 result
    // uses INT64_ERROR as an out-of-band failure signal.
    Item box_int64_result_or_error(int64_t lval);
    Item box_uint64_value(uint64_t uval);
    Item push_d_safe(double val);   // safe boxing: detects already-boxed FLOAT Items
    Item push_k(DateTime dtval);
    Item push_c(int64_t cval);

    // Const pool pointer — modules override this single macro to redirect to module-local consts
    #define _const_pool  rt->consts

    #define const_d2it(index)    lambda_float_ptr_to_item((const double*)_const_pool[index])
    #define const_l2it(index)    l2it(_const_pool[index])
    #define const_c2it(index)    c2it(_const_pool[index])
    #define const_s2it(index)    s2it(_const_pool[index])
    #define const_y2it(index)    y2it(_const_pool[index])
    #define const_k2it(index)    push_k(const_k(index))
    #define const_x2it(index)    x2it(_const_pool[index])

    #define const_s(index)      ((String*)_const_pool[index])
    #define const_x(index)      ((Binary*)_const_pool[index])
    #define const_c(index)      ((Decimal*)_const_pool[index])
    #define const_k(index)      (*(DateTime*)_const_pool[index])

    // item unboxing
    int64_t it2l(Item item);
    uint64_t it2u(Item item);
    double it2d(Item item);
    bool it2b(Item item);
    int64_t it2i(Item item);
    DateTime* it2k(Item item);
    String* it2s(Item item);
    Binary* it2x(Item item);
    const char* fn_to_cstr(Item item);  // convert Item to C string (for path segment names)
    Item coerce_num_sized(Item value, int64_t num_type);
    Item coerce_uint64(Item value);

    // MIR JIT workaround: opaque store functions prevent SSA optimizer from
    // reordering swap-pattern assignments inside while loops.
    // Since these are external functions, MIR can't inline or reorder them.
    void _store_i64(int64_t* dst, int64_t val);
    void _store_f64(double* dst, double val);

    // Safe unbox to int64_t for bitwise operation arguments.
    // Handles both tagged Items (type tag in high byte) and raw int64_t values
    // (from other bitwise ops or literals, with high byte == 0).
    int64_t _barg(Item v);

    // generic field access function
    Item fn_index(Item item, Item index);
    int64_t fn_int64_index(Item item);
    Item fn_member(Item item, Item key);
    Item fn_member_by_id(Item item, uint32_t name_id);
    // length function
    int64_t fn_len(Item item);
    Item fn_int(Item a);
    int64_t fn_int64(Item a);
    Item fn_float(Item a);
    Item fn_decimal(Item a);
    Item fn_binary(Item a);
    Item fn_complex1(Item a);
    Item fn_complex2(Item real, Item imag);
    Item fn_real(Item a);
    Item fn_imag(Item a);
    Item fn_conj(Item a);
    Item complex_new(double real, double imag);
    Item fn_complex_sqrt(Item a);
    Item fn_complex_log(Item a);
    Item fn_complex_exp(Item a);
    Item fn_complex_sin(Item a);
    Item fn_complex_cos(Item a);
    Item fn_complex_tan(Item a);

    Item fn_add(Item a, Item b);
    Item fn_mul(Item a, Item b);
    Item fn_sub(Item a, Item b);
    Item fn_div(Item a, Item b);
    Item fn_idiv(Item a, Item b);
    Item fn_pow(Item a, Item b);
    Item fn_mod(Item a, Item b);
    Item fn_abs(Item a);
    Item fn_round(Item a);
    Item fn_floor(Item a);
    Item fn_ceil(Item a);
    Item fn_min1(Item a);
    Item fn_min2(Item a, Item b);
    Item fn_max1(Item a);
    Item fn_max2(Item a, Item b);
    Item fn_sum(Item a);
    Item fn_avg(Item a);
    Item fn_avg_skip_null(Item a, bool skip_null);
    Item fn_union(Item a, Item b);
    Item fn_pos(Item a);
    Item fn_neg(Item a);

    // truthy idioms
    Item fn_and(Item a, Item b);
    Item fn_or(Item a, Item b);
    Item op_and(Bool a, Bool b);
    Item op_or(Bool a, Bool b);

    Bool fn_eq(Item a, Item b);
    Bool fn_ne(Item a, Item b);
    Bool fn_str_eq_ptr(String* a, String* b);
    Bool fn_sym_eq_ptr(Symbol* a, Symbol* b);
    // Ordered comparisons return Item (Any): a boolean mask when an operand is an
    // ARRAY_NUM (vectorized), else a boxed scalar bool. The *_scalar helpers expose
    // the raw 3-state scalar comparison for callers that only compare scalars.
    Bool fn_lt_scalar(Item a, Item b);
    Bool fn_gt_scalar(Item a, Item b);
    Bool fn_le_scalar(Item a, Item b);
    Bool fn_ge_scalar(Item a, Item b);
    int total_cmp(Item a, Item b);
    Item fn_lt(Item a, Item b);
    Item fn_gt(Item a, Item b);
    Item fn_le(Item a, Item b);
    Item fn_ge(Item a, Item b);
    Bool fn_not(Item a);
    Bool fn_is(Item a, Item b);
    // Type-boundary primitives used by the MIR emitter. `lambda_type_check`
    // returns its input on success and a diagnostic-carrying Error Item on a
    // mismatch; no native lane may be entered before this succeeds.
    bool lambda_type_matches(Item value, Type* expected);
    Item lambda_type_error(Item actual, Type* expected, const char* boundary);
    Item lambda_type_check(Item value, Type* expected, const char* boundary);
    Item lambda_map_set_checked(Item owner, Item key, Item value, Type* expected,
        const char* boundary);
    Item lambda_map_set_checked_inplace(Item owner, Item key, Item value, Type* expected,
        const char* boundary);
    Item lambda_map_path_set_checked(Item owner, Item path, Item value, Type* expected,
        const char* boundary);
    Item lambda_array_set_checked(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary);
    Item lambda_array_set_checked_inplace(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary);
    Item lambda_array_set_checked_lane(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary, uint8_t lane_kind, uint8_t lane_nullable,
        uint8_t lane_byte_size);
    Item lambda_array_set_checked_inplace_lane(Item owner, int64_t index, Item value,
        Type* expected, const char* boundary, uint8_t lane_kind, uint8_t lane_nullable,
        uint8_t lane_byte_size);
    Bool fn_is_nan(Item a);  // IEEE NaN check: expr is nan
    Bool fn_in(Item a, Item b);
    Bool fn_at(Item a, Item b);

    // query operations: search data for items matching a type
    Item fn_query(Item data, Item type_val, int direct);

    // vector arithmetic operations (element-wise)
    Item vec_add(Item a, Item b);
    Item vec_sub(Item a, Item b);
    Item vec_mul(Item a, Item b);
    Item vec_div(Item a, Item b);
    Item vec_mod(Item a, Item b);
    Item vec_pow(Item a, Item b);

    // whole-array typed reduction → double accumulator, correct for every element
    // type (the per-type fn_sum/min/max paths only handled INT/INT64/FLOAT).  op
    // codes match ReduceOp.  Contiguous arrays use the vectorized kernel.
    enum { ARRAY_RED_SUM = 0, ARRAY_RED_PROD = 1, ARRAY_RED_MIN = 2, ARRAY_RED_MAX = 3, ARRAY_RED_AVG = 4 };
    double array_num_reduce_double(ArrayNum* arr, int op);

    // vector system functions (math module)
    Item fn_math_prod(Item a);
    Item fn_math_cumsum(Item a);
    Item fn_math_cumprod(Item a);
    Item fn_argmin(Item a);
    Item fn_argmax(Item a);
    Item fn_fill(Item n, Item value);
    Item fn_math_dot(Item a, Item b);
    Item fn_math_norm(Item a);
    // statistical functions (math module)
    Item fn_math_mean(Item a);
    Item fn_math_mean_skip_null(Item a, bool skip_null);
    Item fn_math_median(Item a);
    Item fn_math_median_skip_null(Item a, bool skip_null);
    Item fn_math_variance(Item a);
    Item fn_math_variance_skip_null(Item a, bool skip_null);
    Item fn_math_deviation(Item a);
    Item fn_math_deviation_skip_null(Item a, bool skip_null);
    // element-wise math functions (math module)
    Item fn_math_sqrt(Item a);
    Item fn_math_log(Item a);
    Item fn_math_log10(Item a);
    Item fn_math_exp(Item a);
    Item fn_math_sin(Item a);
    Item fn_math_cos(Item a);
    Item fn_math_tan(Item a);
    // inverse trigonometric
    Item fn_math_asin(Item a);
    Item fn_math_acos(Item a);
    Item fn_math_atan(Item a);
    Item fn_math_atan2(Item a, Item b);
    // hyperbolic
    Item fn_math_sinh(Item a);
    Item fn_math_cosh(Item a);
    Item fn_math_tanh(Item a);
    // inverse hyperbolic
    Item fn_math_asinh(Item a);
    Item fn_math_acosh(Item a);
    Item fn_math_atanh(Item a);
    // exponential/logarithmic variants
    Item fn_math_exp2(Item a);
    Item fn_math_expm1(Item a);
    Item fn_math_log2(Item a);
    // power/root
    Item fn_math_pow(Item a, Item b);
    Item fn_math_cbrt(Item a);
    Item fn_trunc(Item a);
    Item fn_math_hypot(Item a, Item b);
    Item fn_math_log1p(Item a);
    Item fn_sign(Item a);
    // random number generation (pure functional, SplitMix64)
    Item fn_math_random(Item seed);

    // ============================================================================
    // UNBOXED SYSTEM FUNCTIONS (fn_*_u)
    // Native C implementations that bypass Item boxing overhead.
    // Called directly when types are known at compile time.
    // ============================================================================

    // Math functions (double → double)
    double fn_pow_u(double base, double exponent);
    double fn_min2_u(double a, double b);
    double fn_max2_u(double a, double b);

    // Integer operations
    int64_t fn_abs_i(int64_t x);
    double fn_abs_f(double x);
    int64_t fn_neg_i(int64_t x);
    double fn_neg_f(double x);
    int64_t fn_mod_i(int64_t a, int64_t b);    // handles div-by-zero (returns INT64_ERROR)
    int64_t fn_idiv_i(int64_t a, int64_t b);   // handles div-by-zero (returns INT64_ERROR)

    // Collection length — type-specialized native variants
    // G0: these return a Lambda `int`, so they return int's one native
    // representation. Their receivers are raw pointers, so none can fail.
    int64_t fn_len_l(List* list);       // list length
    int64_t fn_len_a(Array* arr);       // array length
    int64_t fn_len_s(String* str);      // string length (UTF-8 aware)
    int64_t fn_len_e(Element* elmt);    // element children count

    // Boolean operations
    Bool fn_not_u(Bool x);

    // Sign function
    int64_t fn_sign_i(int64_t x);
    int64_t fn_sign_f(double x);

    // JS Math.round (rounds to +Infinity for ties)
    double js_math_round(double x);

    // JS-semantic Math functions (boxed Item → boxed Item, handle NaN/-0/Infinity)
    Item js_math_trunc(Item x);
    Item js_math_sign(Item x);
    Item js_math_floor(Item x);
    Item js_math_ceil(Item x);
    double js_math_ceil_d(double d);
    Item js_math_round_item(Item x);

    // String.raw tagged template literal
    Item js_string_raw(Item* args, int argc);

    // Mark method functions (non-constructable, no prototype)
    void js_mark_method_func(Item fn_item);

    // Mark functions as strict mode
    void js_mark_strict_func(Item fn_item);

    // Rounding functions (int versions return identity)
    int64_t fn_floor_i(int64_t x);
    int64_t fn_ceil_i(int64_t x);
    int64_t fn_round_i(int64_t x);

    // vector manipulation functions
    Item fn_reverse(Item a);
    Item fn_sort1(Item a);
    Item fn_sort2(Item a, Item dir);
    void fn_sort_by_keys(Item values, Item keys, int64_t descending);
    Item fn_unique(Item a);
    Item fn_take(Item a, Item n);
    Item fn_take_last(Item a, Item n);
    Item fn_drop(Item a, Item n);
    Item fn_slice(Item a, Item start, Item end);
    Item fn_slice3(Item a, Item start, Item end);
    Item fn_slice2(Item a, Item start);
    Item fn_clip(Item a, Item lo, Item hi);
    Item fn_all(Item a);
    Item fn_any(Item a);
    Item fn_subview(Item arr, Item start, Item end);  // read-only view over arr[start..end]
    Item fn_is_view(Item arr);                     // returns true if arr is a view
    Item fn_reshape(Item arr, Item shape);         // returns view with new shape (contiguous required)
    Item fn_shape(Item arr);                       // returns shape as a list
    Item fn_ndim(Item arr);                        // returns number of dimensions (int)
    Item fn_transpose(Item arr);                   // view with reversed axes
    Item fn_flatten(Item arr);                     // owned 1-D contiguous copy
    Item fn_ravel(Item arr);                       // 1-D view if contiguous, else copy
    Item fn_matmul(Item a, Item b);                // matrix product
    Item fn_concat(Item a, Item b);                // join along axis 0
    Item fn_stack(Item a, Item b);                 // stack along new leading axis
    Item pn_push(Item arr, Item value);            // append value to a growable array in place
    Item pn_splice(Item arr, Item start, Item count); // remove count elements at start, in place
    Item pn_push_cow(Item owner, Item value);
    Item pn_splice_cow(Item owner, Item start, Item count);

    // image stencil engine: slide a Kh×Kw window over the spatial dims of `in`
    // (2-D H×W, or 3-D H×W×C applied per-channel) and reduce at each position.
    // op: 0=DOT 1=MIN 2=MAX 3=MEDIAN 4=MEAN.  border: 0=CONSTANT 1=EDGE 2=REFLECT
    // 3=WRAP.  pad_h/pad_w: window-start offset from each output's input position
    // (negative → centred at Kh/2, Kw/2 for same-size filtering; 0 → top-left for
    // pooling).  Result is ELEM_FLOAT64.  Covers convolution/morphology/rank/pooling.
    Item array_num_stencil(Item in, Item kernel, int op, int border, double border_value,
                           int64_t stride_h, int64_t stride_w, int64_t pad_h, int64_t pad_w);
    Item fn_convolve(Item img, Item kernel);       // weighted-sum correlation (DOT)
    Item fn_blur(Item img, Item ksize);            // box (mean) blur
    Item fn_erode(Item img, Item ksize);           // morphological min
    Item fn_dilate(Item img, Item ksize);          // morphological max
    Item fn_median_filter(Item img, Item ksize);   // rank (median) filter
    Item fn_maxpool(Item img, Item ksize);         // strided max pooling
    Item fn_avgpool(Item img, Item ksize);         // strided mean pooling
    // image I/O bridge: load/save PNG-JPEG-GIF, ubyte[0,255] <-> float[0,1]
    Item fn_load(Item path);                       // decode to (H,W,4) ubyte RGBA
    Item fn_save(Item img, Item path);             // encode an image to PNG
    Item fn_as_float(Item img);                    // ubyte [0,255] -> float [0,1]
    Item fn_as_ubyte(Item img);                    // float [0,1] -> ubyte [0,255]
    // point / colour / geometric image ops
    Item fn_invert(Item img);                      // photographic negative
    Item fn_gamma(Item img, Item g);               // gamma correction
    Item fn_threshold(Item img, Item t);           // binarize at threshold t
    Item fn_grayscale(Item img);                   // RGB -> luma, (H,W,C)->(H,W)
    Item fn_flip(Item img, Item axis);             // mirror along axis 0 (vert) / 1 (horiz)
    Item fn_rot90(Item img, Item k);               // rotate CCW by 90*k degrees
    Item fn_crop(Item img, Item rows, Item cols);  // owned copy of an inclusive region
    // histogram / segmentation / resize / warp
    Item fn_histogram(Item img, Item bins);        // 1-D value counts
    Item fn_otsu(Item img);                        // optimal threshold value
    Item fn_label(Item mask);                      // 4-connected component labels
    Item fn_resize(Item img, Item h, Item w);      // bilinear resample
    Item fn_rotate(Item img, Item deg);            // bilinear rotation about centre
    Item fn_affine_warp(Item img, Item m);         // 2x3 affine coordinate gather
    int64_t array_num_iter_count(ArrayNum* arr);   // shape[0] for N-D, length for 1-D
    ArrayNum* array_num_new_ndim(ArrayNumElemType elem_type, int64_t total, int ndim, int64_t* dims);
    Item array_num_at_nd(ArrayNum* arr, int ndim, int64_t* indices);   // multi-dim scalar read
    void array_num_set_nd(ArrayNum* arr, int ndim, int64_t* indices, Item value); // multi-dim write
    Item fn_zip(Item a, Item b);
    Item fn_range3(Item start, Item end, Item step);
    Item fn_math_quantile(Item a, Item p);
    Item fn_math_quantile_skip_null(Item a, Item p, bool skip_null);
    Item fn_reduce(Item collection, Item func);

    Item fn_to(Item a, Item b);

    // pipe operations
    typedef Item (*PipeMapFn)(Item item, Item index);
    Item fn_pipe_map(Item collection, PipeMapFn transform);
    Item fn_pipe_where(Item collection, PipeMapFn predicate);
    Item fn_pipe_call(Item collection, Item func);

    String* fn_string(Item item);
    String *fn_strcat(String *left, String *right);
    String *fn_string_freeze(String *str);
    Item fn_normalize(Item str, Item type);
    Item fn_normalize1(Item str);           // normalize with default NFC
    Item fn_substring(Item str, Item start, Item end);
    Bool fn_contains(Item str, Item substr);
    Item fn_join(Item a, Item b);
    // string functions
    Bool fn_starts_with(Item str, Item prefix);
    Bool fn_starts_with_str(String* str, String* prefix);   // native String* variant
    Bool fn_ends_with(Item str, Item suffix);
    Bool fn_ends_with_str(String* str, String* suffix);     // native String* variant
    Item fn_index_of(Item str, Item sub);
    Item fn_last_index_of(Item str, Item sub);
    int64_t fn_index_of_raw(Item str, Item sub);       // C/JS -1 sentinel adapter
    int64_t fn_last_index_of_raw(Item str, Item sub);  // C/JS -1 sentinel adapter
    Item fn_trim(Item str);
    Item fn_trim_start(Item str);
    Item fn_trim_end(Item str);
    Item fn_lower(Item str);
    Item fn_upper(Item str);
    Item fn_url_resolve(Item base, Item relative);
    Item fn_split(Item str, Item sep);
    Item fn_split3(Item str, Item sep, Item keep_delim);
    Item fn_array_split(Item arr, int64_t n, int64_t axis);  // split typed array into n parts along axis
    // axis-aware reductions (2-arg): collapse `axis` of a typed N-D array
    Item fn_sum_axis(Item arr, Item axis);
    Item fn_avg_axis(Item arr, Item axis);
    Item fn_prod_axis(Item arr, Item axis);
    Item fn_cumsum_axis(Item arr, Item axis);    // running sum along axis (no collapse)
    Item fn_cumprod_axis(Item arr, Item axis);   // running product along axis (no collapse)
    Item fn_min_axis(Item arr, Item axis);       // min along axis (via min(arr, axis) / min(arr, axis:N))
    Item fn_max_axis(Item arr, Item axis);       // max along axis
    Item fn_mask_index(Item arr, Item mask);     // arr[mask] — boolean mask selection
    void fn_index_assign(Item arr, Item idx, Item val);  // arr[mask] = v — masked write (procedural in-place)
    Item vec_cmp(Item a, Item b, int op);        // vectorized a OP b → ELEM_BOOL mask (op = operator-OPERATOR_EQ)
    // overload wrappers (the sysfunc dispatcher resolves fn_<name><argcount>)
    Item fn_sum1(Item arr);            Item fn_sum2(Item arr, Item axis);
    Item fn_avg1(Item arr);            Item fn_avg2(Item arr, Item axis);
    Item fn_math_prod1(Item arr);      Item fn_math_prod2(Item arr, Item axis);
    Item fn_math_mean1(Item arr);      Item fn_math_mean2(Item arr, Item axis);
    Item fn_math_median1(Item arr);
    Item fn_math_median2(Item arr, Item option);
    Item fn_math_variance1(Item arr);
    Item fn_math_variance2(Item arr, Item option);
    Item fn_math_deviation1(Item arr);
    Item fn_math_deviation2(Item arr, Item option);
    Item fn_math_quantile2(Item arr, Item p);
    Item fn_math_quantile3(Item arr, Item p, Item option);
    Item fn_math_cumsum1(Item arr);    Item fn_math_cumsum2(Item arr, Item axis);
    Item fn_math_cumprod1(Item arr);   Item fn_math_cumprod2(Item arr, Item axis);
    Item fn_split2(Item str, Item sep);  // overloaded alias for fn_split
    Item fn_ord(Item str);              // ord(str) - Unicode code point or null
    int64_t fn_ord_str(String* str);    // native raw variant with C/JS -1 sentinel
    Item fn_ord_str_item(String* str);  // native Lambda-facing nullable result
    Item fn_chr(Item codepoint);        // chr(int) - 1-char string from Unicode code point
    Item fn_join2(Item list, Item sep);
    Item fn_replace(Item str, Item old_str, Item new_str);
    Item fn_replace3(Item str, Item old_str, Item new_str);  // overloaded alias for fn_replace
    Item fn_replace4(Item str, Item old_str, Item new_str, Item options);
    Item fn_find2(Item source, Item pattern);
    Item fn_find3(Item source, Item pattern, Item options);

    Type* base_type(TypeId type_id);
    Type* const_type(int64_t type_index);
    Type* const_type_with_tl(int64_t type_index, void* type_list_ptr);
    TypePattern* const_pattern(int64_t pattern_index);  // retrieve compiled pattern by index
    TypePattern* const_pattern_with_tl(int64_t pattern_index, void* type_list_ptr);

    // returns the type of the item
    Type* fn_type(Item item);
    TypeId item_type_id(Item item);  // returns the TypeId of an item (for MIR use)

    // returns the name of an element, function, or type as a symbol
    Symbol* fn_name(Item item);

    RetItem fn_input1(Item url);
    RetItem fn_input2(Item url, Item options);
    RetItem fn_parse1(Item str);
    RetItem fn_parse2(Item str, Item options);
    Item fn_parse_html_fragment1(Item str);
    String* fn_format1(Item item);
    String* fn_format2(Item item, Item options);
    Item fn_error(Item message);  // raise a user-defined error
    Symbol* fn_symbol1(Item item);  // convert to symbol
    Item fn_symbol2(Item name, Item url);  // create namespaced symbol

    // view/edit template apply
    Item fn_apply1(Item target);
    Item fn_apply2(Item target, Item options);

    Item fn_typeset_latex(Item input_file, Item output_file, Item options);

    // datetime constructors
    DateTime fn_datetime0();                       // datetime() - current datetime
    DateTime fn_datetime1(Item arg);               // datetime(str) - parse from string
    DateTime fn_date0();                           // date() - current date
    DateTime fn_date1(Item arg);                   // date(dt) - extract date portion
    DateTime fn_date3(Item y, Item m, Item d);     // date(y,m,d) - construct from components
    DateTime fn_time0();                           // time() - current time
    DateTime fn_time1(Item arg);                   // time(dt) - extract time portion
    DateTime fn_time3(Item h, Item m, Item s);     // time(h,m,s) - construct from components
    DateTime fn_justnow();                         // justnow() - current ms timestamp

    // variadic parameter access
    List* set_vargs(List* vargs);     // set current variadic args, returns previous
    void restore_vargs(List* prev);   // restore previous variadic args
    Item fn_varg0();                  // varg() - get all variadic args as list
    Item fn_varg1(Item index);        // varg(n) - get nth variadic arg

    // procedural functions
    Item pn_print(Item item);
    double pn_clock();        // clock() - high-resolution monotonic time in seconds
    RetItem pn_cmd1(Item cmd);
    RetItem pn_cmd2(Item cmd, Item args);
    RetItem pn_fetch(Item url, Item options);
    RetItem pn_output2(Item source, Item target);            // output(data, trg) - writes data to target, returns bytes written
    RetItem pn_output3(Item source, Item target, Item options);  // output(data, trg, options) - options: map {format, mode, atomic}, symbol/string (format), or null
    RetItem pn_output_append(Item source, Item target);      // used by |>> pipe operator (append mode)

    // io module functions (procedural)
    RetItem pn_io_copy(Item src, Item dst);
    RetItem pn_io_read(Item target);
    RetItem pn_io_move(Item src, Item dst);
    RetItem pn_io_delete(Item path);
    RetItem pn_io_mkdir(Item path);
    RetItem pn_io_touch(Item path);
    RetItem pn_io_symlink(Item target, Item link);
    RetItem pn_io_chmod(Item path, Item mode);
    RetItem pn_io_rename(Item old_path, Item new_path);
    RetItem pn_io_fetch1(Item target);
    RetItem pn_io_fetch2(Item target, Item options);

    // bitwise functions (integer operations)
    int64_t fn_band(int64_t a, int64_t b);
    int64_t fn_bor(int64_t a, int64_t b);
    int64_t fn_bxor(int64_t a, int64_t b);
    int64_t fn_bnot(int64_t a);
    int64_t fn_shl(int64_t a, int64_t b);
    int64_t fn_shr(int64_t a, int64_t b);
    Item fn_band_item(Item a, Item b);
    Item fn_bor_item(Item a, Item b);
    Item fn_bxor_item(Item a, Item b);
    Item fn_bnot_item(Item a);
    Item fn_shl_item(Item a, Item b);
    Item fn_shr_item(Item a, Item b);
    Item fn_ushr_item(Item a, Item b);

    // compound assignment support (procedural only)
    Item fn_mutable_value(Item value);
    Item fn_array_set(Array* arr, int64_t index, Item value);
    void fn_map_set(Item map, Item key, Item value);
    bool cow_item_is_container(Item value);
    Item cow_mark_shared(Item value);
    Item cow_bind_var(Item value);
    Item cow_prepare_write(Item old);
    // Optional release-safe COW instrumentation.  It stays dormant unless
    // COW_EXEC_PROFILE is enabled, and raw JS/host setters never call it.
    void cow_profile_note_vmap_snapshot(void);
    void cow_profile_note_vmap_rejection(void);
    void cow_profile_dump(void);
    Item array_set_cow(Item owner, int64_t index, Item value);
    Item map_set_cow(Item owner, Item key, Item value);
    Item cow_path_set_raw(Item owner, Item key, Item value);
    Item cow_path_set(Item owner, Item path, Item value);

    // runtime type coercion for typed array annotations (int[], float[], etc.)
    // converts generic Array/List to typed array, or validates existing typed array
    // returns pointer to the coerced typed array, or NULL if elements are incompatible
    void* ensure_typed_array(Item item, TypeId element_type_id);
    void* ensure_sized_array(Item item, int64_t elem_type);

    // VMap system functions
    Item vmap_new();
    Item vmap_from_array(Item array_item);
    void vmap_set(Item vmap_item, Item key, Item value);
    Item vmap_set_cow(Item owner, Item key, Item value);
    Item vmap_clone_for_cow(Item source);

    // reactive UI: emit event to parent template handler
    Item pn_emit(Item event_name, Item event_data);
    // called from Radiant side — dispatches emitted event up the DOM ancestry
    Item dispatch_emit(Item event_name, Item event_data);

    // editor: push a SourceSelection back to the live DomSelection (Phase R4 §7.4)
    Item pn_set_selection(Item selection);
    Item dispatch_set_selection(Item selection);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
