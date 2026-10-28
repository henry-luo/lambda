#pragma once
#include <string.h>

// one vocabulary serves native drawing, asset conversion and topology diagnostics.
enum Scene3dTopology { SCENE_TRIANGLES, SCENE_POINTS, SCENE_SPRITE, SCENE_LINE, SCENE_SEGMENTS, SCENE_TOPOLOGY_COUNT };
static const char* const scene3d_drawable_tags[SCENE_TOPOLOGY_COUNT]={"mesh","points","sprite","line","line-segments"};
static inline int scene3d_drawable_kind(const char* tag) {
    if(tag) for(unsigned i=0;i<SCENE_TOPOLOGY_COUNT;i++) if(!strcmp(tag,scene3d_drawable_tags[i])) return (int)i;
    return -1;
}
