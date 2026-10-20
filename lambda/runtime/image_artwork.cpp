#include "image_raster.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/font/font.h"
#include "../../lib/arena.h"
#include "../../lib/utf.h"
#include <thorvg_capi.h>
#include <math.h>

// image content uses the same font outlines and vector backend as document rendering.
struct ImageArtwork : RasterOwner {
    const char* failure = nullptr;
    Tvg_Canvas canvas = nullptr;
    Tvg_Paint shape = nullptr;
    FontContext* fonts = nullptr;
    FontHandle* font = nullptr;
    Arena* arena = nullptr;
    bool engine = false;
    size_t commands = 0;
    float pen_x = 0, pen_y = 0;
    ~ImageArtwork() {
        if (canvas) tvg_canvas_destroy(canvas);
        if (shape) tvg_paint_rel(shape);
        if (font) font_handle_release(font);
        if (fonts) font_context_destroy(fonts);
        if (arena) arena_destroy(arena);
        if (engine) tvg_engine_term();
    }
    bool fail(const char* reason) { failure = reason; return false; }
    bool number(Item owner, const char* key, double fallback, double low, double high, double* out) {
        return (lambda_image_number(owner,key,fallback,out) && *out>=low && *out<=high) || fail("artwork number is outside its finite range");
    }
    bool color(Item value, uint8_t* color) {
        return lambda_image_color(value,4,color) || fail("artwork requires normalized RGBA color");
    }
    bool coordinates(const ItemReader& reader, double* values, int count, int start=0) {
        if (!reader.isArray() || reader.asArray().length()!=(size_t)(count+start)) return fail("invalid artwork coordinate count");
        for (int i=0;i<count;i++) if (!item_try_to_double(reader.asArray().get(i+start).item(),values+i) ||
            !isfinite(values[i]) || fabs(values[i])>1000000) return fail("artwork coordinates must be finite and bounded");
        return true;
    }
    bool paint(Item node) {
        uint8_t fill[]={0,0,0,255}, stroke[]={0,0,0,0};
        Item fill_value=item_attr(node,"fill"), stroke_value=item_attr(node,"stroke");
        if (get_type_id(fill_value)!=LMD_TYPE_NULL && !color(fill_value,fill)) return false;
        if (get_type_id(stroke_value)!=LMD_TYPE_NULL && !color(stroke_value,stroke)) return false;
        tvg_shape_set_fill_color(shape,fill[0],fill[1],fill[2],fill[3]);
        tvg_shape_set_fill_rule(shape,lambda_image_tag(node,"text") ? TVG_FILL_RULE_NON_ZERO : TVG_FILL_RULE_EVEN_ODD);
        double width;
        if (!number(node,"stroke_width",0,0,1024,&width)) return false;
        tvg_shape_set_stroke_width(shape,(float)width);
        tvg_shape_set_stroke_color(shape,stroke[0],stroke[1],stroke[2],stroke[3]);
        tvg_shape_set_stroke_join(shape,TVG_STROKE_JOIN_ROUND);
        tvg_shape_set_stroke_cap(shape,TVG_STROKE_CAP_ROUND);
        Item gradient=item_attr(node,"gradient");
        if (get_type_id(gradient)==LMD_TYPE_NULL) return true;
        double start[2], end[2]; uint8_t last[4];
        if (get_type_id(gradient)!=LMD_TYPE_MAP || !lambda_image_setting(gradient,"kind","linear") ||
            !coordinates(ItemReader(item_attr(gradient,"start").to_const()),start,2) ||
            !coordinates(ItemReader(item_attr(gradient,"end").to_const()),end,2) ||
            (start[0]==end[0] && start[1]==end[1]) || !color(item_attr(gradient,"color"),fill) ||
            !color(item_attr(gradient,"end_color"),last)) return fail("invalid artwork linear gradient");
        Tvg_Gradient ramp=tvg_linear_gradient_new();
        if (!ramp) return fail("cannot allocate artwork gradient");
        Tvg_Color_Stop stops[]={{0,fill[0],fill[1],fill[2],fill[3]},{1,last[0],last[1],last[2],last[3]}};
        tvg_linear_gradient_set(ramp,(float)start[0],(float)start[1],(float)end[0],(float)end[1]);
        tvg_gradient_set_color_stops(ramp,stops,2);
        if (tvg_shape_set_gradient(shape,ramp)!=TVG_RESULT_SUCCESS) { tvg_gradient_del(ramp); return fail("cannot attach artwork gradient"); }
        return true;
    }
    bool vector(Item node) {
        if (lambda_image_setting(node,"shape","rectangle")) {
            double radius;
            if (!number(node,"radius",0,0,fmin(result->width,result->height)/2.0,&radius)) return false;
            tvg_shape_append_rect(shape,0,0,(float)result->width,(float)result->height,(float)radius,(float)radius,true);
        } else if (lambda_image_setting(node,"shape","ellipse")) {
            tvg_shape_append_circle(shape,result->width/2.0f,result->height/2.0f,result->width/2.0f,result->height/2.0f,true);
        } else if (lambda_image_setting(node,"shape","path")) {
            ItemReader commands(item_attr(node,"path").to_const());
            if (!commands.isArray() || commands.asArray().length()<1 || commands.asArray().length()>4096)
                return fail("artwork path requires 1 to 4096 commands");
            double current_x=0,current_y=0,start_x=0,start_y=0;
            bool opened=false;
            for (size_t i=0;i<commands.asArray().length();i++) {
                ItemReader command=commands.asArray().get(i);
                if (!command.isArray()) return fail("artwork path command must be an array");
                Item op=command.asArray().get(0).item();
                if (!is_text_type_id(get_type_id(op))) return fail("artwork path command needs an operator");
                const char* name=op.get_chars(); double p[6];
                int count=!strcmp(name,"Z") ? 0 : !strcmp(name,"M") || !strcmp(name,"L") ? 2 : !strcmp(name,"Q") ? 4 : !strcmp(name,"C") ? 6 : -1;
                if (count<0 || !coordinates(command,p,count,1)) return fail("supported path operators are M, L, Q, C and Z");
                if (!strcmp(name,"M")) { tvg_shape_move_to(shape,(float)p[0],(float)p[1]); start_x=p[0]; start_y=p[1]; opened=true; }
                else if (!opened) return fail("artwork path must begin each subpath with M");
                else if (!strcmp(name,"L")) tvg_shape_line_to(shape,(float)p[0],(float)p[1]);
                else if (!strcmp(name,"C")) tvg_shape_cubic_to(shape,(float)p[0],(float)p[1],(float)p[2],(float)p[3],(float)p[4],(float)p[5]);
                else if (!strcmp(name,"Q")) tvg_shape_cubic_to(shape,(float)(current_x+(p[0]-current_x)*2/3),
                    (float)(current_y+(p[1]-current_y)*2/3),(float)(p[2]+(p[0]-p[2])*2/3),(float)(p[3]+(p[1]-p[3])*2/3),(float)p[2],(float)p[3]);
                else { tvg_shape_close(shape); current_x=start_x; current_y=start_y; opened=false; }
                if (count) { current_x=p[count-2]; current_y=p[count-1]; }
            }
        } else return fail("unknown artwork shape");
        return paint(node);
    }
    static bool outline(void* opaque, FontPathCommand command, const float* p, int) {
        ImageArtwork* art=(ImageArtwork*)opaque;
        if (++art->commands>262144) return art->fail("text outline command budget exceeded");
        switch (command) {
        case FONT_PATH_MOVE: return tvg_shape_move_to(art->shape,art->pen_x+p[0],art->pen_y+p[1])==TVG_RESULT_SUCCESS;
        case FONT_PATH_LINE: return tvg_shape_line_to(art->shape,art->pen_x+p[0],art->pen_y+p[1])==TVG_RESULT_SUCCESS;
        case FONT_PATH_CUBIC: return tvg_shape_cubic_to(art->shape,art->pen_x+p[0],art->pen_y+p[1],art->pen_x+p[2],art->pen_y+p[3],art->pen_x+p[4],art->pen_y+p[5])==TVG_RESULT_SUCCESS;
        case FONT_PATH_CLOSE: return tvg_shape_close(art->shape)==TVG_RESULT_SUCCESS;
        }
        return false;
    }
    bool text(Item node, Item assets) {
        Item font_id=item_attr(node,"font"), data=ItemNull;
        if (!is_text_type_id(get_type_id(font_id))) return fail("text requires an embedded font asset ID");
        if (lambda_image_tag(assets,"assets")) for (int64_t i=0;i<assets.element->length;i++) {
            Item entry=item_at(assets,i), id=item_attr(entry,"id");
            if (lambda_image_tag(entry,"font") && is_text_type_id(get_type_id(id)) && id.get_len()==font_id.get_len() &&
                !memcmp(id.get_chars(),font_id.get_chars(),id.get_len())) data=item_attr(entry,"data");
        }
        if (get_type_id(data)!=LMD_TYPE_BINARY || !binary_length(data.get_binary()) || binary_length(data.get_binary())>16*1024*1024)
            return fail("missing font: embed a font asset before rendering text");
        double size,leading;
        if (!number(node,"size",24,1,1024,&size) || !number(node,"line_height",1.2,0.5,8,&leading)) return false;
        FontContextConfig config={}; config.pixel_ratio=1; config.enable_disk_cache=false;
        fonts=font_context_create(&config); arena=arena_create_default();
        if (!fonts || !arena) return fail("cannot create artwork font context");
        FontStyleDesc style={}; style.family="image-embedded"; style.size_px=(float)size; style.weight=FONT_WEIGHT_NORMAL;
        font=font_load_from_memory(fonts,binary_data(data.get_binary()),binary_length(data.get_binary()),&style);
        if (!font) return fail("cannot decode embedded artwork font");
        if (node.element->length>1 || (node.element->length==1 && get_type_id(item_at(node,0))!=LMD_TYPE_STRING))
            return fail("text requires a single string body");
        // S2.6.2: element construction drops empty strings; zero children is an empty text body.
        Item body=node.element->length ? item_at(node,0) : ItemNull;
        size_t length=node.element->length ? body.get_len() : 0;
        if (length>16384) return fail("text exceeds 16384 UTF-8 bytes");
        if (!lambda_image_setting(node,"align","left") && !lambda_image_setting(node,"align","center") && !lambda_image_setting(node,"align","right"))
            return fail("unsupported text alignment");
        const FontMetrics* metrics=font_get_metrics(font);
        if (!metrics) return fail("cannot read artwork font metrics");
        const char* source=length ? body.get_chars() : ""; size_t offset=0; unsigned line=0;
        while (offset<=length) {
            size_t end=offset;
            while (end<length && source[end]!='\n') end++;
            double advance=0; uint32_t previous=0;
            // two bounded passes share the exact kerning/advance geometry used by the outlines.
            for (int pass=0;pass<2;pass++) {
                pen_x=pass==0 || lambda_image_setting(node,"align","left") ? 0 :
                    (float)((result->width-advance)*(lambda_image_setting(node,"align","center") ? 0.5 : 1));
                pen_y=metrics->ascender+(float)(line*size*leading); previous=0;
                for (size_t p=offset;p<end;) {
                    uint32_t cp; int consumed=utf8_decode(source+p,end-p,&cp);
                    if (consumed<=0) return fail("text must be valid UTF-8");
                    p+=(size_t)consumed;
                    // this first text profile requires one horizontal glyph per codepoint.
                    if ((cp>=0x300 && cp<=0x36f) || utf_requires_contextual_shaping(cp) || utf_bidi_strong_class(cp)>0 || (cp>=0x200b && cp<=0x200f) ||
                        (cp>=0x202a && cp<=0x202e) || (cp>=0x2066 && cp<=0x2069)) return fail("text script requires a shaping profile not yet supported");
                    uint32_t glyph=font_get_glyph_index(font,cp);
                    if (!glyph) return fail("embedded font is missing a requested glyph");
                    pen_x+=font_get_kerning(font,previous,cp);
                    // known face-local glyphs use the index API, whose empty outlines are valid whitespace.
                    if (pass==1 && !font_visit_glyph_index_path(font,glyph,outline,this,arena)) return fail("text glyph has no supported outline");
                    pen_x+=font_get_glyph(font,cp).advance_x; previous=cp;
                }
                if (pass==0) advance=pen_x;
            }
            if (end==length) break;
            offset=end+1; line++;
        }
        return paint(node);
    }
    bool run(Item node,Item assets,size_t available) {
        double width,height;
        if (!number(node,"width",0,1,16000000,&width) || !number(node,"height",0,1,16000000,&height) ||
            floor(width)!=width || floor(height)!=height || width*height>16000000) return fail("artwork requires bounded integer dimensions");
        if (width*height*4>available) return fail("artwork scratch budget exceeded");
        result=raster_buffer_create((uint32_t)width,(uint32_t)height,4,64*1024*1024);
        if (!result) return fail("cannot allocate artwork raster");
        engine=tvg_engine_init(0)==TVG_RESULT_SUCCESS;
        if (!engine) return fail("cannot initialize artwork vector backend");
        canvas=tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT); shape=tvg_shape_new();
        if (!canvas || !shape || tvg_swcanvas_set_target(canvas,(uint32_t*)result->pixels,result->width,result->width,result->height,TVG_COLORSPACE_ABGR8888)!=TVG_RESULT_SUCCESS)
            return fail("cannot create artwork vector target");
        if (!(lambda_image_tag(node,"text") ? text(node,assets) : vector(node))) return false;
        uint32_t path_count=0;
        if (tvg_shape_get_path(shape,nullptr,&path_count,nullptr,nullptr)!=TVG_RESULT_SUCCESS)
            return fail("cannot inspect artwork geometry");
        // an empty outline scene is already transparent; it has no drawable backend shape.
        if (!path_count) return true;
        if (tvg_canvas_push(canvas,shape)!=TVG_RESULT_SUCCESS) return fail("cannot submit artwork shape");
        shape=nullptr; // the canvas now owns the submitted paint.
        if (tvg_canvas_draw(canvas,true)!=TVG_RESULT_SUCCESS || tvg_canvas_sync(canvas)!=TVG_RESULT_SUCCESS) return fail("cannot draw artwork");
        // the ThorVG target is premultiplied; Raster stores straight sRGB including alpha.
        for (size_t i=0;i<result->size;i+=4) if (result->pixels[i+3]) for (int c=0;c<3;c++)
            result->pixels[i+c]=(uint8_t)fmin(255,lround(result->pixels[i+c]*255.0/result->pixels[i+3]));
        return true;
    }
};

bool lambda_image_artwork(Item source, Item assets, RasterOwner& output, size_t available, const char** failure) {
    ImageArtwork artwork;
    if (!artwork.run(source,assets,available)) { *failure=artwork.failure; return false; }
    output.reset(artwork.result); artwork.result=nullptr;
    return true;
}
