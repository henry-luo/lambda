#pragma once
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/str.h"
#include "../lib/url.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/math_utils.h"
#include "../lib/memtrack.h"
#include "../lib/font/font.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lambda/input/css/css_style.hpp"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

// ===== computed CSS property access =====

enum PropGroupKind : uint8_t {
    PROP_GROUP_NONE = 0,
    PROP_GROUP_BLOCK,
    PROP_GROUP_BOUNDARY,
    PROP_GROUP_FONT,
    PROP_GROUP_INLINE,
    PROP_GROUP_SCROLL,
    PROP_GROUP_POSITION,
    PROP_GROUP_FLEX_ITEM,
    PROP_GROUP_GRID_ITEM,
};

enum CssPropValueKind : uint8_t {
    CSS_PROP_VALUE_SPECIAL = 0,
    CSS_PROP_VALUE_ENUM,
    CSS_PROP_VALUE_PX,
    CSS_PROP_VALUE_NUMBER,
    CSS_PROP_VALUE_INTEGER,
    CSS_PROP_VALUE_COLOR,
    CSS_PROP_VALUE_STRING,
};

enum CssPropAccessorFlags : uint8_t {
    CSS_PROP_ACCESSOR_USED_VALUE = 1u << 0,
};

struct CssPropAccessor;
typedef bool (*CssPropSerializeFn)(const CssPropAccessor* accessor,
                                   DomElement* element, int pseudo_type,
                                   char* out, size_t out_size);
typedef CssPropSerializeFn CssPropDeriveFn;

// The row shape is intentionally plain and pointer-free apart from callbacks so
// Jube's record dispatch can index the same immutable descriptors.
struct CssPropAccessor {
    CssPropertyId id;
    PropGroupKind group_kind;
    uint16_t offset;
    CssPropValueKind value_kind;
    uint8_t flags;
    CssPropSerializeFn serialize;
    CssPropDeriveFn derive;
};

const CssPropAccessor* css_prop_accessor(CssPropertyId id);
const CssPropAccessor* css_prop_accessors(size_t* count);
bool css_prop_serialize_computed(DomElement* element, CssPropertyId id,
                                 int pseudo_type, char* out, size_t out_size);
bool dom_ensure_computed(DomElement* element, bool needs_used_value = false);

// ===== animation =====

struct DirtyTracker;

DomElement* dom_select_next_option(DomElement* select, DomElement* previous);
const char* dom_option_text(DomElement* option);

typedef enum AnimationType {
    ANIM_CSS_ANIMATION = 0,
    ANIM_CSS_TRANSITION,
    ANIM_GIF,
    ANIM_LOTTIE,
} AnimationType;

typedef enum AnimationDirection {
    ANIM_DIR_NORMAL = 0,
    ANIM_DIR_REVERSE,
    ANIM_DIR_ALTERNATE,
    ANIM_DIR_ALTERNATE_REVERSE,
} AnimationDirection;

typedef enum AnimationFillMode {
    ANIM_FILL_NONE = 0,
    ANIM_FILL_FORWARDS,
    ANIM_FILL_BACKWARDS,
    ANIM_FILL_BOTH,
} AnimationFillMode;

typedef enum AnimationPlayState {
    ANIM_PLAY_RUNNING = 0,
    ANIM_PLAY_PAUSED,
    ANIM_PLAY_FINISHED,
} AnimationPlayState;

typedef enum TimingFunctionType {
    TIMING_LINEAR = 0,
    TIMING_CUBIC_BEZIER,
    TIMING_STEPS,
} TimingFunctionType;

typedef enum StepPosition {
    STEP_JUMP_END = 0,
    STEP_JUMP_START,
    STEP_JUMP_BOTH,
    STEP_JUMP_NONE,
} StepPosition;

// tier-2: view-pool, rebuilt each relayout
typedef struct TimingFunction {
    TimingFunctionType type;
    union {
        struct {
            float x1, y1, x2, y2;
            float samples[11];
        } bezier;
        struct {
            int count;
            StepPosition position;
        } steps;
    };
} TimingFunction;

void timing_cubic_bezier_init(TimingFunction* tf, float x1, float y1, float x2, float y2);
float timing_function_eval(const TimingFunction* tf, float t);

extern TimingFunction TIMING_EASE;
extern TimingFunction TIMING_EASE_IN;
extern TimingFunction TIMING_EASE_OUT;
extern TimingFunction TIMING_EASE_IN_OUT;

void timing_init_presets();

typedef struct AnimationInstance AnimationInstance;
typedef void (*AnimTickFn)(AnimationInstance* anim, float t);
typedef void (*AnimFinishFn)(AnimationInstance* anim);
typedef void (*AnimCancelFn)(AnimationInstance* anim);

// tier-2: view-pool, rebuilt each relayout
struct AnimationInstance {
    AnimationInstance* next;
    AnimationInstance* prev;

    AnimationType type;
    void* target;
    void* state;

    double start_time;
    double duration;
    double delay;
    int iteration_count;
    int current_iteration;

    AnimationDirection direction;
    AnimationFillMode fill_mode;
    AnimationPlayState play_state;

    TimingFunction timing;

    AnimTickFn tick;
    AnimFinishFn on_finish;
    AnimCancelFn on_cancel;

    float bounds[4];
    double pause_time;
};

// tier-2: view-pool, rebuilt each relayout
typedef struct AnimationScheduler {
    AnimationInstance* first;
    AnimationInstance* last;
    int count;

    double current_time;
    bool has_active_animations;

    Pool* pool;
} AnimationScheduler;

AnimationScheduler* animation_scheduler_create(Pool* pool);
void animation_scheduler_destroy(AnimationScheduler* scheduler);
bool animation_scheduler_tick(AnimationScheduler* scheduler, double now,
                              DirtyTracker* dirty_tracker);
void animation_scheduler_add(AnimationScheduler* scheduler, AnimationInstance* anim);
void animation_scheduler_remove(AnimationScheduler* scheduler, AnimationInstance* anim);
void animation_scheduler_cancel(AnimationScheduler* scheduler, AnimationInstance* anim);
void animation_scheduler_remove_by_target(AnimationScheduler* scheduler, void* target);
void animation_scheduler_remove_views(AnimationScheduler* scheduler);
AnimationInstance* animation_instance_create(AnimationScheduler* scheduler);
void animation_instance_pause(AnimationInstance* anim, double now);
void animation_instance_resume(AnimationInstance* anim, double now);

// 3x3 affine transform matrix shared by view transforms and render backends.
// The layout matches ThorVG's Tvg_Matrix, but the type itself is Radiant-owned.
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    float e11, e12, e13;   // row 1: scale-x, shear-x, translate-x
    float e21, e22, e23;   // row 2: shear-y, scale-y, translate-y
    float e31, e32, e33;   // row 3: 0, 0, 1
} RdtMatrix;

static inline RdtMatrix rdt_matrix_identity(void) {
    RdtMatrix m = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
    return m;
}

// multiply two 3x3 affine matrices: result = a * b
static inline RdtMatrix rdt_matrix_multiply(const RdtMatrix* a, const RdtMatrix* b) {
    RdtMatrix r;
    r.e11 = a->e11 * b->e11 + a->e12 * b->e21 + a->e13 * b->e31;
    r.e12 = a->e11 * b->e12 + a->e12 * b->e22 + a->e13 * b->e32;
    r.e13 = a->e11 * b->e13 + a->e12 * b->e23 + a->e13 * b->e33;
    r.e21 = a->e21 * b->e11 + a->e22 * b->e21 + a->e23 * b->e31;
    r.e22 = a->e21 * b->e12 + a->e22 * b->e22 + a->e23 * b->e32;
    r.e23 = a->e21 * b->e13 + a->e22 * b->e23 + a->e23 * b->e33;
    r.e31 = a->e31 * b->e11 + a->e32 * b->e21 + a->e33 * b->e31;
    r.e32 = a->e31 * b->e12 + a->e32 * b->e22 + a->e33 * b->e32;
    r.e33 = a->e31 * b->e13 + a->e32 * b->e23 + a->e33 * b->e33;
    return r;
}

static inline void rdt_matrix_transform_point(const RdtMatrix* m,
                                              float x, float y,
                                              float* out_x, float* out_y) {
    if (!m || !out_x || !out_y) return;
    *out_x = m->e11 * x + m->e12 * y + m->e13;
    *out_y = m->e21 * x + m->e22 * y + m->e23;
}

static inline void rdt_matrix_transform_rect_bounds(const RdtMatrix* m,
                                                    float left, float top,
                                                    float right, float bottom,
                                                    float* out_left,
                                                    float* out_top,
                                                    float* out_right,
                                                    float* out_bottom) {
    if (!m || !out_left || !out_top || !out_right || !out_bottom) return;

    float tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3;
    rdt_matrix_transform_point(m, left, top, &tx0, &ty0);
    rdt_matrix_transform_point(m, right, top, &tx1, &ty1);
    rdt_matrix_transform_point(m, right, bottom, &tx2, &ty2);
    rdt_matrix_transform_point(m, left, bottom, &tx3, &ty3);

    *out_left = LMB_MIN(LMB_MIN(tx0, tx1), LMB_MIN(tx2, tx3));
    *out_right = LMB_MAX(LMB_MAX(tx0, tx1), LMB_MAX(tx2, tx3));
    *out_top = LMB_MIN(LMB_MIN(ty0, ty1), LMB_MIN(ty2, ty3));
    *out_bottom = LMB_MAX(LMB_MAX(ty0, ty1), LMB_MAX(ty2, ty3));
}

// create a translation matrix
static inline RdtMatrix rdt_matrix_translate(float tx, float ty) {
    RdtMatrix m = { 1, 0, tx,  0, 1, ty,  0, 0, 1 };
    return m;
}

#ifndef LAMBDA_HEADLESS
// On macOS, explicitly include OpenGL headers before GLFW
#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include <GLFW/glfw3.h>

// Windows OpenGL headers may not define these constants (they're from OpenGL 1.2+)
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

#endif // LAMBDA_HEADLESS

// Keep Radiant's historical unqualified helpers, but delegate the math to lib
// so layout/render code does not carry a second implementation of these rules.
template<typename T, typename U>
inline auto max(T a, U b) -> decltype(lib_math::max_mixed(a, b)) {
    return lib_math::max_mixed(a, b);
}

template<typename T, typename U>
inline auto min(T a, U b) -> decltype(lib_math::min_mixed(a, b)) {
    return lib_math::min_mixed(a, b);
}

template<typename T>
inline T abs(T a) {
    return lib_math::abs_val(a);
}

template<typename T>
inline T clamp(T value, T lo, T hi) {
    return lib_math::clamp(value, lo, hi);
}

template<typename T, typename U, typename V>
inline auto clamp(T value, U lo, V hi) -> decltype(lib_math::clamp_mixed(value, lo, hi)) {
    return lib_math::clamp_mixed(value, lo, hi);
}

template<typename T>
inline int sign(T a) {
    return lib_math::sign(a);
}

template<typename T>
inline T lerp(T a, T b, float t) {
    return (T)lib_math::lerp(a, b, t);
}

// Forward declarations
struct FontFaceDescriptor;
typedef struct FontFaceDescriptor FontFaceDescriptor;

// Define lexbor tag and CSS value constants first, before including headers that need them
enum {
    HTM_TAG__UNDEF,
    HTM_TAG__TEXT,          // text node
    HTM_TAG__EM_COMMENT,    // for HTML comments
    HTM_TAG__EM_DOCTYPE,    // for DOCTYPE declaration
    HTM_TAG_A,
    HTM_TAG_ABBR,
    HTM_TAG_ACRONYM,
    HTM_TAG_ADDRESS,
    HTM_TAG_ALTGLYPH,
    HTM_TAG_ALTGLYPHDEF,
    HTM_TAG_ALTGLYPHITEM,
    HTM_TAG_ANIMATECOLOR,
    HTM_TAG_ANIMATEMOTION,
    HTM_TAG_ANIMATETRANSFORM,
    HTM_TAG_ANNOTATION_XML,
    HTM_TAG_APPLET,
    HTM_TAG_AREA,
    HTM_TAG_ARTICLE,
    HTM_TAG_ASIDE,
    HTM_TAG_AUDIO,
    HTM_TAG_B,
    HTM_TAG_BASE,
    HTM_TAG_BASEFONT,
    HTM_TAG_BDI,
    HTM_TAG_BDO,
    HTM_TAG_BGSOUND,
    HTM_TAG_BIG,
    HTM_TAG_BLINK,
    HTM_TAG_BLOCKQUOTE,
    HTM_TAG_BODY,
    HTM_TAG_BR,
    HTM_TAG_BUTTON,
    HTM_TAG_CANVAS,
    HTM_TAG_CAPTION,
    HTM_TAG_CENTER,
    HTM_TAG_CITE,
    HTM_TAG_CLIPPATH,
    HTM_TAG_CODE,
    HTM_TAG_COL,
    HTM_TAG_COLGROUP,
    HTM_TAG_DATA,
    HTM_TAG_DATALIST,
    HTM_TAG_DD,
    HTM_TAG_DEL,
    HTM_TAG_DESC,
    HTM_TAG_DETAILS,
    HTM_TAG_DFN,
    HTM_TAG_DIALOG,
    HTM_TAG_DIR,
    HTM_TAG_DIV,
    HTM_TAG_DL,
    HTM_TAG_DT,
    HTM_TAG_EM,
    HTM_TAG_EMBED,
    HTM_TAG_FEBLEND,
    HTM_TAG_FECOLORMATRIX,
    HTM_TAG_FECOMPONENTTRANSFER,
    HTM_TAG_FECOMPOSITE,
    HTM_TAG_FECONVOLVEMATRIX,
    HTM_TAG_FEDIFFUSELIGHTING,
    HTM_TAG_FEDISPLACEMENTMAP,
    HTM_TAG_FEDISTANTLIGHT,
    HTM_TAG_FEDROPSHADOW,
    HTM_TAG_FEFLOOD,
    HTM_TAG_FEFUNCA,
    HTM_TAG_FEFUNCB,
    HTM_TAG_FEFUNCG,
    HTM_TAG_FEFUNCR,
    HTM_TAG_FEGAUSSIANBLUR,
    HTM_TAG_FEIMAGE,
    HTM_TAG_FEMERGE,
    HTM_TAG_FEMERGENODE,
    HTM_TAG_FEMORPHOLOGY,
    HTM_TAG_FEOFFSET,
    HTM_TAG_FEPOINTLIGHT,
    HTM_TAG_FESPECULARLIGHTING,
    HTM_TAG_FESPOTLIGHT,
    HTM_TAG_FETILE,
    HTM_TAG_FETURBULENCE,
    HTM_TAG_FIELDSET,
    HTM_TAG_FIGCAPTION,
    HTM_TAG_FIGURE,
    HTM_TAG_FONT,
    HTM_TAG_FOOTER,
    HTM_TAG_FOREIGNOBJECT,
    HTM_TAG_FORM,
    HTM_TAG_FRAME,
    HTM_TAG_FRAMESET,
    HTM_TAG_GLYPHREF,
    HTM_TAG_H1,
    HTM_TAG_H2,
    HTM_TAG_H3,
    HTM_TAG_H4,
    HTM_TAG_H5,
    HTM_TAG_H6,
    HTM_TAG_HEAD,
    HTM_TAG_HEADER,
    HTM_TAG_HGROUP,
    HTM_TAG_HR,
    HTM_TAG_HTML,
    HTM_TAG_I,
    HTM_TAG_IFRAME,
    HTM_TAG_IMAGE,
    HTM_TAG_IMG,
    HTM_TAG_INPUT,
    HTM_TAG_INS,
    HTM_TAG_ISINDEX,
    HTM_TAG_KBD,
    HTM_TAG_KEYGEN,
    HTM_TAG_LABEL,
    HTM_TAG_LEGEND,
    HTM_TAG_LI,
    HTM_TAG_LINEARGRADIENT,
    HTM_TAG_LINK,
    HTM_TAG_LISTING,
    HTM_TAG_MAIN,
    HTM_TAG_MALIGNMARK,
    HTM_TAG_MAP,
    HTM_TAG_MARK,
    HTM_TAG_MARQUEE,
    HTM_TAG_MATH,
    HTM_TAG_MENU,
    HTM_TAG_META,
    HTM_TAG_METER,
    HTM_TAG_MFENCED,
    HTM_TAG_MGLYPH,
    HTM_TAG_MI,
    HTM_TAG_MN,
    HTM_TAG_MO,
    HTM_TAG_MS,
    HTM_TAG_MTEXT,
    HTM_TAG_MULTICOL,
    HTM_TAG_NAV,
    HTM_TAG_NEXTID,
    HTM_TAG_NOBR,
    HTM_TAG_NOEMBED,
    HTM_TAG_NOFRAMES,
    HTM_TAG_NOSCRIPT,
    HTM_TAG_OBJECT,
    HTM_TAG_OL,
    HTM_TAG_OPTGROUP,
    HTM_TAG_OPTION,
    HTM_TAG_OUTPUT,
    HTM_TAG_P,
    HTM_TAG_PARAM,
    HTM_TAG_PATH,
    HTM_TAG_PICTURE,
    HTM_TAG_PLAINTEXT,
    HTM_TAG_PRE,
    HTM_TAG_PROGRESS,
    HTM_TAG_Q,
    HTM_TAG_RADIALGRADIENT,
    HTM_TAG_RB,
    HTM_TAG_RP,
    HTM_TAG_RT,
    HTM_TAG_RTC,
    HTM_TAG_RUBY,
    HTM_TAG_S,
    HTM_TAG_SAMP,
    HTM_TAG_SCRIPT,
    HTM_TAG_SECTION,
    HTM_TAG_SELECT,
    HTM_TAG_SLOT,
    HTM_TAG_SMALL,
    HTM_TAG_SOURCE,
    HTM_TAG_SPACER,
    HTM_TAG_SPAN,
    HTM_TAG_STRIKE,
    HTM_TAG_STRONG,
    HTM_TAG_STYLE,
    HTM_TAG_SUB,
    HTM_TAG_SUMMARY,
    HTM_TAG_SUP,
    HTM_TAG_SVG,
    HTM_TAG_TABLE,
    HTM_TAG_TBODY,
    HTM_TAG_TD,
    HTM_TAG_TEMPLATE,
    HTM_TAG_TEXTAREA,
    HTM_TAG_TEXTPATH,
    HTM_TAG_TFOOT,
    HTM_TAG_TH,
    HTM_TAG_THEAD,
    HTM_TAG_TIME,
    HTM_TAG_TITLE,
    HTM_TAG_TR,
    HTM_TAG_TRACK,
    HTM_TAG_TT,
    HTM_TAG_U,
    HTM_TAG_UL,
    HTM_TAG_VAR,
    HTM_TAG_VIDEO,
    HTM_TAG_WBR,
    HTM_TAG_WEBVIEW,
    HTM_TAG_XMP,
    HTM_TAG__LAST_ENTRY         = 0x00c5
};

// radiant specific CSS display values
#define RDT_DISPLAY_TEXT                CSS_VALUE_TEXT
#define RDT_DISPLAY_REPLACED            CSS_VALUE__REPLACED

// Enum definitions needed by flex system
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

// margin-trim bitmask flags (CSS Box Level 4)
#define MARGIN_TRIM_BLOCK_START   0x01
#define MARGIN_TRIM_BLOCK_END     0x02
#define MARGIN_TRIM_INLINE_START  0x04
#define MARGIN_TRIM_INLINE_END    0x08

// text-box-trim bitmask flags (CSS Inline Level 3)
#define TEXT_BOX_TRIM_START       0x01   // trim over-side half-leading
#define TEXT_BOX_TRIM_END         0x02   // trim under-side half-leading

typedef enum AlignType {
    ALIGN_AUTO = CSS_VALUE_AUTO,
    ALIGN_START = CSS_VALUE_FLEX_START,
    ALIGN_END = CSS_VALUE_FLEX_END,
    ALIGN_CENTER = CSS_VALUE_CENTER,
    ALIGN_BASELINE = CSS_VALUE_BASELINE,
    ALIGN_STRETCH = CSS_VALUE_STRETCH,
    ALIGN_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    ALIGN_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    ALIGN_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} AlignType;

// static inline float pack_as_nan(int value) {
//     uint32_t bits = 0x7FC00000u | ((uint32_t)value & 0x003FFFFF);       // quiet NaN + payload
//     float f;
//     memcpy(&f, &bits, sizeof(f));                           // avoid aliasing UB
//     return f;
// }

// CSS auto packed as special NaN float value
// inline const float LENGTH_AUTO = pack_as_nan(CSS_VALUE_AUTO);

// inline bool is_length_auto(float a) {
//     uint32_t ia;
//     memcpy(&ia, &a, sizeof(a));
//     return (ia & 0x003FFFFF) == CSS_VALUE_AUTO;
// }

// tier-2: view-pool, rebuilt each relayout
typedef struct Rect {
    float x, y;
    float width, height;
} Rect;

// tier-2: view-pool, rebuilt each relayout
typedef struct Bound {
    float left, top, right, bottom;
} Bound;

typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_SVG,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_GIF,
} ImageFormat;

typedef enum {
    SCALE_MODE_NEAREST = 0,  // Nearest neighbor (fast, pixelated)
    SCALE_MODE_LINEAR,       // Bilinear interpolation (smooth)
    SCALE_MODE_LINEAR_WRAP,  // Bilinear with wrap-around for tiled backgrounds
} ScaleMode;

// tier-2: view-pool, rebuilt each relayout
typedef struct ImageSurface {
    ImageFormat format;
    int width;             // the intrinsic width of the surface/image (used for layout/intrinsic sizing)
    int height;            // the intrinsic height of the surface/image
    int encoded_width;     // raster source dimensions before image-orientation metadata
    int encoded_height;
    int orientation;       // EXIF orientation value, 1 when absent/normal/invalid
    bool has_intrinsic_size;
    int pitch;             // no. of bytes per row of the actual decoded pixel buffer
    // image pixels, 32-bits per pixel, RGBA format
    // pack order is [R] [G] [B] [A], high bit -> low bit
    void *pixels;          // A pointer to the pixels of the surface, the pixels are writeable if non-NULL
#ifndef LAMBDA_HEADLESS
    struct RdtPicture* pic;  // SVG picture (opaque, managed by rdt_vector API)
#endif
    int max_render_width;  // maximum width for rendering the image
    Url* url;        // the resolved absolute URL of the image
    bool cache_owned;      // true when UiContext image_cache owns this surface
    char* source_path;     // local file path for lazy decode (NULL if already decoded or HTTP)
    unsigned char* source_data;  // in-memory data for lazy decode of HTTP images (NULL if file-based)
    size_t source_data_len;      // length of source_data
    int tile_offset_y;   // tiled PNG rendering: physical-pixel Y start of this tile (0 = full-page surface)
    uint64_t generation; // incremented when borrowed pixels/picture content changes
    // Decoded buffer dimensions (for raster images decoded at a smaller scale than intrinsic).
    // If decoded_width > 0, these reflect the actual pixel buffer dims; pitch matches decoded_width*4.
    // Otherwise the buffer matches width/height/pitch above.
    int decoded_width;
    int decoded_height;
} ImageSurface;

extern ImageSurface* image_surface_create(int pixel_width, int pixel_height);
extern ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels);
extern void image_surface_destroy(ImageSurface* img_surface);
extern void image_surface_ensure_decoded(ImageSurface* img, int target_w, int target_h);
extern void image_surface_bump_generation(ImageSurface* img_surface);
extern void image_surface_detach_pixels(ImageSurface* img_surface);
extern void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip,
                              struct ClipShape** clip_shapes = nullptr, int clip_depth = 0);
extern void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode,
                                struct ClipShape** clip_shapes = nullptr, int clip_depth = 0);

extern bool can_break(char c);
extern bool is_space(char c);

typedef struct ViewBlock ViewBlock;
typedef struct TextShadow TextShadow;

// tier-2: view-pool, rebuilt each relayout
struct FontProp {
    char* family;  // font family name
    float font_size;  // font size in pixels, scaled by pixel_ratio
    CssEnum font_style;
    CssEnum font_weight;
    CssEnum font_variant;  // CSS font-variant (normal, small-caps)
    CssEnum font_kerning;  // CSS font-kerning (auto/normal/none, 0 = auto)
    CssEnum text_deco; // CSS text decoration
    int16_t font_weight_numeric;  // CSS numeric weight 100-900 (0 = not set, use font_weight keyword)
    float letter_spacing;  // letter spacing in pixels (default 0)
    float word_spacing;  // word spacing in pixels (default 0)
    bool font_size_from_medium;  // true if font_size originates from the CSS 'medium' keyword (initial value)
    // derived font properties
    float space_width;  // width of a space character of the current font
    float ascender;    // font ascender in pixels
    float descender;   // font descender in pixels
    float font_height; // font height in pixels
    bool has_kerning;  // whether the font has kerning
    struct FontHandle* font_handle; // unified font handle (populated by setup_font)
    bool owns_font_handle; // true when this FontProp owns a retained font_handle ref
    TextShadow* text_shadow;  // CSS text-shadow (linked list for multiple shadows)
    CssEnum text_deco_style;          // CSS text-decoration-style: solid, dashed, dotted, wavy, double
    Color text_deco_color;            // CSS text-decoration-color (default: {0} = use currentColor)
    float text_deco_thickness;        // CSS text-decoration-thickness in px (0 = auto from font metrics)
    float text_underline_offset;      // CSS text-underline-offset in px (0 = auto)
};

// build a FontStyleDesc from a FontProp (for font_load_glyph fallback resolution)
inline FontStyleDesc font_style_desc_from_prop(const FontProp* fp) {
    FontStyleDesc sd = {};
    sd.family  = fp->family;
    sd.size_px = fp->font_size;

    // Use numeric weight if set, otherwise map from CssEnum keyword
    if (fp->font_weight_numeric > 0) {
        sd.weight = (FontWeight)fp->font_weight_numeric;
    } else {
        sd.weight  = (fp->font_weight == CSS_VALUE_BOLD || fp->font_weight == CSS_VALUE_BOLDER)
                     ? FONT_WEIGHT_BOLD : (fp->font_weight == CSS_VALUE_LIGHTER)
                     ? FONT_WEIGHT_LIGHT : FONT_WEIGHT_NORMAL;
    }

    sd.slant   = (fp->font_style == CSS_VALUE_ITALIC)  ? FONT_SLANT_ITALIC
               : (fp->font_style == CSS_VALUE_OBLIQUE) ? FONT_SLANT_OBLIQUE
               : FONT_SLANT_NORMAL;
    return sd;
}

// tier-2: view-pool, rebuilt each relayout
struct GridItemProp {
    // Grid item properties (following flex pattern)
    int grid_row_start;          // Grid row start line
    int grid_row_end;            // Grid row end line
    int grid_column_start;       // Grid column start line
    int grid_column_end;         // Grid column end line
    char* grid_area;             // Named grid area
    // Named line references — resolved to integers before placement
    const char* grid_column_start_name;
    const char* grid_column_end_name;
    const char* grid_row_start_name;
    const char* grid_row_end_name;
    int justify_self;            // Item-specific justify alignment (CSS_VALUE_*)
    int align_self_grid;         // Item-specific align alignment for grid (CSS_VALUE_*)
    int order;                   // CSS order property (affects placement order)

    // tier-3 scratch, valid within layout pass: grid placement, track geometry,
    // and measurement are shared by distinct multipass entry points that do not
    // retain one common GridItemBox owner yet.
    // Grid item computed properties
    int computed_grid_row_start;
    int computed_grid_row_end;
    int computed_grid_column_start;
    int computed_grid_column_end;

    // Track area dimensions (computed during positioning phase, used for alignment)
    float track_area_width;      // Width of the track area this item spans
    float track_area_height;     // Height of the track area this item spans
    float track_base_x;          // Base X position of track area (before alignment)
    float track_base_y;          // Base Y position of track area (before alignment)

    // Grid item flags
    bool has_explicit_grid_row_start;
    bool has_explicit_grid_row_end;
    bool has_explicit_grid_column_start;
    bool has_explicit_grid_column_end;
    bool is_grid_auto_placed;
    bool grid_row_start_is_span;     // True if grid_row_start negative value means "span N", not "negative line"
    bool grid_row_end_is_span;       // True if grid_row_end negative value means "span N", not "negative line"
    bool grid_column_start_is_span;  // True if grid_column_start negative value means "span N", not "negative line"
    bool grid_column_end_is_span;    // True if grid_column_end negative value means "span N", not "negative line"

    // Measured dimensions (from multipass measurement phase)
    float measured_width;          // Content-based width (from measure pass)
    float measured_height;         // Content-based height (from measure pass)
    float measured_min_width;      // Minimum content width (longest word)
    float measured_max_width;      // Maximum content width (no wrapping)
    float measured_min_height;     // Minimum content height
    float measured_max_height;     // Maximum content height
    bool has_measured_size;      // Whether measurement pass has been done
};

// Intrinsic size type (shared by flex and grid layout)
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    float min_content;  // Minimum content width (longest word/element)
    float max_content;  // Maximum content width (no wrapping)
    // CSS Text 3 §5.2: For inline content with forced line breaks (pre/pre-wrap newlines),
    // the content spans multiple lines. These fields allow the parent's inline run
    // accumulation to split at forced break points rather than summing all lines.
    float first_line_max = -1;  // width before first forced break (-1 = no forced break)
    float last_line_max = -1;   // width after last forced break (-1 = no forced break)
    bool has_forced_break = false;
    // Replaced form controls populate min_content/max_content from
    // FormControlProp::intrinsic_width which is already a border-box value.
    // When this flag is set, the common pad/border addition pass at the bottom
    // of measure_element_intrinsic_widths() must be skipped to avoid double counting.
    bool replaced_includes_pad_border = false;
    // Asymmetric variant: replaced element's min-content represents the natural
    // (text-only) width — heavy author CSS padding should NOT be added to it for
    // shrink-to-fit purposes (matches Chrome behavior for appearance:none <select>).
    // max-content still gets pad+border added so it represents the full border-box.
    bool replaced_min_excludes_pad_border = false;
} IntrinsicSizes;

// FlexItemProp definition (needed by flex.hpp)
// tier-2: view-pool, rebuilt each relayout
typedef struct FlexItemProp {
    float flex_basis;  // -1 for auto
    float flex_grow;
    float flex_shrink;
    CssEnum align_self;  // AlignType
    int order;
    float aspect_ratio;
    float baseline_offset;

    // tier-3 scratch, valid within layout pass: intrinsic measurement and the
    // flex sizing/alignment passes are separate entry points, so this state must
    // remain reachable until FlexContainerLayout owns a stable item-keyed map.
    // Intrinsic sizing cache (computed during measurement phase)
    IntrinsicSizes intrinsic_width;   // min_content and max_content widths
    IntrinsicSizes intrinsic_height;  // min_content and max_content heights

    // Resolved constraints (computed from BlockProp given_min/max values)
    float resolved_min_width;    // Resolved min-width (including auto = min-content)
    float resolved_max_width;    // Resolved max-width (FLT_MAX if none)
    float resolved_min_height;   // Resolved min-height (including auto = min-content)
    float resolved_max_height;   // Resolved max-height (FLT_MAX if none)

    // Hypothetical sizes (computed during flex layout Phase 4.5)
    // These are the item's cross-axis sizes before alignment stretching
    float hypothetical_cross_size;        // Inner cross size (content box)
    float hypothetical_outer_cross_size;  // Outer cross size (with margins)

    // Flags for percentage values and measurement state
    uint8_t flex_basis_is_percent : 1;
    uint8_t is_margin_top_auto : 1;
    uint8_t is_margin_right_auto : 1;
    uint8_t is_margin_bottom_auto : 1;
    uint8_t is_margin_left_auto : 1;
    uint8_t has_intrinsic_width : 1;   // True if intrinsic widths calculated
    uint8_t has_intrinsic_height : 1;  // True if intrinsic heights calculated
    uint8_t needs_measurement : 1;     // True if content needs measuring
    uint8_t has_explicit_width : 1;    // True if width explicitly set in CSS
    uint8_t has_explicit_height : 1;   // True if height explicitly set in CSS
    uint8_t main_size_from_flex : 1;   // True if parent flex grew/shrank this item's main-axis size
} FlexItemProp;

// tier-2: view-pool, rebuilt each relayout
struct InlineProp {
    CssEnum cursor;
    CssEnum caret_shape;
    Color color;
    Color accent_color;
    bool has_color;
    bool has_accent_color;
    Color svg_fill_color;
    Color svg_stroke_color;
    CssEnum vertical_align;
    float vertical_align_offset;  // length/percentage vertical-align offset (px), positive = raise
    float opacity;  // CSS opacity value (0.0 to 1.0)
    int visibility;  // Visibility
    CssEnum mix_blend_mode;  // CSS mix-blend-mode (CSS_VALUE_NORMAL default, CSS_VALUE_MULTIPLY, etc.)
    bool has_svg_fill;
    bool svg_fill_none;
    bool has_svg_stroke;
    bool svg_stroke_none;
    bool has_svg_stroke_width;
    float svg_stroke_width;
};

// tier-2: view-pool, rebuilt each relayout
typedef struct Spacing {
    struct { float top, right, bottom, left; };  // for margin, padding, border
    int64_t top_specificity, right_specificity, bottom_specificity, left_specificity;
} Spacing;

// tier-2: view-pool, rebuilt each relayout
typedef struct Margin : Spacing {
    CssEnum top_type, right_type, bottom_type, left_type;   // for CSS enum values, like 'auto'
} Margin;

// tier-2: view-pool, rebuilt each relayout
typedef struct Corner {
    struct { float top_left, top_right, bottom_right, bottom_left; };  // horizontal border radius
    struct { float top_left_y, top_right_y, bottom_right_y, bottom_left_y; };  // vertical border radius
    int64_t tl_specificity, tr_specificity, br_specificity, bl_specificity;
    bool tl_percent, tr_percent, br_percent, bl_percent;  // true if horizontal radius is a percentage (0-100)
    bool tl_percent_y, tr_percent_y, br_percent_y, bl_percent_y;  // true if vertical radius is a percentage (0-100)
} Corner;

// Gradient types for CSS background and border-image gradients
typedef enum {
    GRADIENT_NONE = 0,
    GRADIENT_LINEAR,
    GRADIENT_RADIAL,
    GRADIENT_CONIC
} GradientType;

typedef struct LinearGradient LinearGradient;

// tier-2: view-pool, rebuilt each relayout
typedef struct {
    Spacing width;
    CssEnum top_style, right_style, bottom_style, left_style;
    int64_t top_style_specificity, right_style_specificity, bottom_style_specificity, left_style_specificity;
    Color top_color, right_color, bottom_color, left_color;
    int64_t top_color_specificity, right_color_specificity, bottom_color_specificity, left_color_specificity;
    Corner radius;
    GradientType border_image_type;
    LinearGradient* border_image_linear_gradient;
    float border_image_width;
    bool has_border_image_width;
    CssEnum border_image_repeat;
} BorderProp;

// Color stop for gradients
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    Color color;
    float position;  // 0.0 to 1.0, or -1 for auto
} GradientStop;

// Linear gradient data
// tier-2: view-pool, rebuilt each relayout
struct LinearGradient {
    float angle;           // in degrees, 0 = to top, 90 = to right
    GradientStop* stops;   // array of color stops
    int stop_count;
    uint8_t is_repeating : 1;  // true for repeating-linear-gradient
    uint8_t stops_in_px : 1;   // true if stop positions are in px (not fractions)
};

// Radial gradient shape
typedef enum {
    RADIAL_SHAPE_ELLIPSE = 0,
    RADIAL_SHAPE_CIRCLE
} RadialShape;

// Radial gradient size keyword
typedef enum {
    RADIAL_SIZE_FARTHEST_CORNER = 0,  // default
    RADIAL_SIZE_CLOSEST_SIDE,
    RADIAL_SIZE_CLOSEST_CORNER,
    RADIAL_SIZE_FARTHEST_SIDE
} RadialSize;

// Radial gradient data
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    RadialShape shape;     // circle or ellipse
    RadialSize size;       // size keyword
    float cx, cy;          // center position (0.0-1.0 relative to box, default 0.5,0.5)
    bool cx_set, cy_set;   // whether center was explicitly set
    GradientStop* stops;   // array of color stops
    int stop_count;
} RadialGradient;

// Conic gradient data
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    float from_angle;      // starting angle in degrees (default 0)
    float cx, cy;          // center position (0.0-1.0 relative to box, default 0.5,0.5)
    bool cx_set, cy_set;   // whether center was explicitly set
    GradientStop* stops;   // array of color stops
    int stop_count;
} ConicGradient;

// tier-2: view-pool, rebuilt each relayout
typedef struct {
    Color color; // background color
    char* image; // background image path
    char* repeat; // repeat behavior (legacy string, use repeat_x/repeat_y when set)
    char* position; // positioning of background image (legacy string)
    // Background-size: auto | <length> | <percentage> | cover | contain
    CssEnum bg_size_type;   // CSS_VALUE_AUTO (default), CSS_VALUE_COVER, CSS_VALUE_CONTAIN, or 0 for explicit
    float bg_size_width;    // explicit width (px or %)
    float bg_size_height;   // explicit height (px or %)
    uint8_t bg_size_width_is_percent : 1;
    uint8_t bg_size_height_is_percent : 1;
    uint8_t bg_size_width_auto : 1;   // true if width component is 'auto'
    uint8_t bg_size_height_auto : 1;  // true if height component is 'auto'
    // Background-position: <length> | <percentage> | left | center | right | top | bottom
    float bg_position_x;   // x offset (px or %)
    float bg_position_y;   // y offset (px or %)
    uint8_t bg_position_x_is_percent : 1;
    uint8_t bg_position_y_is_percent : 1;
    uint8_t bg_position_set : 1;  // true if position was explicitly set
    // Background-repeat: repeat | no-repeat | round | space (per axis)
    CssEnum bg_repeat_x;   // CSS_VALUE_REPEAT (default), CSS_VALUE_NO_REPEAT, CSS_VALUE_ROUND, CSS_VALUE_SPACE
    CssEnum bg_repeat_y;
    // Background attachment, origin, and clip (CSS Backgrounds Level 3)
    CssEnum bg_attachment;  // CSS_VALUE_SCROLL (default) | CSS_VALUE_FIXED | CSS_VALUE_LOCAL
    CssEnum bg_origin;      // CSS_VALUE_PADDING_BOX (default) | CSS_VALUE_BORDER_BOX | CSS_VALUE_CONTENT_BOX
    CssEnum bg_clip;        // CSS_VALUE_BORDER_BOX (default) | CSS_VALUE_PADDING_BOX | CSS_VALUE_CONTENT_BOX
    // Gradient support
    GradientType gradient_type;
    LinearGradient* linear_gradient;
    RadialGradient* radial_gradient;
    ConicGradient* conic_gradient;
    // Multiple gradient layers (for stacked gradients)
    RadialGradient** radial_layers;  // array of additional radial gradients
    int radial_layer_count;
    LinearGradient** linear_layers;  // array of additional linear gradients
    int linear_layer_count;
    CssEnum blend_mode;  // CSS background-blend-mode (CSS_VALUE_NORMAL default, CSS_VALUE_MULTIPLY, etc.)
} BackgroundProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct MaskProp {
    bool has_radial_gradient;
    float cx, cy;           // 0.0-1.0 relative to border box
    float radius;           // CSS px unless radius_is_percent is true
    bool radius_is_percent;
} MaskProp;

/**
 * BoxShadow - CSS box-shadow property
 * Supports multiple shadows via linked list (shadows render bottom-to-top)
 * Syntax: box-shadow: [inset] <offset-x> <offset-y> [blur-radius] [spread-radius] [color]
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct BoxShadow {
    float offset_x;              // Horizontal offset (positive = right)
    float offset_y;              // Vertical offset (positive = down)
    float blur_radius;           // Blur amount (0 = sharp edge)
    float spread_radius;         // Spread amount (positive = expand, negative = contract)
    Color color;                 // Shadow color (default: currentColor)
    bool inset;                  // True for inset shadow (inside the box)
    struct BoxShadow* next;      // Next shadow in list (for multiple shadows)
} BoxShadow;

/**
 * OutlineProp - CSS outline property (CSS UI Level 3)
 * Outlines are drawn outside the border-box and don't affect layout.
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct OutlineProp {
    float width;                 // outline-width in pixels
    float offset;                // outline-offset in pixels (can be negative)
    CssEnum style;               // outline-style: solid, dashed, dotted, double, etc.
    Color color;                 // outline-color
} OutlineProp;

/**
 * TextShadow - CSS text-shadow property
 * Supports multiple shadows via linked list
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct TextShadow {
    float offset_x;
    float offset_y;
    float blur_radius;
    Color color;
    struct TextShadow* next;
} TextShadow;

/**
 * TransformFunction - Individual CSS transform function
 * Forms a linked list for transform: translate() rotate() scale() etc.
 */
typedef enum TransformFunctionType {
    TRANSFORM_NONE = 0,
    // 2D Transforms
    TRANSFORM_TRANSLATE,        // translate(x, y) or translate(x)
    TRANSFORM_TRANSLATEX,       // translateX(x)
    TRANSFORM_TRANSLATEY,       // translateY(y)
    TRANSFORM_SCALE,            // scale(x, y) or scale(s)
    TRANSFORM_SCALEX,           // scaleX(x)
    TRANSFORM_SCALEY,           // scaleY(y)
    TRANSFORM_ROTATE,           // rotate(angle)
    TRANSFORM_SKEW,             // skew(x-angle, y-angle)
    TRANSFORM_SKEWX,            // skewX(angle)
    TRANSFORM_SKEWY,            // skewY(angle)
    TRANSFORM_MATRIX,           // matrix(a, b, c, d, e, f)
    // 3D Transforms
    TRANSFORM_TRANSLATE3D,      // translate3d(x, y, z)
    TRANSFORM_TRANSLATEZ,       // translateZ(z)
    TRANSFORM_SCALE3D,          // scale3d(x, y, z)
    TRANSFORM_SCALEZ,           // scaleZ(z)
    TRANSFORM_ROTATEX,          // rotateX(angle)
    TRANSFORM_ROTATEY,          // rotateY(angle)
    TRANSFORM_ROTATEZ,          // rotateZ(angle) - same as rotate()
    TRANSFORM_ROTATE3D,         // rotate3d(x, y, z, angle)
    TRANSFORM_PERSPECTIVE,      // perspective(d)
    TRANSFORM_MATRIX3D,         // matrix3d(16 values)
} TransformFunctionType;

// tier-2: view-pool, rebuilt each relayout
typedef struct TransformFunction {
    TransformFunctionType type;
    union {
        struct { float x, y; } translate;           // translate, translateX, translateY
        struct { float x, y, z; } translate3d;      // translate3d, translateZ
        struct { float x, y; } scale;               // scale, scaleX, scaleY
        struct { float x, y, z; } scale3d;          // scale3d, scaleZ
        float angle;                                 // rotate, skewX, skewY
        struct { float x, y; } skew;                // skew
        struct { float a, b, c, d, e, f; } matrix;  // matrix (2D)
        struct { float x, y, z; float angle; } rotate3d;  // rotate3d
        float perspective;                           // perspective
        float matrix3d[16];                          // matrix3d (4x4)
    } params;
    // Percentage values for translate (to be resolved against element's own dimensions)
    // CSS transform translate percentages are relative to element's own width/height
    float translate_x_percent;  // NaN if not percentage, otherwise percentage value (e.g. -50 for -50%)
    float translate_y_percent;  // NaN if not percentage, otherwise percentage value
    struct TransformFunction* next;                  // Next transform in chain
} TransformFunction;

/**
 * TransformProp - CSS transform properties
 * Contains transform origin and list of transform functions
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct TransformProp {
    TransformFunction* functions;    // Linked list of transform functions (applied in order)
    float origin_x;                  // transform-origin X (default: 50%)
    float origin_y;                  // transform-origin Y (default: 50%)
    float origin_z;                  // transform-origin Z (default: 0)
    bool origin_x_percent;           // true if origin_x is percentage
    bool origin_y_percent;           // true if origin_y is percentage
    float perspective;               // perspective distance (from parent)
    float perspective_origin_x;      // perspective-origin X (default: 50%)
    float perspective_origin_y;      // perspective-origin Y (default: 50%)
    CssEnum transform_style;         // flat or preserve-3d
    CssEnum backface_visibility;     // visible or hidden
} TransformProp;

/**
 * FilterFunction - Individual CSS filter function
 * Forms a linked list for filter: blur() brightness() grayscale() etc.
 */
typedef enum FilterFunctionType {
    FILTER_NONE = 0,
    FILTER_BLUR,              // blur(<length>)
    FILTER_BRIGHTNESS,        // brightness(<number>|<percentage>)
    FILTER_CONTRAST,          // contrast(<number>|<percentage>)
    FILTER_GRAYSCALE,         // grayscale(<number>|<percentage>)
    FILTER_HUE_ROTATE,        // hue-rotate(<angle>)
    FILTER_INVERT,            // invert(<number>|<percentage>)
    FILTER_OPACITY,           // opacity(<number>|<percentage>)
    FILTER_SATURATE,          // saturate(<number>|<percentage>)
    FILTER_SEPIA,             // sepia(<number>|<percentage>)
    FILTER_DROP_SHADOW,       // drop-shadow(<offset-x> <offset-y> <blur-radius>? <color>?)
    FILTER_URL,               // url(<string>) - SVG filter reference
} FilterFunctionType;

// tier-2: view-pool, rebuilt each relayout
typedef struct FilterFunction {
    FilterFunctionType type;
    union {
        float blur_radius;           // blur() - in pixels
        float amount;                // brightness, contrast, grayscale, invert, opacity, saturate, sepia (0-1 or 1 = 100%)
        float angle;                 // hue-rotate - in radians
        struct {
            float offset_x, offset_y;
            float blur_radius;
            Color color;
        } drop_shadow;
        const char* url;             // url() - SVG filter reference
    } params;
    struct FilterFunction* next;     // Next filter in chain
} FilterFunction;

/**
 * FilterProp - CSS filter properties
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct FilterProp {
    FilterFunction* functions;       // Linked list of filter functions (applied in order)
} FilterProp;

/**
 * MultiColumnProp - CSS Multi-column layout properties
 * column-count, column-width, column-gap, column-rule, column-span, column-fill
 */
typedef enum ColumnSpan {
    COLUMN_SPAN_NONE = 0,    // Default: element stays in its column
    COLUMN_SPAN_ALL,         // Element spans all columns
} ColumnSpan;

typedef enum ColumnFill {
    COLUMN_FILL_BALANCE = 0, // Default: balance content across columns
    COLUMN_FILL_AUTO,        // Fill columns sequentially
} ColumnFill;

typedef enum ColumnWrap {
    COLUMN_WRAP_AUTO = 0,    // Wrap when block-size is unconstrained
    COLUMN_WRAP_NOWRAP,      // Overflow after the last column
    COLUMN_WRAP_WRAP,        // Continue columns in the next row
} ColumnWrap;

// tier-2: view-pool, rebuilt each relayout
typedef struct MultiColumnProp {
    // Column sizing
    int column_count;            // Number of columns (0 = auto)
    float column_width;          // Ideal column width (0 = auto)
    float column_height;         // Fragmentainer height (0 = auto)
    float column_gap;            // Gap between columns (default: 1em)
    bool column_gap_is_normal;   // Use normal (1em) gap

    // Column rule (divider between columns)
    float rule_width;            // Rule width in pixels
    CssEnum rule_style;          // solid, dotted, dashed, etc.
    Color rule_color;            // Rule color

    // Column behavior
    ColumnSpan span;             // column-span: none | all
    ColumnFill fill;             // column-fill: balance | auto
    ColumnWrap wrap;             // column-wrap: nowrap | wrap

    // Computed values (set during layout)
    int computed_column_count;   // Actual number of columns after layout
    int computed_used_column_count; // Columns that received content in layout
    float computed_column_width; // Actual column width after layout
} MultiColumnProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct BoundaryProp {
    Margin margin;
    Spacing padding;
    BorderProp* border;
    BackgroundProp* background;
    MaskProp* mask;
    BoxShadow* box_shadow;       // Linked list of box shadows
    OutlineProp* outline;        // CSS outline property (outside border-box)
    float collapsed_through_mb;  // CSS 2.1 §8.3.1: margin transferred from descendants via
                                 // parent-child bottom margin collapse (the inflated portion)
    bool has_clearance;              // CSS 2.1 §9.5.2: true if clearance was applied to this block.
                                     // Prevents first child's parent-child margin collapse from
                                     // double-adjusting position (clearance already accounts for
                                     // the child's margin contribution).
    bool clearance_in_margin_chain;  // CSS 2.1 §8.3.1: true if this element's bottom margin
                                     // includes contribution from a self-collapsing element
                                     // with clearance. Such margins must NOT collapse with
                                     // the parent block's bottom margin.
    // CSS 2.1 §8.3.1: Chain tracking for multi-way margin collapse.
    // When multiple margins collapse (especially through self-collapsing elements),
    // preserving the max positive and most negative components is required for
    // correct mixed-sign collapse. Without this, intermediate scalar results
    // lose negative-margin information (e.g., collapse(+16,-16)=0 then
    // collapse(0,+16)=16 instead of the correct 3-way result of 0).
    float margin_chain_positive;  // max positive margin in the pending collapse chain
    float margin_chain_negative;  // most negative margin in the pending collapse chain
} BoundaryProp;

// Vector path segment for PDF/SVG path rendering
// Stores pre-transformed coordinates ready for ThorVG rendering
// tier-2: view-pool, rebuilt each relayout
typedef struct VectorPathSegment {
    enum { VPATH_MOVETO, VPATH_LINETO, VPATH_CURVETO, VPATH_CLOSE } type;
    float x, y;                     // End point
    float x1, y1, x2, y2;           // Control points (for CURVETO)
    struct VectorPathSegment* next;
} VectorPathSegment;

// Vector path property for complex path rendering
// tier-2: view-pool, rebuilt each relayout
typedef struct VectorPathProp {
    VectorPathSegment* segments;    // Linked list of path segments
    Color stroke_color;             // Stroke color
    Color fill_color;               // Fill color (if filled)
    float stroke_width;             // Stroke width
    bool has_stroke;                // Whether to stroke
    bool has_fill;                  // Whether to fill
    float* dash_pattern;            // Dash pattern array (NULL for solid)
    int dash_pattern_length;        // Length of dash pattern
} VectorPathProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct PositionProp {
    CssEnum position;     // static, relative, absolute, fixed, sticky
    float top, right, bottom, left;  // offset values in pixels
    float top_percent, right_percent, bottom_percent, left_percent;  // raw percentage if percentage value (NaN if not percentage)
    int z_index;            // stacking order
    int custom_layout_z_index; // layout(fn) pass-scoped stacking overlay
    bool has_top, has_right, has_bottom, has_left;  // which offsets are set
    bool has_custom_layout_z_index;
    CssEnum clear;        // clear property for floats
    CssEnum float_prop;   // float property (left, right, none)
    ViewBlock* first_abs_child;   // first child absolute/fixed positioned view
    ViewBlock* last_abs_child;    // last child absolute/fixed positioned view
    ViewBlock* next_abs_sibling;    // next sibling absolute/fixed positioned view
    bool static_x_needs_parent_offset;  // flex static x was computed in parent-local coords
    bool static_y_needs_parent_offset;  // flex static y was computed in parent-local coords
    bool has_static_parent_offset_x;    // static x captured parent-to-containing-block offset
    bool has_static_parent_offset_y;    // static y captured parent-to-containing-block offset
    float static_parent_offset_x;       // parent-to-containing-block offset when static x was set
    float static_parent_offset_y;       // parent-to-containing-block offset when static y was set
} PositionProp;

/**
 * MarkerProp - Stores list marker (bullet) properties
 * Used for ::marker pseudo-element rendering with fixed width and vector graphics
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct MarkerProp {
    CssEnum marker_type;     // CSS_VALUE_DISC, CSS_VALUE_CIRCLE, CSS_VALUE_SQUARE, CSS_VALUE_DECIMAL, etc.
    float width;             // Fixed marker width (typically ~1.4em = 22px at 16px font)
    float height;            // Used marker box height; image markers can exceed line-height
    float bullet_size;       // Size of the bullet shape (typically ~0.35em = 5-6px)
    char* text_content;      // Text content for numbered markers (decimal, roman, alpha)
    char* image_url;         // list-style-image URL (data URI or external URL)
    ImageSurface* loaded_image; // cached loaded image for layout and render
    bool is_outside;         // true = outside position (rendered in margin area, no inline advance)
    bool reserves_first_line; // outside marker has no parent list gutter to occupy
} MarkerProp;

/**
 * PseudoContentProp - Stores dynamically created ::before and ::after pseudo-elements
 *
 * Instead of storing content strings and layout bounds, we create actual DomElement
 * and DomText nodes for pseudo-elements. This allows reusing the existing layout
 * infrastructure for text and inline content.
 *
 * The pseudo DomElements are created during style cascade when 'content' property
 * is resolved, and laid out as part of normal block layout flow.
 */

// Content value types for pseudo-elements (CSS 2.1 Section 12.2)
enum ContentType {
    CONTENT_TYPE_NONE = 0,      // no content
    CONTENT_TYPE_STRING = 1,     // string literal
    CONTENT_TYPE_URI = 2,        // url()
    CONTENT_TYPE_COUNTER = 3,    // counter()
    CONTENT_TYPE_COUNTERS = 4,   // counters()
    CONTENT_TYPE_ATTR = 5,       // attr()
    CONTENT_TYPE_OPEN_QUOTE = 6,
    CONTENT_TYPE_CLOSE_QUOTE = 7
};

// tier-2: view-pool, rebuilt each relayout
typedef struct PseudoContentProp {
    DomElement* before;    // ::before pseudo-element (NULL if none)
    DomElement* after;     // ::after pseudo-element (NULL if none)
    DomElement* marker;    // ::marker pseudo-element (NULL if none)

    // Content value storage for generation
    char* before_content;         // Parsed content string/template (or counter name for counters)
    char* after_content;
    char* before_separator;       // Separator for counters() function
    char* after_separator;
    uint32_t before_counter_style;  // CSS enum value for counter style
    uint32_t after_counter_style;
    uint8_t before_content_type;  // ContentType enum
    uint8_t after_content_type;
    bool before_generated;         // True if before element created
    bool after_generated;          // True if after element created
    bool marker_generated;         // True if marker element created
} PseudoContentProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct BlockProp {
    CssEnum text_align;
    CssEnum text_align_last;  // CSS text-align-last (auto, start, end, left, right, center, justify)
    CssEnum align_content;    // CSS Box Alignment align-content for block containers
    bool legacy_align_center_blocks;  // HTML align=center compatibility: center block/table descendants
    CssEnum legacy_block_align;  // HTML align compatibility for block/table descendants
    CssEnum direction;  // CSS_VALUE_LTR or CSS_VALUE_RTL (CSS 2.1 §9.2.1)
    CssEnum text_transform;  // CSS_VALUE_NONE, CSS_VALUE_UPPERCASE, CSS_VALUE_LOWERCASE, CSS_VALUE_CAPITALIZE
    const CssValue* line_height;
    float text_indent;  // can be negative
    float text_indent_percent;  // NaN if not percentage, else raw percentage value for deferred resolution
    const CssValue* text_indent_calc;  // non-null if text-indent is calc() with percentage, deferred to layout
    float given_min_width, given_max_width;  // non-negative
    float given_min_height, given_max_height;  // non-negative
    CssEnum list_style_type;
    CssEnum list_style_position;  // inside, outside
    char* list_style_image;         // URL or none
    char* list_style_type_string;   // custom string marker (CSS Lists 3 §4.1)
    char* counter_reset;            // counter names and values
    char* counter_increment;        // counter names and values
    char* counter_set;              // counter names and values (CSS Lists 3)
    CssEnum box_sizing;  // CSS_VALUE_CONTENT_BOX or CSS_VALUE_BORDER_BOX
    CssEnum box_decoration_break;  // CSS_VALUE_SLICE (default) | CSS_VALUE_CLONE
    CssEnum white_space;  // CSS_VALUE_NORMAL, CSS_VALUE_NOWRAP, CSS_VALUE_PRE, etc.
    CssEnum text_wrap_style;  // CSS Text 4 text-wrap-style
    CssEnum word_break;   // CSS_VALUE_NORMAL, CSS_VALUE_BREAK_ALL, CSS_VALUE_KEEP_ALL
    CssEnum overflow_wrap;  // CSS_VALUE_NORMAL, CSS_VALUE_BREAK_WORD, CSS_VALUE_ANYWHERE
    CssEnum line_break;    // CSS_VALUE_AUTO, CSS_VALUE_LOOSE, CSS_VALUE_NORMAL, CSS_VALUE_STRICT, CSS_VALUE_ANYWHERE
    CssEnum text_spacing_trim;  // CSS Text 4 text-spacing-trim
    CssEnum break_before;  // CSS Fragmentation: auto, column, page, always
    CssEnum break_after;   // CSS Fragmentation: auto, column, page, always
    int orphans;           // CSS Fragmentation: minimum lines before a break
    int widows;            // CSS Fragmentation: minimum lines after a break
    int tab_size;           // CSS tab-size (number of spaces, default 8)
    uint8_t margin_trim;     // bitmask: MARGIN_TRIM_BLOCK_START|END|INLINE_START|END
    uint8_t text_box_trim;   // bitmask: TEXT_BOX_TRIM_START|END (CSS Inline Level 3)
    uint8_t text_box_trim_applied; // bitmask of start/end trim actually applied during layout
    float text_box_trim_start_amount;
    float text_box_trim_end_amount;
    CssEnum text_box_over_edge;  // CSS Inline 3 text-box-edge over metric (CSS_VALUE_TEXT, CSS_VALUE_CAP, CSS_VALUE_EX, etc.)
    CssEnum text_box_under_edge; // CSS Inline 3 text-box-edge under metric (CSS_VALUE_TEXT, CSS_VALUE_ALPHABETIC, etc.)
    float given_width, given_height;  // CSS specified width/height values
    CssEnum given_width_type;
    CssEnum given_height_type;
    float given_width_percent;  // Raw percentage if width: X% (NaN if not percentage)
    float given_height_percent; // Raw percentage if height: X% (NaN if not percentage)
    float contain_intrinsic_width;
    float contain_intrinsic_height;
    bool contain_size;
    float given_min_width_percent;   // Raw percentage if min-width: X% (NaN if not percentage)
    float given_max_width_percent;   // Raw percentage if max-width: X% (NaN if not percentage)
    float given_min_height_percent;  // Raw percentage if min-height: X% (NaN if not percentage)
    float given_max_height_percent;  // Raw percentage if max-height: X% (NaN if not percentage)
    // CSS Inline 3 §5: line box metrics for text-box-trim.
    // Stored during line_break() to capture inline descendants' contributions.
    float first_line_max_ascender;
    float first_line_max_descender;
    float last_line_max_ascender;
    float last_line_max_descender;
    // Baseline positions (distance from border-box top to baseline).
    // Used for flex/inline-block baseline alignment (CSS 2.1 §10.8.1).
    float first_line_baseline;  // first line box baseline (for flex baseline)
    // Transient layout state: nonzero when BFC float avoidance shifted this block down.
    // Inline placement uses it to discard stale line cursors from floats above.
    float bfc_float_avoidance_shift_y;
    CssEnum text_overflow;  // CSS_VALUE_CLIP (default 0) | CSS_VALUE_ELLIPSIS
    int line_clamp;         // -webkit-line-clamp: max visible lines (0 = no clamp)
    bool line_clamp_inherited; // transient: this block is consuming an ancestor clamp
    bool line_clamped;      // transient: layout hit this block's active line clamp
    float line_clamp_advance_y; // transient: content advance at clamp boundary
    float line_clamp_last_line_ascender;
    float line_clamp_last_line_max_ascender;
    float line_clamp_last_line_max_descender;
} BlockProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct FontBox {
    FontProp *style;  // current font style
    struct FontHandle* font_handle; // unified font handle (opaque, ref-counted)
    float current_font_size;  // font size of current element
} FontBox;

// tier-2: view-pool, rebuilt each relayout
typedef struct TextRect {
    float x, y, width, height;
    float hanging_trim;  // preserved hanging space width excluded from line advance, not from CSSOM rects
    int start_index, length;  // start and length of the text in the style node
    int line_number;  // block-local line index assigned when this rect enters inline flow
    bool has_trailing_hyphen;  // CSS Text 3 §5.2: soft hyphen (U+00AD) broke here; render visible '-' at end
    bool has_trailing_ellipsis; // -webkit-line-clamp: render '…' after text on this rect
    TextRect* next;
} TextRect;

// tier-2: view-pool, rebuilt each relayout
typedef struct ViewText : DomText {
    // TextRect *rect;  // first text rect
    // FontProp *font;  // font for this text
    // Color color;     // text color (for PDF text fill color)
} ViewText;

/**
 * ViewMarker - Represents a list marker (bullet or number)
 * Fixed-width element that renders bullets using vector graphics
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct ViewMarker : DomElement {
} ViewMarker;

// tier-2: view-pool, rebuilt each relayout
struct ViewElement : DomElement {
    // exclude those skipped text nodes
    View* first_placed_child() {
        View* child = (View*)first_child;
        while (child) {
            if (child->view_type) return child;
            child = (View*)child->next_sibling;
        }
        return nullptr;
    }

    // exclude those skipped text nodes
    View* last_placed_child() {
        View* last_placed = nullptr;
        View* child = (View*)first_child;
        while (child) {
            if (child->view_type) { last_placed = child; }
            child = (View*)child->next_sibling;
        }
        return last_placed;
    }
};

typedef ViewElement ViewSpan;

// tier-2: view-pool, rebuilt each relayout
typedef struct {
    // Fast-read pointer to centralized state owner. Writers must use state_store APIs.
    DocState* state_ref;

    float v_scroll_position, h_scroll_position;
    float v_max_scroll, h_max_scroll;
    float v_handle_y, v_handle_height;
    float h_handle_x, h_handle_width;
    void reset();
} ScrollPane;

// tier-2: view-pool, rebuilt each relayout
typedef struct ScrollProp {
    CssEnum overflow_x, overflow_y;
    ScrollPane* pane;
    bool has_hz_overflow, has_vt_overflow;
    bool has_hz_scroll, has_vt_scroll;

    Bound clip; // clipping rect, relative to the block border box
    bool has_clip;
} ScrollProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct FlexProp {
    // CSS properties (using int to allow both enum and Lexbor constants)
    int direction;      // FlexDirection or CSS_VALUE_*
    int wrap;           // FlexWrap or CSS_VALUE_*
    int justify;        // JustifyContent or CSS_VALUE_*
    int align_items;    // AlignType or CSS_VALUE_*
    int align_content;  // AlignType or CSS_VALUE_*
    float row_gap;
    float column_gap;
    bool row_gap_is_percent;      // true if row_gap is a percentage
    bool column_gap_is_percent;   // true if column_gap is a percentage
    WritingMode writing_mode;
    TextDirection text_direction;
    // First baseline of this flex container (computed after layout)
    // Used when this container participates in parent's baseline alignment
    int first_baseline;
    bool has_baseline_child;       // true if first line has baseline-aligned items
} FlexProp;

typedef struct GridTrackList GridTrackList;
typedef struct GridArea GridArea;
// tier-2: view-pool, rebuilt each relayout
typedef struct GridProp {
    // Grid alignment properties (using Lexbor CSS constants)
    int justify_content;         // CSS_VALUE_START, etc.
    int align_content;           // CSS_VALUE_START, etc.
    int justify_items;           // CSS_VALUE_STRETCH, etc.
    int align_items;             // CSS_VALUE_STRETCH, etc.
    int grid_auto_flow;          // CSS_VALUE_ROW, CSS_VALUE_COLUMN
    // Grid gap properties
    float row_gap;
    float column_gap;
    bool row_gap_is_percent;
    bool column_gap_is_percent;

    // Grid template properties
    GridTrackList* grid_template_rows;
    GridTrackList* grid_template_columns;
    GridTrackList* grid_template_areas;

    // Grid auto track sizing
    GridTrackList* grid_auto_rows;
    GridTrackList* grid_auto_columns;

    int computed_row_count;
    int computed_column_count;
    // Grid areas
    GridArea* grid_areas;
    int area_count;
    int allocated_areas;

    // Advanced features
    bool is_dense_packing;       // grid-auto-flow: dense
} GridProp;

// tier-2: view-pool, rebuilt each relayout
typedef struct EmbedProp {
    ImageSurface* img;  // image surface
    float content_image_resolution; // CSS image-set() density for intrinsic sizing, 0 means 1x
    DomDocument* doc;   // iframe document
    struct WebViewProp* webview;  // native OS web view (WKWebView/WebView2/WebKitGTK)
    FlexProp* flex;
    GridProp* grid;
    CssEnum object_fit; // CSS_VALUE_FILL (default), CSS_VALUE_CONTAIN, CSS_VALUE_COVER, CSS_VALUE_NONE, CSS_VALUE_SCALE_DOWN
    float object_position_x; // percent when object_position_x_is_percent, otherwise CSS px
    float object_position_y; // percent when object_position_y_is_percent, otherwise CSS px
    struct RdtVideo* video;  // video playback context (NULL for non-video elements)
    ImageSurface* poster;    // poster image for <video> (displayed before playback starts)
    bool object_position_set;
    bool object_position_x_is_percent;
    bool object_position_y_is_percent;
    bool has_controls;       // true if <video controls> attribute present
    bool broken_alt_fallback; // true when an unloaded <img> is rendered as alt text
} EmbedProp;

// tier-2: view-pool, rebuilt each relayout
struct ViewBlock : ViewSpan {
    // float content_width, content_height;  // width and height of the child content including padding
    // BlockProp* blk;  // block specific style properties
    // ScrollProp* scroller;  // handles overflow
    // // block content related properties for flexbox, image, iframe
    // EmbedProp* embed;
    // // positioning properties for CSS positioning
    // PositionProp* position;
    // ViewBlock* last_child;
};

// tier-2: view-pool, rebuilt each relayout
typedef struct TableProp {
    // Table layout algorithm mode
    enum {
        TABLE_LAYOUT_AUTO = 0,    // Content-based width calculation (default)
        TABLE_LAYOUT_FIXED = 1    // Fixed width calculation based on first row/col elements
    } table_layout;

    // Caption positioning
    enum {
        CAPTION_SIDE_TOP = 0,     // Caption appears above the table (default)
        CAPTION_SIDE_BOTTOM = 1   // Caption appears below the table
    } caption_side;

    // Empty cells display
    enum {
        EMPTY_CELLS_SHOW = 0,     // Show borders and backgrounds of empty cells (default)
        EMPTY_CELLS_HIDE = 1      // Hide borders and backgrounds of empty cells
    } empty_cells;

    // Border model and spacing
    float border_spacing_h; // horizontal spacing between columns (px)
    float border_spacing_v; // vertical spacing between rows (px)
    // Fixed layout height distribution
    int fixed_row_height;   // Height per row for table-layout:fixed with explicit height (0=auto)
    // Table's computed font-size for resolving em units in CSS properties like height
    float computed_font_size;  // Set before cell layout, used for height resolution
    // border_collapse=false => separate borders, apply border-spacing gaps
    // border_collapse=true  => collapsed borders, no gaps between cells
    bool border_collapse;
    uint8_t is_annoy_tbody:1;    // whether this element is doubled as an anonymous tbody
    uint8_t is_annoy_tr:1;       // whether this element is doubled as an anonymous tr
    uint8_t is_annoy_td:1;       // whether this element is doubled as an anonymous td
    uint8_t is_annoy_colgroup:1; // whether this element is doubled as an anonymous colgroup

} TableProp;

// Table-specific lightweight subclasses (no additional fields yet)
// These keep table concerns out of the base ViewBlock while preserving layout/render compatibility.

// Forward declarations for table navigation
struct ViewTableRow;
struct ViewTableCell;
struct ViewTableRowGroup;

// tier-2: view-pool, rebuilt each relayout
typedef struct ViewTable : ViewBlock {
    TableProp* table() const { return role_kind() == ROLE_TABLE ? tb : nullptr; }
    // Navigation helpers that respect anonymous box flags (CSS 2.1 Section 17.2.1)

    // Get first logical row (may be in a row group or directly under table if is_annoy_tbody)
    ViewTableRow* first_row();

    // Get first row group (may be the table itself if is_annoy_tbody)
    ViewBlock* first_row_group();

    // Iterate all rows across all row groups
    // Usage: for (auto row = table->first_row(); row; row = table->next_row(row))
    ViewTableRow* next_row(ViewTableRow* current);

    // Get first cell when table acts as its own row (is_annoy_tr)
    // Returns nullptr if table doesn't act as a row
    ViewTableCell* first_direct_cell();

    // Get next cell when table acts as its own row
    ViewTableCell* next_direct_cell(ViewTableCell* current);

    // Check if table acts as its own tbody
    inline bool acts_as_tbody() { return tb && tb->is_annoy_tbody; }

    // Check if table acts as its own row (cells are direct children)
    inline bool acts_as_row() { return tb && tb->is_annoy_tr; }
} ViewTable;

// CSS 2.1 Section 17.2: Row group types for visual ordering
enum TableSectionType {
    TABLE_SECTION_THEAD = 0,   // Header group - renders first
    TABLE_SECTION_TBODY = 1,   // Body group - renders in middle (default)
    TABLE_SECTION_TFOOT = 2    // Footer group - renders last
};

// tier-2: view-pool, rebuilt each relayout
typedef struct ViewTableRowGroup : ViewBlock {
    // NOTE: Do NOT add fields here - views share memory with DomElement!
    // Section type is determined at runtime via get_section_type() method.

    // Get section type from tag/display for visual ordering
    TableSectionType get_section_type() const;

    // Get first row in this group
    ViewTableRow* first_row();

    // Get next row in this group
    ViewTableRow* next_row(ViewTableRow* current);
} ViewTableRowGroup;

// tier-2: view-pool, rebuilt each relayout
typedef struct ViewTableRow : ViewBlock {
    // Minimal metadata may be added later (e.g., computed baseline)

    // Get first cell in this row
    ViewTableCell* first_cell();

    // Get next cell in this row
    ViewTableCell* next_cell(ViewTableCell* current);

    // Get parent row group (or table if row is direct child)
    ViewBlock* parent_row_group();
} ViewTableRow;

// Border-collapse resolved border structure (CSS 2.1 §17.6.2)
// Stores the winning border after conflict resolution between
// cell, row, rowgroup, column, colgroup, and table borders
// tier-2: view-pool, rebuilt each relayout
struct CollapsedBorder {
    float width;
    CssEnum style;      // CSS_VALUE_NONE, CSS_VALUE_HIDDEN, CSS_VALUE_SOLID, etc.
    Color color;        // Border color (simple RGBA union)
    uint8_t priority;   // Used for conflict resolution (higher wins)

    CollapsedBorder() : width(0), style(CSS_VALUE_NONE), priority(0) {
        color.r = color.g = color.b = color.a = 0;
    }
};

// tier-2: view-pool, rebuilt each relayout
struct TableCellProp {
    // Vertical alignment
    enum {
        CELL_VALIGN_TOP = 0,
        CELL_VALIGN_MIDDLE = 1,
        CELL_VALIGN_BOTTOM = 2,
        CELL_VALIGN_BASELINE = 3
    } vertical_align;

    // Cell spanning metadata
    int col_span;  // Number of columns this cell spans (default: 1)
    int row_span;  // Number of rows this cell spans (default: 1)
    // tier-3 scratch, valid within layout pass: TableMetadata is column/row keyed
    // and has no cell-key lookup, while later border-collapse phases revisit cells.
    int col_index; // Starting column index (computed during layout)
    int row_index; // Starting row index (computed during layout)
    uint8_t is_annoy_tr:1;       // whether this element is doubled as an anonymous tr
    uint8_t is_annoy_td:1;       // whether this element is doubled as an anonymous td
    uint8_t is_annoy_colgroup:1; // whether this element is doubled as an anonymous colgroup
    uint8_t is_empty:1;          // whether this cell has no content (for empty-cells: hide)
    uint8_t hide_empty:1;        // combined flag: is_empty && table has empty-cells: hide

    // tier-3 scratch, valid within layout pass for the same multiphase table lifetime.
    // Intrinsic width (content + padding) measured during column sizing
    // Used in border-collapse mode to re-compute column widths with per-cell border halves
    float intrinsic_width;

    // Border-collapse resolved borders (CSS 2.1 §17.6.2)
    // Only populated when table has border-collapse: collapse
    // These store the winning borders after conflict resolution
    // Used during rendering phase to draw correct collapsed borders
    CollapsedBorder* top_resolved;
    CollapsedBorder* right_resolved;
    CollapsedBorder* bottom_resolved;
    CollapsedBorder* left_resolved;
};

// tier-2: view-pool, rebuilt each relayout
typedef struct ViewTableCell : ViewBlock {
    TableCellProp* cell() const { return role_kind() == ROLE_CELL ? td : nullptr; }
} ViewTableCell;

// Direct fi/gi/tb/td/form reads outside these tag-checking accessors are invalid;
// parent-item role and the element's own role occupy separate tagged unions.

// Radiant view wrappers are static_cast/reinterpret_cast overlays on DOM storage
// (see lib/tagged.hpp unsafe_* helpers), so adding fields here corrupts nodes.
// The compiler trait keeps this C+ header independent of the C++ standard library.
static_assert(__is_trivially_copyable(DomNode), "DomNode must remain trivially copyable");
static_assert(__is_trivially_copyable(DomText), "DomText must remain trivially copyable");
static_assert(__is_trivially_copyable(DomComment), "DomComment must remain trivially copyable");
static_assert(__is_trivially_copyable(DomElement), "DomElement must remain trivially copyable");
static_assert(sizeof(View) == sizeof(DomNode), "View must remain a DomNode alias");
static_assert(sizeof(ViewText) == sizeof(DomText), "ViewText must not add fields");
static_assert(sizeof(ViewElement) == sizeof(DomElement), "ViewElement must not add fields");
static_assert(sizeof(ViewSpan) == sizeof(DomElement), "ViewSpan must not add fields");
static_assert(sizeof(ViewMarker) == sizeof(DomElement), "ViewMarker must not add fields");
static_assert(sizeof(ViewBlock) == sizeof(DomElement), "ViewBlock must not add fields");
static_assert(sizeof(ViewTable) == sizeof(DomElement), "ViewTable must not add fields");
static_assert(sizeof(ViewTableRowGroup) == sizeof(DomElement), "ViewTableRowGroup must not add fields");
static_assert(sizeof(ViewTableRow) == sizeof(DomElement), "ViewTableRow must not add fields");
static_assert(sizeof(ViewTableCell) == sizeof(DomElement), "ViewTableCell must not add fields");
static_assert(sizeof(DomElement) <= 368, "DomElement size ratchet regressed");
static_assert(sizeof(DomText) <= 120, "DomText size ratchet regressed");
static_assert(sizeof(DomNode) <= 80, "DomNode size ratchet regressed");

typedef enum HtmlVersion {
    HTML5 = 1,              // HTML5
    HTML4_01_STRICT,        // HTML4.01 Strict
    HTML4_01_TRANSITIONAL,  // HTML4.01 Transitional
    HTML4_01_FRAMESET,      // HTML4.01 Frameset
    HTML_QUIRKS,            // Legacy HTML or missing DOCTYPE
    HTML1_0,                // HTML 1.0 (1991) - uses <HEADER> as head, <NEXTID> void element
} HtmlVersion;

// WHATWG Quirks Mode: https://quirks.spec.whatwg.org/
// In quirks mode (missing DOCTYPE, or Transitional/Frameset without system identifier),
// certain CSS behaviors differ from standards mode.
inline bool is_quirks_mode(HtmlVersion v) {
    return v == HTML4_01_TRANSITIONAL || v == HTML4_01_FRAMESET ||
           v == HTML_QUIRKS || v == HTML1_0;
}

struct MeasurementCacheEntry;
struct CanonicalInlineEntry;

typedef struct CanonicalPropStats {
    uint64_t inline_lookups;
    uint64_t inline_hits;
    uint64_t inline_misses;
    uint64_t inline_exact_compares;
    uint64_t inline_collisions;
    uint64_t inline_promotions;
    uint64_t inline_cows;
    uint64_t cap_fallbacks;
    size_t index_bytes;
} CanonicalPropStats;

// tier-2: view-pool, rebuilt each relayout
struct ViewTree {
    Pool* prop_pool;       // Mutable element-owned view props; survives retained reflow.
    Arena* canonical_prop_arena; // Immutable shared props; survives ordinary style/layout generations.
    Arena* scratch_arena;  // Pass-local layout/render scratch; never owns retained props.
    CanonicalInlineEntry** inline_canonical_buckets; // Resizable exact-value index in prop_pool.
    size_t inline_canonical_bucket_count;
    size_t inline_canonical_count;
    size_t canonical_prop_cap_bytes;
    CanonicalPropStats canonical_stats;
    TextRect* free_text_rects; // Reusable retained text fragments owned by prop_pool.
    View* root;
    HtmlVersion html_version;
    MeasurementCacheEntry* measurement_cache;
    int measurement_cache_count;
    int measurement_cache_capacity;
    uint32_t measurement_cache_generation;
    uint32_t layout_generation; // Advances at each retained full-layout boundary.
#ifdef __cplusplus
    void init();
    void reset_retained();
    void destroy();
    void* alloc_prop(size_t size);
    TextRect* alloc_text_rect();
    void recycle_text_rects(TextRect* first);
#endif
};

uint64_t inline_prop_hash(const InlineProp* value);
bool inline_prop_equal(const InlineProp* left, const InlineProp* right);
void view_tree_canonical_init(ViewTree* tree);
void view_tree_canonical_destroy(ViewTree* tree);
void view_tree_commit_inline_prop(ViewTree* tree, DomElement* element,
                                  DomElement* parent);

void release_dom_owned_embed_images(DomElement* elem);
void view_tree_release_retired_subtree(ViewTree* tree, DomNode* root);

// Forward declaration for DocState (full definition in state_store.hpp)
struct DocState;
typedef struct DocState DocState;

// Forward declarations for state types (full definitions in state_store.hpp)
struct CursorState;
struct SelectionPresentation;
struct FocusState;
struct BrowsingSession;  // Browsing session for web navigation
struct EventStateLog;    // per-document JSONL event/state log
struct StateDumpLog;     // per-document Mark state-store dump

struct StateStore;
typedef struct StateStore StateStore;

// rendering context structs
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    float x, y;  // abs x, y relative to entire canvas/screen
    Bound clip;  // clipping rect
    Corner clip_radius;  // rounded corner clipping (for overflow:hidden with border-radius)
    bool has_clip_radius;  // true if clip_radius should be applied
} BlockBlot;

// tier-2: view-pool, rebuilt each relayout
typedef struct {
    CssEnum list_style_type;
    int item_index;
} ListBlot;


// consolidated Radiant view/style API (DD4); declarations below retain source-file section names for history lookup.

// ===== symbol resolver =====

/**
 * @file symbol_resolver.h
 * @brief Unified symbol resolution for rendering HTML entities and emoji shortcodes
 *
 * This module provides a unified API for resolving Symbol items to their
 * UTF-8 string representations during rendering. It combines:
 * - HTML entity names (copy → ©, mdash → —, etc.)
 * - Emoji shortcodes (smile → 😄, heart → ❤️, etc.)
 *
 * Resolution priority:
 * 1. Emoji shortcodes (if enabled)
 * 2. HTML entity names
 */



#ifdef __cplusplus
extern "C" {
#endif

/**
 * Symbol type after resolution
 */
typedef enum {
    SYMBOL_UNKNOWN = 0,     // Unknown symbol
    SYMBOL_HTML_ENTITY,     // HTML entity (copy, mdash, etc.)
    SYMBOL_EMOJI            // Emoji shortcode (smile, heart, etc.)
} SymbolType;

/**
 * Result of symbol resolution
 */
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    SymbolType type;
    const char* utf8;           // UTF-8 string representation (static, do not free)
    size_t utf8_len;            // Length of UTF-8 string
    uint32_t codepoint;         // Primary Unicode codepoint (for single-codepoint symbols)
} SymbolResolution;

/**
 * Resolve a symbol name to its UTF-8 representation
 *
 * @param name Symbol name (without & ; or : delimiters)
 * @param len Length of symbol name
 * @return SymbolResolution with UTF-8 string and metadata
 *
 * Example:
 *   SymbolResolution r = resolve_symbol("copy", 4);
 *   // r.type == SYMBOL_HTML_ENTITY
 *   // r.utf8 == "©"
 *   // r.codepoint == 0x00A9
 *
 *   SymbolResolution r = resolve_symbol("smile", 5);
 *   // r.type == SYMBOL_EMOJI
 *   // r.utf8 == "😄"
 */
SymbolResolution resolve_symbol(const char* name, size_t len);

/**
 * Resolve a symbol from a Lambda String* symbol
 * Convenience wrapper that extracts name from String
 */
SymbolResolution resolve_symbol_string(const void* string_ptr);

/**
 * Check if a symbol name is a known emoji shortcode
 */

/**
 * Check if a symbol name is a known HTML entity
 */
bool is_html_entity(const char* name, size_t len);

#ifdef __cplusplus
}
#endif


// ===== font face =====

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct LayoutContext;
struct UiContext;

// Maximum number of src entries in a single @font-face rule
#define FONT_FACE_MAX_SRC 8

// Individual src entry with path and format
// tier-2: view-pool, rebuilt each relayout
typedef struct FontFaceSrc {
    char* path;                  // Resolved local path
    char* format;                // Format string: "woff", "truetype", "opentype", etc.
} FontFaceSrc;

// Font face descriptor for @font-face support
// Descriptors are registered with the unified font module (lib/font) via
// font_face_register(). Actual font loading is handled entirely by the
// unified module; this struct only stores the CSS @font-face metadata.
// tier-2: view-pool, rebuilt each relayout
typedef struct FontFaceDescriptor {
    char* family_name;           // font-family value
    char* src_local_path;        // local font file path (no web URLs) - first/fallback
    char* src_local_name;        // src: local() font name value
    FontFaceSrc* src_entries;    // Array of all src entries with formats
    int src_count;               // Number of entries in src_entries array
    CssEnum font_style;         // normal, italic, oblique
    CssEnum font_weight;        // 100-900, normal, bold
    CssEnum font_display;       // auto, block, swap, fallback, optional
    bool is_loaded;              // loading state
} FontFaceDescriptor;

// ============================================================================
// Text flow logging categories
// ============================================================================

extern log_category_t* font_log;
extern log_category_t* text_log;
extern log_category_t* layout_log;

// Logging initialization
void init_text_flow_logging(void);
// ============================================================================
// CSS @font-face parsing and registration
// ============================================================================

// Forward declarations for CSS types
struct CssStylesheet;

// Parse and register @font-face rules from a CSS rule node
void parse_font_face_rule(struct LayoutContext* lycon, void* rule);

// Register a font face descriptor with UiContext (and bridge to unified FontContext)
void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor);

// Process all @font-face rules from a stylesheet
void process_font_face_rules_from_stylesheet(UiContext* uicon, struct CssStylesheet* stylesheet, const char* base_path);

// Process all @font-face rules from all stylesheets in a document
void process_document_font_faces(UiContext* uicon, struct DomDocument* doc);

#ifdef __cplusplus
}
#endif


// ===== font API =====

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct UiContext;
struct FontBox;
struct FontProp;

// Function declarations
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
void fontface_cleanup(UiContext* uicon);

#ifdef __cplusplus
}
#endif


// ===== clip shapes =====

// ============================================================================
// Vector clip shapes for overflow:hidden and CSS clip-path
// ============================================================================

enum ClipShapeType {
    CLIP_SHAPE_NONE = 0,
    CLIP_SHAPE_POLYGON,
    CLIP_SHAPE_CIRCLE,
    CLIP_SHAPE_ELLIPSE,
    CLIP_SHAPE_INSET,
    CLIP_SHAPE_ROUNDED_RECT
};

// tier-2: view-pool, rebuilt each relayout
struct ClipShape {
    ClipShapeType type;
    union {
        struct { float* vx; float* vy; int count; } polygon;
        struct { float cx, cy, r; } circle;
        struct { float cx, cy, rx, ry; } ellipse;
        struct { float x, y, w, h, rx, ry; } inset;
        struct { float x, y, w, h, r_tl, r_tr, r_br, r_bl; } rounded_rect;
    };
};

#define RDT_MAX_CLIP_SHAPES 8

bool clip_point_in_rounded_rect(float px, float py,
    float rx, float ry, float rw, float rh,
    float r_tl, float r_tr, float r_br, float r_bl);
void clip_scanline_rounded_rect(
    float rx, float ry, float rw, float rh,
    float r_tl, float r_tr, float r_br, float r_bl,
    float y, float* out_left, float* out_right);
bool clip_point_in_circle(float px, float py, float cx, float cy, float r);
bool clip_point_in_ellipse(float px, float py, float cx, float cy, float rx, float ry);
bool clip_point_in_inset(float px, float py, float ix, float iy, float iw, float ih);
bool clip_point_in_polygon(float px, float py, const float* vx, const float* vy, int count);
bool clip_point_in_shape(ClipShape* cs, float px, float py);
void clip_scanline_circle(float cx, float cy, float r,
    float y, float* out_left, float* out_right);
void clip_scanline_ellipse(float cx, float cy, float rx, float ry,
    float y, float* out_left, float* out_right);
void clip_scanline_polygon(const float* vx, const float* vy, int count,
    float y, float* out_left, float* out_right);
bool clip_shape_rect_inside(ClipShape* cs, float x, float y, float w, float h);
bool clip_shapes_rect_inside(ClipShape** shapes, int depth,
    float x, float y, float w, float h);
void clip_shapes_scanline_bounds(ClipShape** shapes, int depth,
    float y, int base_left, int base_right, int* out_left, int* out_right);
ClipShape clip_shape_from_params(int type, const float* params);
void clip_shape_to_params(const ClipShape* cs, int* out_type, float* out_params);


// ===== form controls =====

struct DomElement;
struct FontProp;
struct GridItemProp;
struct ViewState;

// Forward decl from text_edit.hpp (avoids circular include).
struct EditHistory;
void te_history_free(EditHistory* h);

/**
 * Form Control Support for Radiant
 *
 * Form elements (input, button, select, textarea) are "replaced elements"
 * with intrinsic dimensions determined by control type rather than content flow.
 */

// Form control types
enum FormControlType {
    FORM_CONTROL_NONE = 0,
    FORM_CONTROL_TEXT,          // text, password, email, url, search, tel, number
    FORM_CONTROL_CHECKBOX,
    FORM_CONTROL_RADIO,
    FORM_CONTROL_BUTTON,        // button, submit, reset
    FORM_CONTROL_SELECT,
    FORM_CONTROL_TEXTAREA,
    FORM_CONTROL_RANGE,
    FORM_CONTROL_IMAGE,         // type="image" - replaced element (image button)
    FORM_CONTROL_HIDDEN,        // type="hidden" - no visual
};

// Default intrinsic sizes (CSS pixels at 1x pixel ratio)
// All dimensions are border-box values matching Chrome UA defaults
namespace FormDefaults {
    // Text input: ~20 characters wide
    // Chrome default: 153x21 border-box (1px border, 1px padding top/bottom, 2px padding left/right)
    constexpr float TEXT_WIDTH = 153.0f;   // border-box width
    constexpr float TEXT_HEIGHT = 21.0f;   // border-box height
    constexpr float TEXT_PADDING_H = 2.0f;
    constexpr float TEXT_PADDING_V = 1.0f;
    constexpr float TEXT_BORDER = 2.0f;
    constexpr int   TEXT_SIZE_CHARS = 20;  // default size attribute

    // Checkbox/Radio: square controls
    constexpr float CHECK_SIZE = 13.0f;
    constexpr float CHECK_MARGIN = 3.0f;
    // Radio: margin-left=5, margin-right=3 (Chrome UA stylesheet)
    constexpr float RADIO_MARGIN_LEFT = 5.0f;
    constexpr float RADIO_MARGIN_RIGHT = 3.0f;
    // Checkbox: margin-left=4, margin-right=3 (Chrome UA stylesheet)
    constexpr float CHECKBOX_MARGIN_LEFT = 4.0f;
    constexpr float CHECKBOX_MARGIN_RIGHT = 3.0f;

    // Button: content-based + padding + 2px border
    constexpr float BUTTON_PADDING_H = 6.0f;
    constexpr float BUTTON_PADDING_V = 1.0f;
    constexpr float BUTTON_BORDER = 2.0f;   // Chrome: 2px outset border
    constexpr float BUTTON_MIN_WIDTH = 52.0f;  // minimum button width

    // Select dropdown
    // Chrome default: height=19 border-box, width depends on content
    constexpr float SELECT_WIDTH = 57.0f;  // typical default for short options
    constexpr float SELECT_HEIGHT = 19.0f; // border-box height
    constexpr float SELECT_ARROW_WIDTH = 16.0f; // painted arrow glyph area
    constexpr float SELECT_NATIVE_ARROW_AREA = 20.0f; // themed arrow button and text gap
    constexpr float SELECT_BORDER = 1.0f;
    constexpr float OPTION_PADDING_H = 2.0f;
    constexpr float BASE_SELECT_PADDING_H = 8.0f;
    constexpr float BASE_SELECT_PADDING_V = 4.0f;
    constexpr float BASE_SELECT_GAP = 8.0f;
    constexpr float BASE_SELECT_ICON_WIDTH = 10.0f;
    // Options inside an <optgroup> are indented in the dropdown popup on macOS Chrome.
    // The indent contributes to the intrinsic select width for each optgroup option.
    constexpr float OPTGROUP_OPTION_INDENT = 17.0f;
    // A blank option inside an optgroup still occupies at least this much display width
    // (the indent area itself), even if its text is empty.
    constexpr float OPTGROUP_OPTION_MIN_WIDTH = 20.0f;

    // Textarea: default cols/rows
    // Chrome default: 182x36 border-box (20 cols, 2 rows)
    constexpr int   TEXTAREA_COLS = 20;
    constexpr int   TEXTAREA_ROWS = 2;
    constexpr float TEXTAREA_PADDING = 2.0f;
    constexpr float TEXTAREA_BORDER = 1.0f;

    // Range slider
    constexpr float RANGE_WIDTH = 129.0f;
    constexpr float RANGE_HEIGHT = 16.0f;        // Chrome: 16px border-box (no list)
    constexpr float RANGE_HEIGHT_WITH_LIST = 22.0f;  // Chrome: 22px border-box (with list/datalist for tick marks)
    constexpr float RANGE_TRACK_HEIGHT = 5.0f;
    constexpr float RANGE_THUMB_SIZE = 13.0f;

    // Meter: Chrome default 80x16
    constexpr float METER_WIDTH = 80.0f;
    constexpr float METER_HEIGHT = 16.0f;

    // Progress: Chrome default 160x16
    constexpr float PROGRESS_WIDTH = 160.0f;
    constexpr float PROGRESS_HEIGHT = 16.0f;

    // Fieldset
    constexpr float FIELDSET_PADDING = 10.0f;
    constexpr float FIELDSET_BORDER_WIDTH = 2.0f;

    // Image input (broken image fallback): Chrome shows ~57.5x16
    constexpr float IMAGE_INPUT_WIDTH = 57.5f;
    constexpr float IMAGE_INPUT_HEIGHT = 16.0f;

    // Common border colors (3D effect)
    constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;   // white highlight
    constexpr uint32_t BORDER_DARK = 0xFF767676;    // dark shadow
    constexpr uint32_t BORDER_MID = 0xFFA0A0A0;     // mid gray
    constexpr uint32_t INPUT_BG = 0xFFFFFFFF;       // white background
    constexpr uint32_t BUTTON_BG = 0xFFE0E0E0;      // light gray button
    constexpr uint32_t PLACEHOLDER_COLOR = 0xFF757575;  // gray placeholder text
}

/**
 * FormControlProp - Properties for form control elements
 */
// tier-2: view-pool, rebuilt each relayout
struct FormControlProp {
    // Fast-read pointer to centralized state owner. Writers must use state_store APIs.
    struct DocState* state_ref;
    ViewState* form_state_ref;
    ViewState* scroll_state_ref;

    FormControlType control_type;
    const char* input_type;     // Original type attribute value
    const char* value;          // Current value (for display)
    const char* placeholder;    // Placeholder text
    const char* name;           // Form field name

    // Sizing attributes
    int size;                   // Character width for text inputs (size attr)
    int cols;                   // Textarea columns
    int rows;                   // Textarea rows
    int maxlength;              // Max input length

    // Range input properties
    float range_min;
    float range_max;
    float range_step;
    float range_value;          // Current position (normalized 0-1)

    // State flags (bitfield)
    uint8_t disabled : 1;
    uint8_t readonly : 1;
    uint8_t checked : 1;        // For checkbox/radio
    uint8_t required : 1;
    uint8_t autofocus : 1;
    uint8_t multiple : 1;       // For select
    uint8_t dropdown_open : 1;  // For select: dropdown is currently open
    uint8_t appearance_none : 1; // CSS appearance: none — suppress UA-rendered chrome (arrow, etc.)
    uint8_t appearance_base_select : 1; // CSS UI 4 base appearance for customizable select

    // Select dropdown properties
    int selected_index;         // Index of currently selected option (0-based, -1 if none)
    int option_count;           // Total number of options
    int hover_index;            // Index of currently hovered option in dropdown (-1 if none)
    int select_size;            // Visible rows for select listbox (HTML size attr; 0 = not set)

    // Computed intrinsic dimensions (in physical pixels)
    float intrinsic_width;
    float intrinsic_height;

    // Computed ::placeholder pseudo-element rendering style.
    FontProp* placeholder_font;
    uint8_t placeholder_color_r;
    uint8_t placeholder_color_g;
    uint8_t placeholder_color_b;
    uint8_t placeholder_color_a;
    float placeholder_opacity;
    uint8_t placeholder_has_color : 1;
    uint8_t placeholder_has_opacity : 1;
    uint8_t heap_allocated : 1;

    // ------------------------------------------------------------------
    // Text-control selection state (input text-types and textarea only)
    //   - current_value:  mutable user-edited value (UTF-8). When non-null
    //     this is the live `.value` IDL attribute. nullptr ⇒ fall back to
    //     `value` HTML attribute (for input) or text content (for textarea).
    //     Heap-allocated via malloc/realloc; freed in destructor.
    //   - selection_start/end:  UTF-16 code-unit offsets into the value.
    //   - selection_direction:  0=none, 1=forward, 2=backward.
    //   - tc_initialized: lazy-init flag (selection set to (len,len) on
    //     first access per HTML spec default).
    // See vibe/radiant/Radiant_Design_Selection.md §8.
    // ------------------------------------------------------------------
    char*    current_value;
    uint32_t current_value_len;       // UTF-8 byte length
    uint32_t current_value_u16_len;   // cached UTF-16 length
    uint32_t selection_start;         // UTF-16 code units
    uint32_t selection_end;           // UTF-16 code units
    uint8_t  selection_direction;     // 0=none, 1=forward, 2=backward
    uint8_t  tc_initialized : 1;
    uint8_t  tc_sc_pending : 1;       // queued in state->tc_selectionchange_head

    // Phase 8E: per-text-control selectionchange coalescing list link.
    // Single-linked through this pointer when the element is on the pending
    // list; nullptr otherwise.
    DomElement* tc_sc_next_pending;

    // Constraint Validation API (§4.10.20)
    // Custom validity error message set via setCustomValidity(msg).
    // nullptr or "" means no custom error.
    char* custom_validity_msg;

    // ------------------------------------------------------------------
    // F1 (Radiant_Design_Form_Input.md §3.1):
    //   - value_at_focus: snapshot of current_value taken when this text
    //     control receives focus. Used by te_blur_dispatch_change() to
    //     decide whether to fire `change` on blur (HTML §4.10.5.5).
    //   - history: undo/redo ring (lazy-allocated by text_edit.cpp).
    //     Opaque here; defined in text_edit.hpp.
    // ------------------------------------------------------------------
    char*    value_at_focus;
    uint32_t value_at_focus_len;
    void*    history;   // EditHistory*; lazy

    // ------------------------------------------------------------------
    // F4 (Radiant_Design_Form_Input.md §3.1, §3.8):
    //   - scroll_x / scroll_y: viewport offset for auto-scroll. The text
    //     content is shifted left/up by these amounts so the caret stays
    //     visible inside the content box. Updated by render_form before
    //     drawing the caret.
    //   - caret_blink_t: monotonic seconds since the last caret toggle.
    //   - caret_on: visibility flag toggled by the blink timer (always
    //     true in headless renders so snapshots stay deterministic).
    //   - pseudo-state bits live in StateStore; FormControlProp keeps only
    //     text rendering/session projection fields here.
    // ------------------------------------------------------------------
    float    scroll_x;
    float    scroll_y;
    float    caret_blink_t;
    uint8_t  caret_on : 1;
    uint8_t  password_reveal_active : 1;
    uint32_t password_reveal_start;
    uint32_t password_reveal_end;
    double   password_reveal_elapsed;

    // ------------------------------------------------------------------
    // F7 (Radiant_Design_Form_Input.md §3.7): IME / composition preedit.
    // The preedit string is the partially-entered text shown by the OS
    // input method between `compositionstart` and `compositionend`. It is
    // NOT part of `current_value` until commit. The renderer overlays it
    // at the caret with an underline so the user can see what they're
    // composing. `preedit_caret` is the codepoint offset inside preedit
    // (where the IME's own caret sits).
    // ------------------------------------------------------------------
    char*    preedit_utf8;
    uint32_t preedit_len;
    uint32_t preedit_caret;

    // FormControlProp is a POD (no C++ ctor/dtor) per the C+ convention; use
    // form_control_prop_init / form_control_prop_release for lifecycle.
};

// Apply the non-zero default field values. Memory pointed to by `f` must be
// either zeroed (e.g. from pool_calloc / mem_calloc) or freshly-allocated
// scratch — this function only assigns the non-zero defaults.
void form_control_prop_init(FormControlProp* f);

// Release owned heap pointers (current_value, custom_validity_msg,
// value_at_focus, history, preedit_utf8). Does NOT free `f` itself.
void form_control_prop_release(FormControlProp* f);

// Release a form-control property attached to a DOM element. This is used by
// both layout-owned views and JS-created detached nodes.
void form_control_release_prop(DomElement* elem);

// Helper functions

/**
 * Determine FormControlType from input type attribute
 */
inline FormControlType get_input_control_type(const char* type) {
    if (!type || !*type) return FORM_CONTROL_TEXT;  // default is text

    // Text-like inputs
    if (strcmp(type, "text") == 0 ||
        strcmp(type, "password") == 0 ||
        strcmp(type, "email") == 0 ||
        strcmp(type, "url") == 0 ||
        strcmp(type, "search") == 0 ||
        strcmp(type, "tel") == 0 ||
        strcmp(type, "number") == 0) {
        return FORM_CONTROL_TEXT;
    }

    // Toggle controls
    if (strcmp(type, "checkbox") == 0) return FORM_CONTROL_CHECKBOX;
    if (strcmp(type, "radio") == 0) return FORM_CONTROL_RADIO;

    // Button types
    if (strcmp(type, "submit") == 0 ||
        strcmp(type, "reset") == 0 ||
        strcmp(type, "button") == 0) {
        return FORM_CONTROL_BUTTON;
    }

    // Image button - replaced element with image dimensions
    if (strcmp(type, "image") == 0) return FORM_CONTROL_IMAGE;

    // Special types
    if (strcmp(type, "hidden") == 0) return FORM_CONTROL_HIDDEN;
    if (strcmp(type, "range") == 0) return FORM_CONTROL_RANGE;

    // File, date, color etc. - treat as text for now
    return FORM_CONTROL_TEXT;
}


// ===== CSS animations =====

// Forward declarations
struct DomElement;
struct DomDocument;
struct LayoutContext;

// ============================================================================
// Animated Property Values
// ============================================================================

typedef enum CssAnimValueType {
    ANIM_VAL_NONE = 0,
    ANIM_VAL_FLOAT,         // opacity, numeric values
    ANIM_VAL_COLOR,         // color, background-color, border-*-color
    ANIM_VAL_LENGTH,        // width, height, margin-*, padding-*, top/right/bottom/left
    ANIM_VAL_TRANSFORM,     // transform function list
} CssAnimValueType;

// Forward declaration from view.hpp
struct TransformFunction;

// tier-2: view-pool, rebuilt each relayout
typedef struct CssAnimatedProp {
    CssPropertyId property_id;
    CssAnimValueType value_type;
    union {
        float f;                // ANIM_VAL_FLOAT
        Color color;            // ANIM_VAL_COLOR
        struct {
            float value;
            bool is_percent;
        } length;               // ANIM_VAL_LENGTH
        TransformFunction* transform;  // ANIM_VAL_TRANSFORM (linked list)
    } value;
} CssAnimatedProp;

// ============================================================================
// Keyframe Data Structures
// ============================================================================

// A single keyframe stop (e.g., "50% { opacity: 0.5; transform: scale(1.2); }")
// tier-2: view-pool, rebuilt each relayout
typedef struct CssKeyframeStop {
    float offset;               // 0.0 (from) to 1.0 (to)
    CssAnimatedProp* properties;
    int property_count;
    TimingFunction* timing;     // per-keyframe easing (NULL = use animation easing)
} CssKeyframeStop;

// A parsed @keyframes rule
// tier-2: view-pool, rebuilt each relayout
typedef struct CssKeyframes {
    const char* name;           // animation name (e.g., "fadeIn")
    CssKeyframeStop* stops;     // sorted by offset ascending
    int stop_count;
} CssKeyframes;

// ============================================================================
// Keyframe Registry (per document)
// ============================================================================

// tier-2: view-pool, rebuilt each relayout
typedef struct KeyframeRegistry {
    CssKeyframes** entries;
    int count;
    int capacity;
    Pool* pool;
} KeyframeRegistry;

// Create a keyframe registry from all @keyframes rules in the document's stylesheets
KeyframeRegistry* keyframe_registry_create(DomDocument* doc, Pool* pool);

// Look up a @keyframes rule by name
CssKeyframes* keyframe_registry_find(KeyframeRegistry* registry, const char* name);

// ============================================================================
// CSS Animation Configuration (per element, populated during style resolution)
// ============================================================================

// tier-2: view-pool, rebuilt each relayout
typedef struct CssAnimProp {
    const char* name;           // animation-name (keyframes reference)
    float duration;             // animation-duration in seconds
    float delay;                // animation-delay in seconds
    int iteration_count;        // -1 = infinite
    AnimationDirection direction;
    AnimationFillMode fill_mode;
    AnimationPlayState play_state;
    TimingFunction timing;      // animation-timing-function
} CssAnimProp;

// ============================================================================
// CSS Transition Configuration (per element)
// ============================================================================

// tier-2: view-pool, rebuilt each relayout
typedef struct CssTransitionProp {
    CssPropertyId* properties;  // transitioned property IDs (NULL = all)
    int property_count;         // -1 = "all"
    float duration;             // transition-duration in seconds
    float delay;                // transition-delay in seconds
    TimingFunction timing;      // transition-timing-function
} CssTransitionProp;

bool css_transition_resolve_config(StyleTree* style_tree, Pool* pool,
                                   CssTransitionProp* transition,
                                   CssPropertyId* property_buffer,
                                   int property_capacity);
bool css_transition_resolve_values(const CssValue* shorthand_value,
                                   const CssValue* duration_value,
                                   const CssValue* delay_value,
                                   const CssValue* property_value,
                                   const CssValue* timing_value,
                                   CssTransitionProp* transition,
                                   CssPropertyId* property_buffer,
                                   int property_capacity);

// ============================================================================
// CSS Animation Runtime State (attached to AnimationInstance.state)
// ============================================================================

// tier-2: view-pool, rebuilt each relayout
typedef struct CssAnimState {
    CssKeyframes* keyframes;
    DomElement* element;
    UiContext* ui_context;
    bool event_started;
    int event_iteration;
} CssAnimState;

// ============================================================================
// CSS Transition Runtime State
// ============================================================================

// The set of properties this vertical slice can transition. Only value types
// that both apply_animated_value (write side) and the used-value snapshot
// (read side) already handle are supported; others are deferred.
#define CSS_TRANSITION_MAX_TRACKED 3   // opacity, color, background-color

// One tracked transitionable property: its last-applied used value (the
// snapshot) plus the currently running transition instance (if any).
// tier-2: view-pool, rebuilt each relayout
typedef struct CssTransitionTrack {
    CssPropertyId property_id;
    CssAnimValueType value_type;
    bool has_snapshot;              // false until the first used value is observed
    union {
        float f;                    // ANIM_VAL_FLOAT (opacity)
        Color color;                // ANIM_VAL_COLOR (color, background-color)
    } snapshot;                     // last-applied used value
} CssTransitionTrack;

// Persistent per-element transition state (pointed to by DomElement.transition_state).
// tier-2: view-pool, rebuilt each relayout
typedef struct CssTransitionElemState {
    CssTransitionTrack tracks[CSS_TRANSITION_MAX_TRACKED];
    int track_count;
} CssTransitionElemState;

// Per-instance transition state (attached to AnimationInstance.state).
// tier-2: view-pool, rebuilt each relayout
typedef struct CssTransitionState {
    DomElement* element;
    UiContext* ui_context;
    CssPropertyId property_id;
    CssAnimValueType value_type;
    union {
        float f;
        Color color;
    } from;
    union {
        float f;
        Color color;
    } to;
} CssTransitionState;

// ============================================================================
// Property Interpolation
// ============================================================================

// Interpolate a float value: a + (b - a) * t
float css_interpolate_float(float a, float b, float t);

// Interpolate a color (per-channel linear in sRGB)
Color css_interpolate_color(Color a, Color b, float t);

// ============================================================================
// CSS Animation Lifecycle
// ============================================================================

// Create a CSS animation instance from animation properties and keyframes.
// Returns the AnimationInstance (already added to scheduler), or NULL on failure.
AnimationInstance* css_animation_create(AnimationScheduler* scheduler,
                                        DomElement* element,
                                        CssAnimProp* anim_prop,
                                        CssKeyframes* keyframes,
                                        double now,
                                        Pool* pool);

// Animation tick callback (applied by AnimationScheduler)
void css_animation_tick(AnimationInstance* anim, float t);

// Animation finish callback
void css_animation_finish(AnimationInstance* anim);

// ============================================================================
// Integration with Style Resolution
// ============================================================================

// Process animation properties during style resolution and start animations
// if animation-name references valid @keyframes. Called after resolve_css_styles.
void css_animation_resolve(DomElement* element, LayoutContext* lycon);

// ============================================================================
// CSS Transition Lifecycle
// ============================================================================

// Transition tick callback: interpolates from→to and applies via apply_animated_value.
void css_transition_tick(AnimationInstance* anim, float t);

// Transition finish callback.
void css_transition_finish(AnimationInstance* anim);

// Process transition-* properties during style resolution. Reads the element's
// newly-computed used values (opacity/color/background-color), compares them to
// the persistent per-element snapshot, and starts an ANIM_CSS_TRANSITION for each
// property that actually changed (given a matching transition declaration).
// Called from layout, right after resolve_css_styles + css_animation_resolve.
void css_transition_resolve(DomElement* element, LayoutContext* lycon);


// ===== CSS temporary declarations =====

// CSS shorthand resolve-only declaration helpers.
//
// Background: shorthand expansion in resolve_css_style.cpp copies a parsed
// CssDeclaration, rewrites property_id, and points value at a longhand
// CSS value before calling resolve_css_property(). When the value is a small
// synthetic list built on the stack, the list must stay alive for the whole
// resolve call. Manually assigning decl.value = &local_list is fragile: a
// narrower lexical scope for the list leads to stack-use-after-scope (see
// vibe/Memory_Safety_Template4.md §1).
//
// These helpers tie the scratch list storage to the resolve() call so the
// stack value cannot outlive — or under-live — the call. The copied
// declaration is never handed back to callers, so it cannot be re-pointed at
// a narrower-scope value after construction.
//
// Contract (Template4 §3, §9): resolve_css_property() may read decl->value
// during the call but must not retain a pointer from a resolve-only
// declaration. Persistent CSS values use the PersistentField path instead.

// CssDeclaration, CssValue, CssPropertyId, CSS_VALUE_TYPE_LIST

// LayoutContext lives in radiant/layout.hpp. Forward declare it here so view
// helpers can expose style-resolution entry points without dragging in layout.
struct LayoutContext;

// ===== style resolution =====

float convert_lambda_length_to_px(const CssValue* value, LayoutContext* lycon,
                                   CssPropertyId prop_id);
Color resolve_color_value(LayoutContext* lycon, const CssValue* value);
Color color_name_to_rgb(CssEnum color_name);
int64_t get_cascade_priority(const CssDeclaration* decl);
float resolve_length_value(LayoutContext* lycon, uintptr_t property, const CssValue* value);
char* resolve_css_resource_url(LayoutContext* lycon, const CssDeclaration* decl,
                               const char* url);
const CssValue* resolve_var_function(LayoutContext* lycon, const CssValue* value);
const char* css_font_family_name_from_value(const CssValue* value);
bool css_font_family_is_available(LayoutContext* lycon, const char* family,
                                  bool require_loadable_face_source);
const char* css_join_font_family_values(LayoutContext* lycon, const CssValue* list,
                                        size_t start, size_t end);
const char* css_select_font_family(LayoutContext* lycon, const CssValue* value,
                                   bool require_loadable_face_source);
const char* css_select_font_shorthand_family(LayoutContext* lycon,
                                             const CssValue* shorthand_value,
                                             const CssValue* main_group,
                                             size_t family_start_index,
                                             bool require_loadable_face_source);
void resolve_css_styles(DomElement* dom_elem, LayoutContext* lycon);
void resolve_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon);
DisplayValue resolve_display_value(void* child);
DisplayValue blockify_display(DisplayValue display);

namespace lam {

// Resolve-only single-value declaration. Routes one parsed shorthand
// component to a longhand resolver without exposing the copied declaration.
class CssTempDecl {
    CssDeclaration decl_;

public:
    CssTempDecl(const CssDeclaration* base, CssPropertyId prop, const CssValue* value)
        : decl_(*base) {
        decl_.property_id = prop;
        // resolve-only contract: value is read during the call, never retained
        decl_.value = const_cast<CssValue*>(value);
    }

    CssTempDecl(const CssTempDecl&) = delete;
    CssTempDecl& operator=(const CssTempDecl&) = delete;

    void resolve(LayoutContext* lycon) {
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }
};

// Resolve-only list declaration with compile-time capacity N. The scratch
// list value and its backing pointer array live inside the helper, so they
// stay alive for the whole resolve() call. Appending past capacity returns
// false instead of overflowing.
template<int N>
class CssTempListDecl {
    CssDeclaration decl_;
    CssValue list_;
    const CssValue* values_[N];
    int count_;

public:
    CssTempListDecl(const CssDeclaration* base, CssPropertyId prop)
        : decl_(*base), list_(), count_(0) {
        decl_.property_id = prop;
        for (int i = 0; i < N; i++) values_[i] = nullptr;
    }

    CssTempListDecl(const CssTempListDecl&) = delete;
    CssTempListDecl& operator=(const CssTempListDecl&) = delete;

    int count() const { return count_; }

    // Append a borrowed shorthand component. Returns false on null value or
    // when the compile-time capacity N is already reached.
    bool append(const CssValue* value) {
        if (!value || count_ >= N) return false;
        values_[count_++] = value;
        return true;
    }

    // Route the collected components to the longhand resolver. A single
    // component is passed directly; multiple components are wrapped in the
    // helper-owned scratch list value. No-op when nothing was appended.
    void resolve(LayoutContext* lycon) {
        if (count_ <= 0) return;
        // resolve-only contract: components are read during the call, never retained
        if (count_ == 1) {
            decl_.value = const_cast<CssValue*>(values_[0]);
        } else {
            list_.type = CSS_VALUE_TYPE_LIST;
            list_.data.list.values = const_cast<CssValue**>(values_);
            list_.data.list.count = count_;
            decl_.value = &list_;  // CSS_TEMP_DECL_OK: list_ outlives this resolve call.
        }
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }
};

} // namespace lam


// ===== CSS transform helpers =====

/**
 * transform.hpp - CSS Transform utilities for Radiant Layout Engine
 *
 * Provides functions to:
 * 1. Compute combined transform matrix from TransformFunction chain
 * 2. Apply transform matrix to ThorVG paint objects
 * 3. Convert between coordinate systems
 */



namespace radiant {

extern RdtMatrix compute_transform_matrix(TransformFunction* functions,
                                          float width, float height,
                                          float origin_x, float origin_y,
                                          float perspective_distance = 0.0f,
                                          float perspective_origin_x = 0.0f,
                                          float perspective_origin_y = 0.0f);
extern bool has_transform(DomElement* elem);
extern void transform_point(float& x, float& y, const RdtMatrix& m);

} // namespace radiant


#ifndef LAMBDA_HEADLESS
// tier-2: view-pool, rebuilt each relayout
typedef struct {
    bool is_mouse_down;
    float down_x, down_y;  // mouse position when mouse down
    int cursor;  // current cursor style (CssEnum value)
    GLFWcursor* sys_cursor;
} MouseState;

// tier-2: view-pool, rebuilt each relayout
typedef struct UiContext {
    GLFWwindow *window;    // current window
    float window_width;    // window pixel width (actual framebuffer size, physical pixels)
    float window_height;   // window pixel height (actual framebuffer size, physical pixels)
    float viewport_width;  // intended viewport width (CSS logical pixels, for vh/vw units)
    float viewport_height; // intended viewport height (CSS logical pixels, for vh/vw units)
    ImageSurface* surface;  // rendering surface of a window

    // font handling
    struct FontContext* font_ctx; // unified font context
    Pool* font_pool;       // factory-registered root for font context allocations
    Arena* font_arena;     // factory-registered arena for font strings/database
    Arena* font_glyph_arena; // factory-registered arena for glyph bitmap caches
    FontProp default_font;  // default font style for HTML5
    FontProp legacy_default_font;  // default font style for legacy HTML before HTML5
    char** fallback_fonts;  // fallback fonts

    // @font-face support
    FontFaceDescriptor** font_faces;    // Array of @font-face declarations
    int font_face_count;
    int font_face_capacity;

    // image cache
    struct hashmap* image_cache;  // cache for images loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    DomDocument* document;  // current document
    MouseState mouse_state; // current mouse state
    struct BrowsingSession* browsing_session;  // web browsing session with history
    struct WebViewManager* webview_mgr;  // native web view manager (NULL until first <webview> element)
    struct EventStateLog* event_log;  // view-mode per-document event/state log owner
    struct StateDumpLog* state_dump_log;  // view-mode per-document Mark state dump owner
    bool event_log_enabled; // true when --event-log is active for view mode
    bool state_dump_enabled; // true when --state-dump is active for view mode
    bool headless;          // true if running headless (no visible window). When true, clipboard
                            // operations use the in-process ClipboardStore only and do NOT touch
                            // the OS pasteboard via GLFW (avoids cross-process races in tests).

    int init(bool headless);
    void create_surface(int pixel_width, int pixel_height);
    void destroy_document();
    void destroy();
} UiContext;

extern void* load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
extern void font_prop_release_handle(FontProp* fprop);
extern ImageSurface* load_image(UiContext* uicon, const char *file_path);
#endif // LAMBDA_HEADLESS

typedef struct DomDocument DomDocument;  // Forward declaration for Lambda CSS DOM Document
DomDocument* load_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height, float pixel_ratio = 1.0f);
DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_wiki_doc(Url* wiki_url, int viewport_width, int viewport_height, Pool* pool);
void free_document(DomDocument* doc);
