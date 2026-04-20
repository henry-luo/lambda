#ifndef RADIANT_LOTTIE_PLAYER_H
#define RADIANT_LOTTIE_PLAYER_H

#include <stdint.h>
#include "../lib/mempool.h"
#include "animation.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct ImageSurface;
struct DirtyTracker;

// ============================================================================
// Lottie Animation Player
// ============================================================================

typedef struct LottiePlayer {
    void* tvg_animation;        // Tvg_Animation (opaque, managed by ThorVG)
    void* tvg_canvas;           // Tvg_Canvas for rasterization (opaque)

    float total_frames;
    float frame_rate;
    float duration;             // seconds

    // Rendering target
    uint32_t* pixels;           // ABGR8888 buffer (owned by this player)
    int width, height;

    // Target image surface — pixel pointer is swapped on frame change
    struct ImageSurface* surface;

    bool loop;
    bool playing;
} LottiePlayer;

// Create a LottiePlayer from a file path and register with scheduler.
// Returns the animation instance, or NULL if the file is not a valid Lottie.
AnimationInstance* lottie_player_create_from_file(AnimationScheduler* scheduler,
                                                   struct ImageSurface* surface,
                                                   const char* path,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool);

// Create a LottiePlayer from in-memory data and register with scheduler.
// Returns the animation instance, or NULL if the data is not a valid Lottie.
AnimationInstance* lottie_player_create_from_data(AnimationScheduler* scheduler,
                                                   struct ImageSurface* surface,
                                                   const char* data, size_t length,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool);

// Tick callback for Lottie animation (called by scheduler).
void lottie_animation_tick(AnimationInstance* anim, float t);

// Finish callback for Lottie animation.
void lottie_animation_finish(AnimationInstance* anim);

// Detect if a file path looks like a Lottie file (by extension).
bool lottie_detect_by_path(const char* path);

// Detect if in-memory data is a Lottie JSON file (quick heuristic check).
bool lottie_detect_by_content(const unsigned char* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_LOTTIE_PLAYER_H
