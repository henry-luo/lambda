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
// CSS Transition Runtime State
// ============================================================================

// The set of properties this vertical slice can transition. Only value types
// that both apply_animated_value (write side) and the used-value snapshot
// (read side) already handle are supported; others are deferred.
#define CSS_TRANSITION_MAX_TRACKED 3   // opacity, color, background-color

// One tracked transitionable property: its last-applied used value (the
// snapshot) plus the currently running transition instance (if any).
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
typedef struct CssTransitionElemState {
    CssTransitionTrack tracks[CSS_TRANSITION_MAX_TRACKED];
    int track_count;
} CssTransitionElemState;

// Per-instance transition state (attached to AnimationInstance.state).
typedef struct CssTransitionState {
    DomElement* element;
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

#endif // RADIANT_CSS_ANIMATION_H
