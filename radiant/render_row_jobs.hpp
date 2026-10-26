#pragma once

#include "render.hpp"

// shared scanline work joins before its stack closure is released; nested worker calls run serially
template <typename Rows>
inline void render_parallel_rows(int width, int height, Rows& rows) {
    if (width > 0 && height > 0 && (size_t)width * (size_t)height >= (size_t)TILE_SIZE_CSS * TILE_SIZE_CSS) {
        struct Range { Rows* rows; int begin, end; };
        Range ranges[8];
        TileJob jobs[8] = {};
        int count = height < 8 ? height : 8;
        for (int i = 0; i < count; i++) {
            int begin = (int)((int64_t)height * i / count); // INT_CAST_OK: bounded raster row index
            int end = (int)((int64_t)height * (i + 1) / count); // INT_CAST_OK: bounded raster row index
            ranges[i] = {&rows, begin, end};
            jobs[i].work_context = &ranges[i];
            jobs[i].work = [](void* argument) {
                auto* range = (Range*)argument;
                (*range->rows)(range->begin, range->end);
            };
        }
        if (render_output_dispatch_jobs(jobs, count) > 0) return;
    }
    rows(0, height);
}

