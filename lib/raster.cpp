#include "raster.h"
#include "image_process.h"
#include "mem.h"
#include <string.h>
#include <math.h>

RasterBuffer* raster_buffer_create(uint32_t width, uint32_t height, uint8_t channels,
        size_t byte_limit) {
    if (!width || !height || !channels || channels > 4 ||
            width > SIZE_MAX / height || (size_t)width * height > byte_limit / channels)
        return nullptr;
    RasterBuffer* buffer = (RasterBuffer*)mem_calloc(1, sizeof(RasterBuffer), MEM_CAT_CONTAINER);
    if (!buffer) return nullptr;
    buffer->size = (size_t)width * height * channels;
    buffer->storage = byte_storage_alloc(buffer->size, MEM_CAT_CONTAINER);
    if (!buffer->storage) { mem_free(buffer); return nullptr; }
    buffer->pixels = buffer->storage->data;
    memset(buffer->pixels, 0, buffer->size);
    buffer->width = width;
    buffer->height = height;
    buffer->channels = channels;
    return buffer;
}

void raster_buffer_destroy(RasterBuffer* buffer) {
    if (!buffer) return;
    byte_storage_release(buffer->storage);
    for (size_t i = 0; buffer->tiles && i < (size_t)buffer->columns * buffer->rows; i++)
        byte_storage_release(buffer->tiles[i].storage);
    mem_free(buffer->tiles);
    mem_free(buffer);
}

RasterBuffer* raster_buffer_sparse(uint32_t width, uint32_t height, uint8_t channels, size_t limit) {
    if (!width || !height || !channels || channels > 4 || width > SIZE_MAX / height ||
            (size_t)width * height > limit / channels) return nullptr;
    RasterBuffer* result = (RasterBuffer*)mem_calloc(1, sizeof(RasterBuffer), MEM_CAT_CONTAINER);
    if (!result) return nullptr;
    result->width = width; result->height = height; result->channels = channels;
    result->size = (size_t)width * height * channels;
    result->columns = (width - 1) / RASTER_TILE_SIDE + 1;
    result->rows = (height - 1) / RASTER_TILE_SIDE + 1;
    result->tiles = (RasterTile*)mem_calloc((size_t)result->columns * result->rows, sizeof(RasterTile), MEM_CAT_CONTAINER);
    if (!result->tiles) { mem_free(result); return nullptr; }
    return result;
}

RasterBuffer* raster_buffer_fork(const RasterBuffer* source) {
    if (!source) return nullptr;
    RasterBuffer* result = raster_buffer_sparse(source->width, source->height, source->channels, source->size);
    if (!result) return nullptr;
    for (uint32_t y = 0; y < result->rows; y++) for (uint32_t x = 0; x < result->columns; x++) {
        RasterTile& tile = result->tiles[(size_t)y * result->columns + x];
        if (source->pixels) {
            tile.storage = byte_storage_retain(source->storage);
            tile.offset = ((size_t)y * RASTER_TILE_SIDE * source->width + x * RASTER_TILE_SIDE) * source->channels;
            tile.stride = (size_t)source->width * source->channels;
        } else {
            tile = source->tiles[(size_t)y * source->columns + x];
            byte_storage_retain(tile.storage);
        }
    }
    return result;
}

const uint8_t* raster_buffer_pixel(const RasterBuffer* buffer, uint32_t x, uint32_t y) {
    static const uint8_t zero[4] = {};
    if (!buffer || x >= buffer->width || y >= buffer->height) return zero;
    if (buffer->pixels) return buffer->pixels + ((size_t)y * buffer->width + x) * buffer->channels;
    const RasterTile& tile = buffer->tiles[(size_t)(y / RASTER_TILE_SIDE) * buffer->columns + x / RASTER_TILE_SIDE];
    return tile.storage ? tile.storage->data + tile.offset + (y % RASTER_TILE_SIDE) * tile.stride +
        (x % RASTER_TILE_SIDE) * buffer->channels : zero;
}

uint8_t* raster_buffer_edit_pixel(RasterBuffer* buffer, uint32_t x, uint32_t y) {
    if (!buffer || x >= buffer->width || y >= buffer->height) return nullptr;
    if (buffer->pixels) {
        if (byte_storage_ref_count(buffer->storage) != 1 || (buffer->storage->flags & BYTE_STORAGE_FLAG_READ_ONLY)) return nullptr;
        return buffer->pixels + ((size_t)y * buffer->width + x) * buffer->channels;
    }
    uint32_t column = x / RASTER_TILE_SIDE, row = y / RASTER_TILE_SIDE;
    uint32_t left = column * RASTER_TILE_SIDE, top = row * RASTER_TILE_SIDE;
    uint32_t width = buffer->width - left, height = buffer->height - top;
    if (width > RASTER_TILE_SIDE) width = RASTER_TILE_SIDE;
    if (height > RASTER_TILE_SIDE) height = RASTER_TILE_SIDE;
    RasterTile& tile = buffer->tiles[(size_t)row * buffer->columns + column];
    if (!tile.storage || byte_storage_ref_count(tile.storage) != 1 || tile.offset ||
            tile.stride != (size_t)width * buffer->channels || (tile.storage->flags & BYTE_STORAGE_FLAG_READ_ONLY)) {
        ByteStorage* replacement = byte_storage_alloc((size_t)width * height * buffer->channels, MEM_CAT_CONTAINER);
        if (!replacement) return nullptr;
        // old dense spans and untouched tiles stay shared; only this tile detaches.
        if (tile.storage) for (uint32_t v = 0; v < height; v++)
            memcpy(replacement->data + (size_t)v * width * buffer->channels,
                tile.storage->data + tile.offset + v * tile.stride, (size_t)width * buffer->channels);
        else memset(replacement->data, 0, replacement->capacity);
        byte_storage_release(tile.storage);
        tile = {replacement, 0, (size_t)width * buffer->channels};
    }
    return tile.storage->data + (y - top) * tile.stride + (x - left) * buffer->channels;
}

bool raster_buffer_samples_equal(const RasterBuffer* a,const RasterBuffer* b) {
    if (!a || !b || a->width!=b->width || a->height!=b->height || a->channels!=b->channels) return false;
    if (a==b) return true;
    for (uint32_t y=0;y<a->height;y++) for (uint32_t x=0;x<a->width;x++) {
        const uint8_t* left=raster_buffer_pixel(a,x,y), *right=raster_buffer_pixel(b,x,y);
        if (left!=right && memcmp(left,right,a->channels)) return false;
    }
    return true;
}

size_t raster_buffer_index_bytes(const RasterBuffer* buffer) {
    return sizeof(RasterBuffer) + (buffer->tiles ? (size_t)buffer->columns * buffer->rows * sizeof(RasterTile) : 0);
}

bool raster_buffer_stores(RasterBuffer* buffer, bool (*visit)(ByteStorage*, void*), void* context) {
    if (buffer->storage && !visit(buffer->storage, context)) return false;
    for (size_t i = 0; buffer->tiles && i < (size_t)buffer->columns * buffer->rows; i++)
        if (buffer->tiles[i].storage && !visit(buffer->tiles[i].storage, context)) return false;
    return true;
}

bool raster_buffer_copy_region(const RasterBuffer* buffer, uint32_t left, uint32_t top,
        uint32_t width, uint32_t height, int channel, uint8_t* output, size_t capacity) {
    if (!buffer || !output || !width || !height || left > buffer->width ||
            top > buffer->height || width > buffer->width - left ||
            height > buffer->height - top || channel < -1 || channel >= buffer->channels)
        return false;
    size_t channels = channel < 0 ? buffer->channels : 1;
    if ((size_t)width * height > capacity / channels) return false;
    for (uint32_t y = 0; y < height; y++) {
        if (buffer->pixels && channel < 0)
            memcpy(output + (size_t)y * width * channels, raster_buffer_pixel(buffer, left, top + y), (size_t)width * channels);
        else for (uint32_t x = 0; x < width; x++) {
            const uint8_t* pixel = raster_buffer_pixel(buffer, left + x, top + y);
            if (channel < 0) memcpy(output + ((size_t)y * width + x) * channels, pixel, channels);
            else output[(size_t)y * width + x] = pixel[channel];
        }
    }
    return true;
}

double raster_linear_sample(uint8_t sample) {
    double n = sample / 255.0;
    return n <= 0.04045 ? n / 12.92 : pow((n + 0.055) / 1.055, 2.4);
}

uint8_t raster_encoded_sample(double n) {
    n = n <= 0.0031308 ? 12.92 * n : 1.055 * pow(n, 1.0 / 2.4) - 0.055;
    return (uint8_t)lround(fmax(0.0, fmin(1.0, n)) * 255);
}

void raster_mix_pixel(uint8_t* d, const uint8_t* s, uint8_t channels, double coverage, bool erase) {
    if (coverage <= 0) return;
    if (channels == 1) { d[0] = (uint8_t)lround(d[0] * (1 - coverage) + (erase ? 0 : s[0]) * coverage); return; }
    double sa = coverage * s[3] / 255.0, da = d[3] / 255.0;
    if (erase) { d[3] = (uint8_t)lround(d[3] * (1 - coverage)); return; }
    if (sa <= 0) return;
    if (sa >= 1) { memcpy(d, s, 4); return; }
    double alpha = sa + da * (1 - sa);
    for (uint8_t c = 0; c < 3; c++) d[c] = raster_encoded_sample(
        (raster_linear_sample(s[c]) * sa + raster_linear_sample(d[c]) * da * (1 - sa)) / alpha);
    d[3] = (uint8_t)lround(alpha * 255);
}

static void raster_mix_blend_mode(uint8_t* d, const uint8_t* s, double coverage, RasterBlend mode, bool atop) {
    if (mode == RASTER_NORMAL && !atop) { raster_mix_pixel(d, s, 4, coverage, false); return; }
    double sa = coverage * s[3] / 255.0, da = d[3] / 255.0;
    if (sa <= 0 || (atop && da<=0)) return;
    double alpha = atop ? da : sa + da * (1-sa);
    for (int c=0; c<3; c++) {
        double a = raster_linear_sample(d[c]), b = raster_linear_sample(s[c]), blend = b;
        switch (mode) {
        case RASTER_MULTIPLY: blend = a*b; break;
        case RASTER_SCREEN: blend = a+b-a*b; break;
        case RASTER_OVERLAY: blend = a<=0.5 ? 2*a*b : 1-2*(1-a)*(1-b); break;
        case RASTER_DARKEN: blend = fmin(a,b); break;
        case RASTER_LIGHTEN: blend = fmax(a,b); break;
        case RASTER_DIFFERENCE: blend = fabs(a-b); break;
        case RASTER_EXCLUSION: blend = a+b-2*a*b; break;
        default: break;
        }
        d[c] = raster_encoded_sample(atop ? (1-sa)*a+sa*blend :
            ((1-sa)*da*a + (1-da)*sa*b + da*sa*blend)/alpha);
    }
    d[3] = (uint8_t)lround(alpha*255);
}

void raster_mix_blend(uint8_t* d, const uint8_t* s, double coverage, RasterBlend mode) {
    raster_mix_blend_mode(d,s,coverage,mode,false);
}

void raster_rgba_pixel(const RasterBuffer* source, uint32_t x, uint32_t y, uint8_t* rgba) {
    const uint8_t* s = raster_buffer_pixel(source,x,y);
    rgba[0]=s[0]; rgba[1]=s[source->channels<=2 ? 0 : 1]; rgba[2]=s[source->channels<=2 ? 0 : 2];
    rgba[3]=source->channels==2 || source->channels==4 ? s[source->channels-1] : 255;
}

static double raster_mask_sample(const RasterBuffer* mask, int64_t x, int64_t y, double outside) {
    return x>=0 && y>=0 && x<mask->width && y<mask->height
        ? raster_buffer_pixel(mask,(uint32_t)x,(uint32_t)y)[0]/255.0 : outside;
}

bool raster_buffer_blend(RasterBuffer* destination, const RasterBuffer* source,
        double left, double top, const double* transform, RasterBlend mode, double opacity,
        const RasterBuffer* mask, double mask_left, double mask_top, double outside, bool inverse, bool atop) {
    if (!destination || destination->channels!=4 || !source) return false;
    const double identity[]={1,0,0,1,0,0};
    const double* m = transform ? transform : identity;
    double det=m[0]*m[3]-m[1]*m[2];
    if (!isfinite(det) || fabs(det)<1e-12) return false;
    double min_x=INFINITY,min_y=INFINITY,max_x=-INFINITY,max_y=-INFINITY;
    for (int i=0;i<4;i++) {
        double x=(i&1) ? source->width : 0, y=(i&2) ? source->height : 0;
        double tx=m[0]*x+m[2]*y+m[4]+left, ty=m[1]*x+m[3]*y+m[5]+top;
        min_x=fmin(min_x,tx); max_x=fmax(max_x,tx); min_y=fmin(min_y,ty); max_y=fmax(max_y,ty);
    }
    int64_t x0=(int64_t)fmax(0,fmin(destination->width,floor(min_x)));
    int64_t y0=(int64_t)fmax(0,fmin(destination->height,floor(min_y)));
    int64_t x1=(int64_t)fmax(0,fmin(destination->width,ceil(max_x)));
    int64_t y1=(int64_t)fmax(0,fmin(destination->height,ceil(max_y)));
    // integral translations need exact sample reads, without inverse affine interpolation.
    bool aligned=m[0]==1 && m[1]==0 && m[2]==0 && m[3]==1 &&
        floor(left+m[4])==left+m[4] && floor(top+m[5])==top+m[5];
    bool aligned_mask=aligned && floor(mask_left)==mask_left && floor(mask_top)==mask_top;
    for (int64_t y=y0;y<y1;y++) for (int64_t x=x0;x<x1;x++) {
        double sx,sy; uint8_t rgba[4]={};
        if (aligned) {
            sx=x-left-m[4]; sy=y-top-m[5];
            raster_rgba_pixel(source,(uint32_t)sx,(uint32_t)sy,rgba);
        } else {
            double dx=x+0.5-left-m[4],dy=y+0.5-top-m[5];
            sx=(m[3]*dx-m[2]*dy)/det; sy=(-m[1]*dx+m[0]*dy)/det;
            if (sx<0 || sy<0 || sx>=source->width || sy>=source->height) continue;
            sx-=0.5; sy-=0.5;
            int64_t ix=(int64_t)floor(sx),iy=(int64_t)floor(sy);
            double fx=sx-ix,fy=sy-iy,alpha=0,colors[3]={};
            // interpolate premultiplied samples so hidden RGB cannot bleed across transparent edges.
            for (int j=0;j<4;j++) {
                double weight=((j&1)?fx:1-fx)*((j&2)?fy:1-fy);
                if (weight==0) continue;
                uint8_t pixel[4];
                raster_rgba_pixel(source,(uint32_t)image_edge_index(ix+(j&1),source->width),
                    (uint32_t)image_edge_index(iy+((j>>1)&1),source->height),pixel);
                double a=pixel[3]/255.0; alpha+=weight*a;
                for (int c=0;c<3;c++) colors[c]+=weight*a*pixel[c];
            }
            rgba[3]=(uint8_t)lround(alpha*255);
            if (alpha>0) for (int c=0;c<3;c++) rgba[c]=(uint8_t)lround(colors[c]/alpha);
        }
        double coverage=1;
        if (mask) {
            double mx=sx-mask_left,my=sy-mask_top;
            int64_t bx=(int64_t)floor(mx),by=(int64_t)floor(my);
            coverage=aligned_mask ? raster_mask_sample(mask,bx,by,outside) :
                image_bilinear_mix(raster_mask_sample(mask,bx,by,outside),
                raster_mask_sample(mask,bx+1,by,outside),raster_mask_sample(mask,bx,by+1,outside),
                raster_mask_sample(mask,bx+1,by+1,outside),mx-bx,my-by);
            if (inverse) coverage=1-coverage;
        }
        raster_mix_blend_mode(destination->pixels+((size_t)y*destination->width+x)*4,rgba,opacity*coverage,mode,atop);
    }
    return true;
}

void raster_buffer_over(RasterBuffer* destination, const RasterBuffer* source,
        int64_t left, int64_t top, double opacity, const RasterBuffer* mask,
        int64_t mask_left, int64_t mask_top, double outside, bool inverse) {
    raster_buffer_blend(destination,source,left,top,nullptr,RASTER_NORMAL,opacity,mask,
        mask_left-left,mask_top-top,outside,inverse,false);
}

void raster_buffer_crossfade(RasterBuffer* destination, const RasterBuffer* source, double opacity,
        const RasterBuffer* mask, int64_t left, int64_t top, double outside, bool inverse, bool atop) {
    for (uint32_t y=0;y<destination->height;y++) for (uint32_t x=0;x<destination->width;x++) {
        double coverage=mask ? raster_mask_sample(mask,(int64_t)x-left,(int64_t)y-top,outside) : 1;
        if (mask && inverse) coverage=1-coverage;
        double f=opacity*coverage;
        if (f<=0) continue;
        uint8_t* d=destination->pixels+((size_t)y*destination->width+x)*4;
        const uint8_t* s=raster_buffer_pixel(source,x,y);
        if (!memcmp(d,s,4) || (atop && !d[3])) continue;
        double da=d[3]/255.0,sa=s[3]/255.0,a=atop ? da : da*(1-f)+sa*f;
        for (int c=0;c<3;c++) d[c]=a>0 ? raster_encoded_sample(atop ?
            raster_linear_sample(d[c])*(1-f)+raster_linear_sample(s[c])*f :
            (raster_linear_sample(d[c])*da*(1-f)+raster_linear_sample(s[c])*sa*f)/a) : 0;
        d[3]=(uint8_t)lround(a*255);
    }
}
