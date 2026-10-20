#include "image_raster.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/image_process.h"
#include <math.h>

// style extents are explicit local filter regions, shared by rendering and editor bounds.
struct ImageStyle {
    const RasterBuffer* source;
    RasterBuffer* target;
    int padding, dx, dy;
    uint8_t color[4];
    double opacity;
    static bool read(void* opaque,int64_t y,double* row) {
        ImageStyle* s=(ImageStyle*)opaque;
        int64_t sy=y-s->padding-s->dy;
        for (uint32_t x=0;x<s->target->width;x++) {
            int64_t sx=(int64_t)x-s->padding-s->dx;
            uint8_t sample[4]={};
            if (sx>=0 && sy>=0 && sx<s->source->width && sy<s->source->height)
                raster_rgba_pixel(s->source,(uint32_t)sx,(uint32_t)sy,sample);
            row[x]=sample[3];
        }
        return true;
    }
    static bool write(void* opaque,int64_t y,const double* row) {
        ImageStyle* s=(ImageStyle*)opaque;
        for (uint32_t x=0;x<s->target->width;x++) {
            uint8_t* pixel=s->target->pixels+((size_t)y*s->target->width+x)*4;
            memcpy(pixel,s->color,3);
            pixel[3]=(uint8_t)lround(fmax(0,fmin(255,row[x]*s->opacity*s->color[3]/255.0)));
        }
        return true;
    }
};

bool lambda_image_styles(Item owner,const RasterBuffer** pixels,RasterOwner& output,
    int* offset,size_t available,const char** failure) {
    auto fail=[&](const char* reason) { *failure=reason; return false; };
    for (int64_t i=0;i<owner.element->length;i++) {
        Item node=item_at(owner,i);
        if (!lambda_image_tag(node,"style")) continue;
        Item enabled=item_attr(node,"enabled");
        if (get_type_id(enabled)!=LMD_TYPE_NULL && get_type_id(enabled)!=LMD_TYPE_BOOL) return fail("style enabled must be boolean");
        bool overlay=lambda_image_setting(node,"kind","overlay"), glow=lambda_image_setting(node,"kind","glow");
        if (get_type_id(item_attr(node,"kind"))==LMD_TYPE_NULL || (!overlay && !glow && !lambda_image_setting(node,"kind","shadow")))
            return fail("unsupported layer style");
        double padding,radius,dx,dy,opacity;
        if (!lambda_image_number(node,"outset",-1,&padding) || padding<0 || padding>512 || floor(padding)!=padding ||
            !lambda_image_number(node,"radius",0,&radius) || radius<0 || radius>128 || floor(radius)!=radius ||
            !lambda_image_number(node,"dx",0,&dx) || fabs(dx)>256 || floor(dx)!=dx ||
            !lambda_image_number(node,"dy",0,&dy) || fabs(dy)>256 || floor(dy)!=dy ||
            !lambda_image_number(node,"opacity",1,&opacity) || opacity<0 || opacity>1) return fail("invalid layer style geometry or opacity");
        if ((overlay && (padding || radius || dx || dy)) || (glow && (dx || dy))) return fail("style geometry disagrees with its kind");
        ImageStyle style={}; style.padding=(int)padding; style.dx=(int)dx; style.dy=(int)dy; style.opacity=opacity;
        if (!lambda_image_color(item_attr(node,"color"),4,style.color)) return fail("style requires normalized RGBA color");
        if (get_type_id(enabled)==LMD_TYPE_BOOL && !it2b(enabled)) continue;
        uint32_t width=(*pixels)->width+2*style.padding, height=(*pixels)->height+2*style.padding;
        size_t bytes=(size_t)width*height*4;
        if ((uint64_t)width*height>16000000 || bytes>available || (output.result && output.result->size>available-bytes))
            return fail("layer style exceeds intermediate raster budget");
        RasterOwner next; next.result=raster_buffer_create(width,height,4,64*1024*1024);
        if (!next.result) return fail("cannot allocate layer style result");
        style.source=*pixels; style.target=next.result;
        if (overlay) {
            double coverage=opacity*style.color[3]/255.0;
            for (uint32_t y=0;y<height;y++) for (uint32_t x=0;x<width;x++) {
                uint8_t* sample=next.result->pixels+((size_t)y*width+x)*4;
                raster_rgba_pixel(*pixels,x,y,sample);
                for (int c=0;c<3;c++) sample[c]=raster_encoded_sample(
                    raster_linear_sample(sample[c])*(1-coverage)+raster_linear_sample(style.color[c])*coverage);
            }
        } else {
            if (!image_box_filter(width,height,1,2*(int64_t)radius+1,ImageStyle::read,ImageStyle::write,&style))
                return fail("cannot filter layer style alpha");
            raster_buffer_over(next.result,*pixels,style.padding,style.padding,1,nullptr,0,0,1,false);
        }
        output.reset(next.result); next.result=nullptr; *pixels=output.result;
        *offset-=style.padding;
    }
    return true;
}
