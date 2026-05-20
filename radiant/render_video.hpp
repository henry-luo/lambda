#pragma once

struct DisplayList;
struct DocState;
struct ImageSurface;
struct UiContext;

void render_video_frames(DisplayList* dl, ImageSurface* surface, DocState* rstate, UiContext* uicon);
void render_video_frames_cached(DocState* rstate, ImageSurface* surface, UiContext* uicon);
