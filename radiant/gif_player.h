#ifndef RADIANT_GIF_PLAYER_H
#define RADIANT_GIF_PLAYER_H

#include "../lib/image.h"
#include "../lib/mempool.h"
#include "animation.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct ImageSurface;
struct DirtyTracker;

// ============================================================================
// GIF Animation Player
// ============================================================================

typedef struct GifAnimation {
    GifFrames* frames;           // decoded frame data (owned, freed on destroy)
    int current_frame;           // index of currently displayed frame
    double frame_end_time;       // when to advance to next frame (absolute seconds)
    int loop_count;              // 0 = infinite (from GIF NETSCAPE extension)
    int loops_completed;         // number of loops finished so far

    // Target surface — pixel pointer is swapped on frame change
    struct ImageSurface* surface;
} GifAnimation;

// Create a GifAnimation from decoded frames and register with scheduler.
// Takes ownership of gif_frames (freed on destroy).
// Returns the animation instance, or NULL on failure.
AnimationInstance* gif_animation_create(AnimationScheduler* scheduler,
                                         struct ImageSurface* surface,
                                         GifFrames* gif_frames,
                                         double start_time,
                                         Pool* pool);

// Tick callback for GIF animation (called by scheduler).
void gif_animation_tick(AnimationInstance* anim, float t);

// Finish callback for GIF animation.
void gif_animation_finish(AnimationInstance* anim);

// Check if an image source (file path or URL) is an animated GIF (>1 frame).
// If so, loads all frames and returns the GifFrames*. Returns NULL if static.
GifFrames* gif_detect_animated(const char* path);

// Check if in-memory image data is an animated GIF (>1 frame).
// If so, loads all frames and returns the GifFrames*. Returns NULL if static.
GifFrames* gif_detect_animated_from_memory(const unsigned char* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_GIF_PLAYER_H
