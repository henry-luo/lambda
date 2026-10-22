#include "image_process.hpp"
#include "transpiler.hpp"
#include "lambda-number-runtime.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/log.h"
#include <math.h>
#include <string.h>

bool lambda_image_source(Item pixels, ImageProcessSource* out) {
    if (!out || get_type_id(pixels)!=LMD_TYPE_ARRAY_NUM) return false;
    ArrayNum* array=pixels.array_num;
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    if (array->get_elem_type()!=ELEM_UINT8 || array_num_get_shape_strides(array,shape,strides)!=3 ||
        shape[2]!=4 || shape[0]<1 || shape[1]<1 || shape[0]>16000000 || shape[1]>16000000 ||
        shape[0]*shape[1]>16000000) return false;
    *out={}; out->pixels=(const uint8_t*)array->data;
    out->width=(int)shape[1]; out->height=(int)shape[0];
    memcpy(out->strides,strides,sizeof(out->strides));
    return true;
}
bool lambda_image_options(Item options, ImageProcessOptions* out) {
    if (!out || get_type_id(options)!=LMD_TYPE_MAP) return false;
    *out={};
    MapReader map=ItemReader(options.to_const()).asMap();
    struct Number { const char* name; double* value; double low, high; };
    Number numbers[]={
        {"angle",&out->angle,-15,15}, {"exposure",&out->exposure,-3,3},
        {"brightness",&out->brightness,-1,1}, {"contrast",&out->contrast,0,2},
        {"saturation",&out->saturation,0,2}, {"temperature",&out->temperature,-1,1},
        {"gamma",&out->gamma,0.2,3}, {"black",&out->black,0,0.95}, {"white",&out->white,0.05,1},
        {"output_black",&out->output_black,0,1}, {"output_white",&out->output_white,0,1},
        {"shadows",&out->shadows,-1,1}, {"highlights",&out->highlights,-1,1},
        {"intensity",&out->intensity,0,1}, {"vignette",&out->vignette,0,1},
        {"softness",&out->softness,0,6}, {"sharpen",&out->sharpen,0,2}};
    for (auto& number:numbers) {
        auto value=map.get(number.name);
        if (value.isNull()) continue;
        if (!value.isNumber()) return false;
        double v;
        if (!item_try_to_double(value.item(),&v)) return false;
        if (!isfinite(v) || v<number.low || v>number.high) return false;
        *number.value=v;
    }
    struct Boolean { const char* name; bool* value; };
    Boolean booleans[]={{"flip_x",&out->flip_x},{"flip_y",&out->flip_y},
        {"compare",&out->compare},{"uncropped",&out->uncropped}};
    for (auto& boolean:booleans) {
        auto value=map.get(boolean.name);
        if (value.isNull()) continue;
        if (!value.isBool()) return false;
        *boolean.value=value.asBool();
    }
    struct Integer { const char* name; int* value; int high; };
    Integer integers[]={{"turns",&out->turns,3},{"output_width",&out->output_width,16384},
        {"output_height",&out->output_height,16384}};
    for (auto& integer:integers) {
        auto value=map.get(integer.name);
        if (value.isNull()) continue;
        if (!value.isInt() || value.asInt()<0 || value.asInt()>integer.high) return false;
        *integer.value=(int)value.asInt();
    }
    auto crop=map.get("crop");
    if (!crop.isNull()) {
        if (!crop.isMap()) return false;
        const char* edges[]={"left","top","right","bottom"};
        for (int i=0;i<4;i++) {
            auto value=crop.asMap().get(edges[i]);
            if (!value.isNumber()) return false;
            if (!item_try_to_double(value.item(),&out->crop[i])) return false;
            if (!isfinite(out->crop[i]) || out->crop[i]<0 || out->crop[i]>1) return false;
        }
    }
    auto matrix=map.get("matrix");
    if (!matrix.isNull()) {
        if (!matrix.isArray() || matrix.asArray().length()!=12) return false;
        for (int i=0;i<12;i++) {
            auto value=matrix.asArray().get(i);
            if (!value.isNumber()) return false;
            if (!item_try_to_double(value.item(),&out->matrix[i])) return false;
            if (!isfinite(out->matrix[i]) || fabs(out->matrix[i])>16) return false;
        }
    }
    auto mode=map.get("curve_mode");
    if (!mode.isNull()) {
        if (!mode.isString() || (strcmp(mode.asString()->chars,"linear") && strcmp(mode.asString()->chars,"smooth"))) return false;
        out->smooth_curves=!strcmp(mode.asString()->chars,"smooth");
    }
    auto curves=map.get("curves");
    if (!curves.isNull()) {
        if (!curves.isMap()) return false;
        const char* channels[]={"rgb","r","g","b"};
        MapReader curve_map=curves.asMap();
        auto entries=curve_map.entries(); const char* key; ItemReader points;
        while (entries.next(&key,&points)) {
            int channel=-1;
            for (int c=0;c<IMAGE_TONE_CHANNELS;c++) if (!strcmp(channels[c],key)) channel=c;
            if (channel<0 || !points.isArray()) return false;
            auto rows=points.asArray();
            int64_t count=rows.length();
            if (count<2 || count>IMAGE_TONE_POINTS) return false;
            out->curve_counts[channel]=(uint64_t)count;
            for (int64_t p=0;p<count;p++) {
                auto point=rows.get(p);
                if (!point.isArray() || point.asArray().length()!=2) return false;
                for (int axis=0;axis<2;axis++) {
                    auto value=point.asArray().get(axis);
                    if (!value.isNumber() || !item_try_to_double(value.item(),&out->curves[channel][p][axis])) return false;
                }
            }
        }
    }
    return image_process_tone_valid(*out) && out->white>out->black && out->crop[2]>out->crop[0] && out->crop[3]>out->crop[1] &&
        ((out->output_width==0 && out->output_height==0) ||
         (out->output_width>0 && out->output_height>0 && (int64_t)out->output_width*out->output_height<=16000000));
}
Item lambda_image_result(const ImageProcessResult& result) {
    int64_t shape[]={result.height,result.width,4};
    ArrayNum* output=alloc_ndim_arraynum(ELEM_UINT8,3,shape);
    if (!output || !output->data || output->length!=(int64_t)result.width*result.height*4) return ItemError;
    memcpy(output->data,result.pixels,(size_t)result.width*result.height*4);
    return {.array_num=output};
}
extern "C" Item fn_image_process(Item image, Item options) {
    RootFrame roots(3); Rooted<Item> pixels(roots,image), parameters(roots,options), output(roots,ItemNull);
    ImageProcessSource source;
    ImageProcessOptions config;
    if (!lambda_image_source(pixels.get(),&source) || !lambda_image_options(parameters.get(),&config)) {
        log_error("IMAGE_PROCESS: invalid RGBA8 image or processing options"); return ItemError;
    }
    int width, height;
    if (!image_process_dimensions(source,config,&width,&height)) return ItemError;
    int64_t shape[]={height,width,4};
    ArrayNum* array=alloc_ndim_arraynum(ELEM_UINT8,3,shape);
    if (!array || !array->data || array->length!=(int64_t)width*height*4) return ItemError;
    output.set({.array_num=array});
    // allocation can relocate the source header/data; refresh the borrow afterwards.
    if (!lambda_image_source(pixels.get(),&source)) return ItemError;
    ImageProcessResult result;
    if (!image_process(source,config,&result,nullptr,nullptr,(uint8_t*)array->data,(size_t)width*height*4)) return ItemError;
    return output.get();
}
