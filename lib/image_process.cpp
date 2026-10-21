#include "image_process.h"
#include "mem.h"
#include "math_utils.h"
#include <math.h>
#include <string.h>

static bool image_cancelled(const bool* cancelled) {
    return cancelled && __atomic_load_n(cancelled, __ATOMIC_RELAXED);
}
static double image_clip(double value) {
    // preserve the reference's rounding at the final RGBA8 boundary.
    return (fabs(value)-fabs(value-1.0)+1.0)*0.5;
}

// a rolling horizontal sum and vertical ring turn a k*k scan into linear work.
bool image_box_filter(int64_t width, int64_t height, int64_t channels, int64_t kernel,
    ImageReadRow read, ImageWriteRow write, void* user, const bool* cancelled) {
    if (width <= 0 || height <= 0 || channels <= 0 || width > INT64_MAX/channels) return false;
    if (kernel < 1) kernel = 1;
    size_t row_count = (size_t)(width*channels);
    if (row_count > SIZE_MAX/(sizeof(double)*5)) return false;
    int64_t ring_rows = kernel < height ? kernel : height;
    // large generic kernels use recomputation rather than unbounded ring storage.
    bool ring = (uint64_t)ring_rows <= (32u*1024u*1024u)/(row_count*sizeof(double));
    size_t rows = ring ? (size_t)ring_rows : 1;
    if (rows > SIZE_MAX/(row_count*sizeof(double))-4) return false;
    double* storage = (double*)mem_calloc(rows+4, row_count*sizeof(double), MEM_CAT_TEMP);
    if (!storage) return false;
    double* input = storage + rows*row_count;
    double* sums = input + row_count;
    double* output = sums + row_count;
    int64_t* nonzero=(int64_t*)(output+row_count);
    int64_t pad = kernel/2, last = kernel-pad-1;
    auto horizontal = [&](int64_t y) -> double* {
        double* row = storage + (ring ? (size_t)(y%ring_rows)*row_count : 0);
        if (!read(user,y,input)) return nullptr;
        for (int64_t c=0; c<channels; c++) {
            double sum = input[c]*(double)(pad+1);
            int64_t count=input[c]!=0 ? pad+1 : 0;
            int64_t end = last < width-1 ? last : width-1;
            for (int64_t x=1; x<=end; x++) { sum += input[x*channels+c]; count+=input[x*channels+c]!=0; }
            if (last > width-1) {
                sum += input[(width-1)*channels+c]*(double)(last-(width-1));
                if (input[(width-1)*channels+c]!=0) count+=last-(width-1);
            }
            for (int64_t x=0; x<width; x++) {
                // empty windows must become exact zero after nonzero samples depart.
                if (!count) sum=0;
                row[x*channels+c] = sum;
                double entering=input[image_edge_index(x+last+1,width)*channels+c];
                double leaving=input[image_edge_index(x-pad,width)*channels+c];
                // subtract first: forming entering-leaving can overflow for a size-one window.
                sum-=leaving;
                sum+=entering;
                count+=(entering!=0)-(leaving!=0);
            }
        }
        return row;
    };
    bool ok = true;
    int64_t end = last < height-1 ? last : height-1;
    for (int64_t y=0; y<=end && ok; y++) {
        if (image_cancelled(cancelled)) { ok=false; break; }
        double* row = horizontal(y);
        if (!row) { ok=false; break; }
        int64_t weight = y == 0 ? pad+1 : 1;
        if (y == height-1 && last > height-1) weight += last-(height-1);
        for (size_t x=0; x<row_count; x++) { sums[x] += row[x]*(double)weight; if (row[x]!=0) nonzero[x]+=weight; }
    }
    double divisor = (double)kernel*(double)kernel;
    for (int64_t y=0; y<height && ok; y++) {
        if (image_cancelled(cancelled)) { ok=false; break; }
        for (size_t x=0; x<row_count; x++) output[x] = nonzero[x] ? sums[x]/divisor : 0;
        if (!write(user,y,output)) { ok=false; break; }
        if (y == height-1) break;
        int64_t leaving = image_edge_index(y-pad,height), entering = image_edge_index(y+last+1,height);
        double* row = ring ? storage+(size_t)(leaving%ring_rows)*row_count : horizontal(leaving);
        if (!row) { ok=false; break; }
        for (size_t x=0; x<row_count; x++) { sums[x] -= row[x]; nonzero[x]-=row[x]!=0; }
        // repeated edge rows already occupy the ring; new rows replace the departing slot.
        row = ring && y+last+1 >= height ? storage+(size_t)((height-1)%ring_rows)*row_count : horizontal(entering);
        if (!row) { ok=false; break; }
        for (size_t x=0; x<row_count; x++) {
            if (!nonzero[x]) sums[x]=0;
            sums[x] += row[x]; nonzero[x]+=row[x]!=0;
        }
    }
    mem_free(storage);
    return ok;
}

struct ImageCacheEntry {
    ImageProcessOptions key;
    uint64_t source = 0, stamp = 0;
    double* pixels = nullptr;
    size_t bytes = 0;
    bool ready = false;
};
struct ImageProcessCache {
    ImageCacheEntry entries[3];
    size_t budget = 0, bytes = 0;
    uint64_t stamp = 0;
};
ImageProcessCache* image_process_cache_create(size_t budget) {
    auto* cache = (ImageProcessCache*)mem_calloc(1,sizeof(ImageProcessCache),MEM_CAT_TEMP);
    if (cache) cache->budget = budget;
    return cache;
}
void image_process_cache_destroy(ImageProcessCache* cache) {
    if (!cache) return;
    for (auto& entry : cache->entries) mem_free(entry.pixels);
    mem_free(cache);
}
static void image_cache_drop(ImageProcessCache* cache, int index) {
    auto& entry = cache->entries[index];
    cache->bytes -= entry.bytes;
    mem_free(entry.pixels);
    entry.pixels=nullptr; entry.bytes=0; entry.ready=false;
}
static ImageProcessOptions image_cache_key(const ImageProcessOptions& options, int stage) {
    ImageProcessOptions key = options;
    key.output_width=0; key.output_height=0; key.vignette=0; key.sharpen=0; key.softness=0;
    if (stage == 0) {
        ImageProcessOptions geometry;
        geometry.turns=key.turns; geometry.flip_x=key.flip_x; geometry.flip_y=key.flip_y;
        geometry.angle=key.angle; geometry.uncropped=key.uncropped;
        memcpy(geometry.crop,key.crop,sizeof(key.crop));
        key=geometry;
    }
    // compare affects color, but never geometry. Blur depends on its rounded support.
    if (stage == 2) key.softness=options.softness;
    return key;
}
static bool image_cache_equal(const ImageProcessOptions& a, const ImageProcessOptions& b) {
    // compare fields, excluding padding between the boolean and numeric members.
    return a.turns==b.turns && a.flip_x==b.flip_x && a.flip_y==b.flip_y &&
        a.compare==b.compare && a.uncropped==b.uncropped &&
        memcmp(&a.angle,&b.angle,offsetof(ImageProcessOptions,output_width)-offsetof(ImageProcessOptions,angle))==0;
}
static bool image_cache_matches(const ImageCacheEntry& entry, uint64_t source,
    const ImageProcessOptions& key, size_t bytes) {
    return entry.pixels && entry.ready && entry.source==source && entry.bytes==bytes && image_cache_equal(entry.key,key);
}
static double* image_cache_get(ImageProcessCache* cache, int stage, uint64_t source,
    const ImageProcessOptions& options, size_t bytes, bool* hit) {
    *hit=false;
    if (!cache) return nullptr;
    if (bytes > cache->budget) { image_cache_drop(cache,stage); return nullptr; }
    auto key=image_cache_key(options,stage);
    auto& entry=cache->entries[stage];
    if (image_cache_matches(entry,source,key,bytes)) {
        *hit=true; entry.stamp=++cache->stamp; return entry.pixels;
    }
    image_cache_drop(cache,stage);
    while (cache->bytes+bytes > cache->budget) {
        int oldest=-1;
        for (int i=0;i<3;i++) if (cache->entries[i].pixels &&
            (oldest<0 || cache->entries[i].stamp < cache->entries[oldest].stamp)) oldest=i;
        if (oldest<0) return nullptr;
        image_cache_drop(cache,oldest);
    }
    entry.pixels=(double*)mem_alloc(bytes,MEM_CAT_TEMP);
    if (!entry.pixels) return nullptr;
    entry.key=key; entry.source=source; entry.bytes=bytes; entry.stamp=++cache->stamp;
    cache->bytes+=bytes;
    return entry.pixels;
}

struct ImageProcessRun {
    const ImageProcessSource* source;
    const ImageProcessOptions* options;
    ImageProcessCache* cache;
    ImageProcessResult* result;
    const bool* cancelled;
    int width, height, oriented_width, oriented_height, left, top, next_output=0;
    double cosine, sine, exposure, exponent;
    double* previous;
    double* row;
    bool color_identity;
    bool has_curves;
    uint8_t curve_segments[IMAGE_TONE_CHANNELS][256];
};
bool image_process_tone_valid(const ImageProcessOptions& options) {
    if (!isfinite(options.output_black) || !isfinite(options.output_white) ||
        options.output_black<0 || options.output_white>1 || options.output_white<options.output_black) return false;
    for (int channel=0;channel<IMAGE_TONE_CHANNELS;channel++) {
        uint64_t count=options.curve_counts[channel];
        if (count==0) continue;
        if (count<2 || count>IMAGE_TONE_POINTS || options.curves[channel][0][0]!=0 ||
            options.curves[channel][count-1][0]!=1) return false;
        for (uint64_t point=0;point<count;point++) {
            const double* p=options.curves[channel][point];
            if (!isfinite(p[0]) || !isfinite(p[1]) || p[0]<0 || p[0]>1 || p[1]<0 || p[1]>1 ||
                (point>0 && p[0]<=options.curves[channel][point-1][0])) return false;
        }
    }
    return true;
}
static void image_prepare_curves(ImageProcessRun& run) {
    for (int channel=0;channel<IMAGE_TONE_CHANNELS;channel++) {
        uint64_t count=run.options->curve_counts[channel], segment=0;
        if (count==0) continue;
        run.has_curves=true;
        for (int bucket=0;bucket<256;bucket++) {
            double x=bucket/255.0;
            while (segment+2<count && run.options->curves[channel][segment+1][0]<=x) segment++;
            run.curve_segments[channel][bucket]=(uint8_t)segment;
        }
    }
}
static double image_curve_value(const ImageProcessRun& run, int channel, double value) {
    uint64_t count=run.options->curve_counts[channel];
    if (count==0) return value;
    int bucket=(int)(value*255.0);
    uint64_t segment=run.curve_segments[channel][bucket];
    const double (*points)[2]=run.options->curves[channel];
    // bucket lookup accelerates search without approximating narrow or nonmonotone curves.
    while (segment>0 && points[segment][0]>value) segment--;
    while (segment+2<count && points[segment+1][0]<=value) segment++;
    const double* a=points[segment];
    const double* b=points[segment+1];
    return a[1]+(b[1]-a[1])*((value-a[0])/(b[0]-a[0]));
}
static void image_oriented_pixel(const ImageProcessRun& run, int y, int x, double* rgba) {
    auto& source=*run.source;
    auto& options=*run.options;
    if (options.flip_x) x=run.oriented_width-1-x;
    if (options.flip_y) y=run.oriented_height-1-y;
    int sy=y, sx=x;
    switch (options.turns) {
        case 1: sy=x; sx=source.width-1-y; break;
        case 2: sy=source.height-1-y; sx=source.width-1-x; break;
        case 3: sy=source.height-1-x; sx=y; break;
    }
    int64_t offset=sy*source.strides[0]+sx*source.strides[1];
    for (int c=0;c<4;c++) rgba[c]=source.pixels[offset+c*source.strides[2]]*(1.0/255.0);
}
static void image_geometry_pixel(const ImageProcessRun& run, int y, int x, double* rgba) {
    if (run.cache && run.cache->entries[0].ready) {
        memcpy(rgba,run.cache->entries[0].pixels+((size_t)y*run.width+x)*4,4*sizeof(double)); return;
    }
    y+=run.top; x+=run.left;
    if (run.options->angle == 0) { image_oriented_pixel(run,y,x,rgba); return; }
    double cy=(run.oriented_height-1)*0.5, cx=(run.oriented_width-1)*0.5;
    double sy=cy+(y-cy)*run.cosine+(x-cx)*run.sine;
    double sx=cx-(y-cy)*run.sine+(x-cx)*run.cosine;
    memset(rgba,0,4*sizeof(double));
    if (sy < -0.5 || sy > run.oriented_height-0.5 || sx < -0.5 || sx > run.oriented_width-0.5) return;
    int64_t iy=(int64_t)floor(sy), ix=(int64_t)floor(sx);
    double fy=sy-iy, fx=sx-ix;
    double samples[4][4];
    for (int i=0;i<4;i++) {
        image_oriented_pixel(run,(int)image_edge_index(iy+i/2,run.oriented_height),
            (int)image_edge_index(ix+i%2,run.oriented_width),samples[i]);
        for (int c=0;c<3;c++) samples[i][c]*=samples[i][3];
    }
    for (int c=0;c<4;c++) rgba[c]=image_bilinear_mix(samples[0][c],samples[1][c],samples[2][c],samples[3][c],fx,fy);
    for (int c=0;c<3;c++) rgba[c]=rgba[3] == 0 ? 0 : rgba[c]/rgba[3];
}
static void image_color_pixel(const ImageProcessRun& run, int y, int x, double* rgba) {
    if (run.cache && run.cache->entries[1].ready) {
        memcpy(rgba,run.cache->entries[1].pixels+((size_t)y*run.width+x)*4,4*sizeof(double)); return;
    }
    image_geometry_pixel(run,y,x,rgba);
    if (run.color_identity) return;
    auto& o=*run.options;
    double rgb[3];
    for (int c=0;c<3;c++) {
        double value=image_clip((rgba[c]-o.black)/(o.white-o.black));
        value=value+o.shadows*(1-value)*(1-value)*0.3+o.highlights*value*value*0.3;
        rgb[c]=(value*run.exposure+o.brightness-0.5)*o.contrast+0.5;
    }
    double luma=rgb[0]*0.299+rgb[1]*0.587+rgb[2]*0.114;
    for (int c=0;c<3;c++) {
        rgb[c]=luma+(rgb[c]-luma)*o.saturation+(c == 0 ? o.temperature*0.1 : c == 2 ? -o.temperature*0.1 : 0);
        if (o.gamma != 1) rgb[c]=pow((rgb[c]+fabs(rgb[c]))*0.5,run.exponent);
    }
    for (int c=0;c<3;c++) {
        const double* matrix=o.matrix+c*4;
        double preset=rgb[0]*matrix[0]+rgb[1]*matrix[1]+rgb[2]*matrix[2]+matrix[3];
        double value=image_clip(rgb[c]*(1-o.intensity)+preset*o.intensity);
        if (run.has_curves) value=image_curve_value(run,c+1,image_curve_value(run,0,value));
        rgba[c]=image_clip(o.output_black+value*(o.output_white-o.output_black));
    }
}
static bool image_color_row(void* opaque, int64_t y, double* row) {
    auto& run=*(ImageProcessRun*)opaque;
    if (image_cancelled(run.cancelled)) return false;
    for (int x=0;x<run.width;x++) {
        image_color_pixel(run,(int)y,x,row+x*4);
        for (int c=0;c<3;c++) row[x*4+c]*=row[x*4+3];
    }
    return true;
}
static uint8_t image_byte(double value) {
    return (uint8_t)round(fmax(0.0,fmin(255.0,value*255.0)));
}
static bool image_output_row(ImageProcessRun& run, int y, const double* row) {
    auto& result=*run.result;
    bool resize=result.width != run.width || result.height != run.height;
    if (!resize) {
        for (int x=0;x<run.width;x++) for (int c=0;c<4;c++)
            result.pixels[((size_t)y*run.width+x)*4+c]=image_byte(row[x*4+c]);
        return true;
    }
    // only adjacent rows are needed for bilinear output, including upsampling.
    while (run.next_output < result.height) {
        double sy=(run.next_output+0.5)*run.height/result.height-0.5;
        int64_t iy=(int64_t)floor(sy);
        int lo=(int)image_edge_index(iy,run.height), hi=(int)image_edge_index(iy+1,run.height);
        if (hi > y) break;
        const double* rows[2]={lo == y ? row : run.previous, row};
        double fy=sy-iy;
        for (int x=0;x<result.width;x++) {
            double sx=(x+0.5)*run.width/result.width-0.5;
            int64_t ix=(int64_t)floor(sx);
            int xs[2]={(int)image_edge_index(ix,run.width),(int)image_edge_index(ix+1,run.width)};
            double fx=sx-ix, rgba[4]={};
            const double* samples[]={rows[0]+xs[0]*4,rows[0]+xs[1]*4,rows[1]+xs[0]*4,rows[1]+xs[1]*4};
            for (int c=0;c<4;c++) {
                double values[4];
                for (int i=0;i<4;i++) values[i]=c==3 ? samples[i][c] : samples[i][c]*samples[i][3];
                rgba[c]=image_bilinear_mix(values[0],values[1],values[2],values[3],fx,fy);
            }
            for (int c=0;c<3;c++) rgba[c]=rgba[3] == 0 ? 0 : rgba[c]/rgba[3];
            for (int c=0;c<4;c++) result.pixels[((size_t)run.next_output*result.width+x)*4+c]=image_byte(rgba[c]);
        }
        run.next_output++;
    }
    memcpy(run.previous,row,(size_t)run.width*4*sizeof(double));
    return true;
}
static bool image_effect_row(void* opaque, int64_t y, const double* smooth) {
    auto& run=*(ImageProcessRun*)opaque;
    if (image_cancelled(run.cancelled)) return false;
    auto& o=*run.options;
    double vy=(y+0.5)/run.height*2-1;
    for (int x=0;x<run.width;x++) {
        double* rgba=run.row+x*4;
        image_color_pixel(run,(int)y,x,rgba);
        double vx=(x+0.5)/run.width*2-1;
        double shade=o.compare ? 1 : 1-image_clip((vx*vx+vy*vy)*0.65)*o.vignette;
        for (int c=0;c<3;c++) {
            double blurred=smooth ? (smooth[x*4+3] == 0 ? 0 : smooth[x*4+c]/smooth[x*4+3]) : rgba[c];
            if (!o.compare && (o.vignette != 0 || o.softness != 0 || o.sharpen != 0))
                rgba[c]=image_clip(((o.softness > 0 ? blurred : rgba[c])+(rgba[c]-blurred)*o.sharpen)*shade);
        }
    }
    if (smooth && run.cache && run.cache->entries[2].pixels && !run.cache->entries[2].ready)
        memcpy(run.cache->entries[2].pixels+(size_t)y*run.width*4,smooth,(size_t)run.width*4*sizeof(double));
    return image_output_row(run,(int)y,run.row);
}
static bool image_process_extent(ImageProcessRun& run) {
    auto& source=*run.source;
    auto& options=*run.options;
    auto* result=run.result;
    if (!source.pixels || source.width<1 || source.height<1 ||
        (int64_t)source.width*source.height>16000000 || !result) return false;
    run.oriented_width=options.turns%2 ? source.height : source.width;
    run.oriented_height=options.turns%2 ? source.width : source.height;
    run.left=options.uncropped ? 0 : (int)floor(options.crop[0]*run.oriented_width);
    run.top=options.uncropped ? 0 : (int)floor(options.crop[1]*run.oriented_height);
    run.width=options.uncropped ? run.oriented_width : (int)ceil(options.crop[2]*run.oriented_width)-run.left;
    run.height=options.uncropped ? run.oriented_height : (int)ceil(options.crop[3]*run.oriented_height)-run.top;
    if (run.width<1) run.width=1;
    if (run.height<1) run.height=1;
    result->width=options.output_width && !options.uncropped ? options.output_width : run.width;
    result->height=options.output_height && !options.uncropped ? options.output_height : run.height;
    return result->width>0 && result->height>0 && (int64_t)result->width*result->height<=16000000 &&
        (size_t)run.width*4*sizeof(double)<=16u*1024u*1024u;
}
bool image_process_dimensions(const ImageProcessSource& source, const ImageProcessOptions& options,
    int* width, int* height) {
    ImageProcessResult result;
    ImageProcessRun run={&source,&options,nullptr,&result,nullptr};
    if (!width || !height || !image_process_extent(run)) return false;
    *width=result.width; *height=result.height;
    return true;
}
bool image_process(const ImageProcessSource& source, const ImageProcessOptions& options,
    ImageProcessResult* result, ImageProcessCache* cache, const bool* cancelled,
    uint8_t* destination, size_t destination_bytes) {
    if (!result) return false;
    *result={};
    if (image_cancelled(cancelled) || !image_process_tone_valid(options)) return false;
    ImageProcessRun run={&source,&options,cache,result,cancelled};
    if (!image_process_extent(run)) return false;
    size_t output_bytes=(size_t)result->width*result->height*4;
    if (destination && destination_bytes<output_bytes) return false;
    double radians=math_degrees_to_radians_d(options.angle);
    run.cosine=cos(radians); run.sine=sin(radians);
    run.exposure=pow(2.0,options.exposure); run.exponent=1.0/options.gamma;
    image_prepare_curves(run);
    ImageProcessOptions neutral;
    run.color_identity=options.compare || (options.exposure==0 && options.brightness==0 && options.contrast==1 &&
        options.saturation==1 && options.temperature==0 && options.gamma==1 && options.black==0 && options.white==1 &&
        options.shadows==0 && options.highlights==0 && options.output_black==0 && options.output_white==1 &&
        !run.has_curves && (options.intensity==0 || memcmp(options.matrix,neutral.matrix,sizeof(options.matrix))==0));
    size_t row_bytes=(size_t)run.width*4*sizeof(double), cache_bytes=row_bytes*run.height;
    if (cache) for (int stage=0;stage<3;stage++) {
        auto key=image_cache_key(options,stage);
        if (!image_cache_matches(cache->entries[stage],source.revision,key,cache_bytes)) image_cache_drop(cache,stage);
    }
    bool filtered=!options.compare && (options.softness>0 || options.sharpen>0);
    bool cached_color=cache && cache->entries[1].ready;
    bool cached_blur=filtered && cache && cache->entries[2].ready;
    auto prepare_cache=[&](int stage) -> bool {
        bool hit=false;
        double* pixels=image_cache_get(cache,stage,source.revision,options,cache_bytes,&hit);
        if (hit) {
            if (stage==0) result->geometry_hits++;
            if (stage==1) result->color_hits++;
            if (stage==2) result->blur_hits++;
        } else if (pixels && stage<2) {
            for (int y=0;y<run.height;y++) {
                if (image_cancelled(cancelled)) return false;
                for (int x=0;x<run.width;x++) {
                    double* pixel=pixels+((size_t)y*run.width+x)*4;
                    if (stage==0) image_geometry_pixel(run,y,x,pixel); else image_color_pixel(run,y,x,pixel);
                }
            }
            cache->entries[stage].ready=true;
        }
        return true;
    };
    // downstream hits avoid rebuilding predecessors and evicting the very result being reused.
    if (cache && options.angle==0) image_cache_drop(cache,0);
    if ((options.angle!=0 && !cached_color && !cached_blur && !prepare_cache(0)) || !prepare_cache(1)) return false;
    if (filtered && !prepare_cache(2)) return false;
    result->owns_pixels=destination==nullptr;
    result->pixels=destination ? destination : (uint8_t*)mem_alloc(output_bytes,MEM_CAT_TEMP);
    double* rows=(double*)mem_alloc(row_bytes*2,MEM_CAT_TEMP);
    if (!result->pixels || !rows) { mem_free(rows); image_process_result_free(result); return false; }
    run.previous=rows; run.row=rows+(size_t)run.width*4;
    int64_t kernel=1+2*(int64_t)round(fmax(1.0,options.softness)*fmax(run.width,run.height)/960.0);
    if (kernel<3) kernel=3;
    int64_t ring=kernel<run.height ? kernel : run.height;
    size_t ring_rows=(size_t)ring<=32u*1024u*1024u/row_bytes ? (size_t)ring : 1;
    result->scratch_bytes=row_bytes*(2+(filtered ? ring_rows+4 : 0));
    bool ok=true;
    if (filtered && cache && cache->entries[2].ready) {
        for (int y=0;y<run.height && ok;y++) ok=image_effect_row(&run,y,cache->entries[2].pixels+(size_t)y*run.width*4);
    } else if (filtered) {
        ok=image_box_filter(run.width,run.height,4,kernel,image_color_row,image_effect_row,&run,cancelled);
        if (ok && cache && cache->entries[2].pixels) cache->entries[2].ready=true;
    } else {
        for (int y=0;y<run.height && ok;y++) ok=image_effect_row(&run,y,nullptr);
    }
    mem_free(rows);
    result->cache_bytes=cache ? cache->bytes : 0;
    if (!ok) image_process_result_free(result);
    return ok;
}
void image_process_result_free(ImageProcessResult* result) {
    if (!result) return;
    if (result->owns_pixels) mem_free(result->pixels);
    result->pixels=nullptr;
}
