#ifndef RADIANT_CSS_ANIMATION_H
#define RADIANT_CSS_ANIMATION_H

#include "animation.h"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_value.hpp"

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
typedef struct CssKeyframeStop {
    float offset;               // 0.0 (from) to 1.0 (to)
    CssAnimatedProp* properties;
    int property_count;
    TimingFunction* timing;     // per-keyframe easing (NULL = use animation easing)
} CssKeyframeStop;

// A parsed @keyframes rule
typedef struct CssKeyframes {
    const char* name;           // animation name (e.g., "fadeIn")
    CssKeyframeStop* stops;     // sorted by offset ascending
    int stop_count;
} CssKeyframes;

// ============================================================================
// Keyframe Registry (per document)
// ============================================================================

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

// Destroy a keyframe registry
void keyframe_registry_destroy(KeyframeRegistry* registry);

// ============================================================================
// CSS Animation Configuration (per element, populated during style resolution)
// ============================================================================

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

typedef struct CssTransitionProp {
    CssPropertyId* properties;  // transitioned property IDs (NULL = all)
    int property_count;         // -1 = "all"
    float duration;             // transition-duration in seconds
    float delay;                // transition-delay in seconds
    TimingFunction timing;      // transition-timing-function
} CssTransitionProp;

// ============================================================================
// CSS Animation Runtime State (attached to AnimationInstance.state)
// ============================================================================

typedef struct CssAnimState {
    CssKeyframes* keyframes;
    DomElement* element;
} CssAnimState;

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

#endif // RADIANT_CSS_ANIMATION_H
