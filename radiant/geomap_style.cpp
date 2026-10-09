#include "geomap_style.hpp"
#include "../lib/color.h"
#include "../lib/str.h"
#include <math.h>

enum ExprType : unsigned { EX_NULL=1, EX_BOOL=2, EX_NUMBER=4, EX_STRING=8, EX_ARRAY=16, EX_OBJECT=32, EX_ANY=63 };
enum ExprOp { OP_VALUE, OP_GET, OP_HAS, OP_ID, OP_GEOMETRY, OP_ZOOM, OP_COALESCE,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_ALL, OP_ANY, OP_NOT, OP_CASE, OP_MATCH,
    OP_STEP, OP_INTERPOLATE, OP_NUMBER, OP_STRING, OP_BOOLEAN };
struct GeoMapExpression {
    ExprOp op;
    unsigned type, dependencies;
    ItemReader literal;
    GeoMapExpression** args;
    int64_t count;
};
struct ExprCompiler {
    Arena* arena;
    unsigned nodes;
    const char* error;
    bool filter;
};
struct ExprValue {
    unsigned type;
    ItemReader item;
    double number;
    const char* string;
    bool boolean;
};
static const char* expr_text(ItemReader item) {
    return item.isString() ? item.asString()->chars : item.isSymbol() ? item.asSymbol()->chars : nullptr;
}
static bool expr_is(ItemReader item, const char* text) {
    const char* value=expr_text(item); return value && !strcmp(value,text);
}
static ExprValue expr_value(ItemReader item) {
    ExprValue value={}; value.item=item;
    if (item.isNull()) value.type=EX_NULL;
    else if (item.isBool()) { value.type=EX_BOOL; value.boolean=item.asBool(); }
    else if (item.isString()) { value.type=EX_STRING; value.string=item.asString()->chars; }
    else if (item.isArray()) value.type=EX_ARRAY;
    else if (item.isMap()) value.type=EX_OBJECT;
    else if (item_try_to_double(item.item(),&value.number) && isfinite(value.number)) value.type=EX_NUMBER;
    return value;
}
static GeoMapExpression* expr_error(ExprCompiler* c, const char* message) {
    if (!c->error) c->error=message;
    return nullptr;
}
static bool expr_accept(unsigned actual, unsigned expected) { return actual==EX_ANY || !(actual & ~expected); }
static bool expr_require(ExprCompiler* c, GeoMapExpression* node, unsigned type) {
    if (node && expr_accept(node->type,type)) return true;
    expr_error(c,"expression argument has the wrong type"); return false;
}
static unsigned expr_unify(ExprCompiler* c, unsigned a, unsigned b) {
    if (!a) return b;
    if (a==EX_ANY || b==EX_ANY) return EX_ANY;
    if ((a & ~EX_NULL) && (b & ~EX_NULL) && (a & ~EX_NULL)!=(b & ~EX_NULL)) {
        expr_error(c,"expression outputs must have one type"); return 0;
    }
    return a|b;
}
static bool expr_equal(ExprValue a, ExprValue b) {
    if (a.type!=b.type) return false;
    switch (a.type) {
    case EX_NULL: return true;
    case EX_BOOL: return a.boolean==b.boolean;
    case EX_NUMBER: return a.number==b.number;
    case EX_STRING: return !strcmp(a.string,b.string);
    default: return false;
    }
}
static ExprValue expr_eval(const GeoMapExpression* node, ItemReader feature, double zoom);
static bool style_color(ExprValue value, Color* out);
static GeoMapExpression* expr_finish(ExprCompiler* c, GeoMapExpression* node, unsigned expected, bool color_output) {
    if (c->error || (expected && !expr_require(c,node,expected))) return nullptr;
    if (!node->dependencies) {
        ExprValue value=expr_eval(node,ItemReader(),0); Color color={};
        // constant failures are errors even in an unselected branch, matching upstream folding.
        if (!value.type) return expr_error(c,"constant expression failed");
        if (color_output && value.type!=EX_NULL && !style_color(value,&color))
            return expr_error(c,"constant color expression has an invalid value");
    }
    return node;
}
static GeoMapExpression* expr_compile(ExprCompiler* c, ItemReader input, unsigned depth=0,
    bool zoom_allowed=false, unsigned expected=0, bool color_output=false) {
    if (depth>32 || ++c->nodes>1024) return expr_error(c,"expression depth or node quota exceeded");
    auto* node=(GeoMapExpression*)arena_calloc(c->arena,sizeof(GeoMapExpression));
    if (!node) return expr_error(c,"expression allocation failed");
    node->literal=input;
    if (!input.isArray()) {
        node->type=expr_value(input).type;
        if (!node->type || node->type==EX_OBJECT) return expr_error(c,"expression requires a scalar or literal wrapper");
        return expr_finish(c,node,expected,color_output);
    }
    ArrayReader args=input.asArray(); int64_t length=args.length();
    if (!length || !args.get(0).isString()) return expr_error(c,"expression must start with an operator string");
    const char* op=expr_text(args.get(0));
    if (!strcmp(op,"literal")) {
        if (length!=2) return expr_error(c,"literal requires one argument");
        node->literal=args.get(1); node->type=expr_value(node->literal).type;
        return node->type ? expr_finish(c,node,expected,color_output) : expr_error(c,"unsupported literal value");
    }
    struct Operator { const char* name; ExprOp op; int64_t minimum, maximum; };
    static const Operator operators[]={
        {"get",OP_GET,1,2},{"has",OP_HAS,1,2},{"id",OP_ID,0,0},{"geometry-type",OP_GEOMETRY,0,0},{"zoom",OP_ZOOM,0,0},
        {"coalesce",OP_COALESCE,1,1024},{"==",OP_EQ,2,2},{"!=",OP_NE,2,2},{"<",OP_LT,2,2},{"<=",OP_LE,2,2},
        {">",OP_GT,2,2},{">=",OP_GE,2,2},{"all",OP_ALL,0,1024},{"any",OP_ANY,0,1024},{"!",OP_NOT,1,1},
        {"case",OP_CASE,3,1024},{"match",OP_MATCH,4,1024},{"step",OP_STEP,4,1024},{"interpolate",OP_INTERPOLATE,6,1024},
        {"number",OP_NUMBER,1,1024},{"string",OP_STRING,1,1024},{"boolean",OP_BOOLEAN,1,1024}
    };
    const Operator* found=nullptr;
    for (const auto& entry:operators) if (!strcmp(op,entry.name)) { found=&entry; break; }
    if (!found) return expr_error(c,"unsupported expression operator");
    node->op=found->op; node->count=length-1;
    if (node->count<found->minimum || node->count>found->maximum) return expr_error(c,"invalid expression arity");
    if ((node->op==OP_CASE && node->count%2!=1) ||
        ((node->op==OP_MATCH || node->op==OP_STEP || node->op==OP_INTERPOLATE) && node->count%2!=0))
        return expr_error(c,"expression requires paired branches or stops");
    if (node->op==OP_ZOOM && !c->filter && !zoom_allowed)
        return expr_error(c,"paint zoom must be the input of an outer step or interpolate");
    node->args=(GeoMapExpression**)arena_calloc(c->arena,sizeof(GeoMapExpression*)*(size_t)node->count);
    if (node->count && !node->args) return expr_error(c,"expression allocation failed");
    for (int64_t i=0;i<node->count;i++) {
        bool label=node->op==OP_MATCH && i>0 && i<node->count-1 && i%2==1;
        bool descriptor=node->op==OP_INTERPOLATE && i==0;
        if (label || descriptor) {
            auto* child=(GeoMapExpression*)arena_calloc(c->arena,sizeof(GeoMapExpression));
            if (!child) return expr_error(c,"expression allocation failed");
            child->literal=args.get(i+1); child->type=expr_value(child->literal).type;
            node->args[i]=child;
        } else {
            bool direct_zoom=depth==0 && ((node->op==OP_STEP && i==0) || (node->op==OP_INTERPOLATE && i==1));
            // expected output types reach every branch, including branches that never execute.
            unsigned child_type=0; bool child_color=false;
            switch (node->op) {
            case OP_GET: case OP_HAS: child_type=i==0?EX_STRING:EX_OBJECT; break;
            case OP_ALL: case OP_ANY: case OP_NOT: child_type=EX_BOOL; break;
            case OP_COALESCE: child_type=expected; child_color=color_output; break;
            case OP_CASE:
                child_color=color_output && (i==node->count-1 || i%2==1);
                child_type=i<node->count-1 && i%2==0?EX_BOOL:expected; break;
            case OP_MATCH: child_type=i>0?expected:0; child_color=i>0 && color_output; break;
            case OP_STEP:
                child_color=color_output && i%2==1;
                child_type=i==0 || (i>=2 && i%2==0)?EX_NUMBER:expected; break;
            case OP_INTERPOLATE: child_type=EX_NUMBER; break;
            default: break;
            }
            node->args[i]=expr_compile(c,args.get(i+1),depth+1,direct_zoom,child_type,child_color);
        }
        if (!node->args[i]) return nullptr;
        node->dependencies|=node->args[i]->dependencies;
    }
    auto require=[&](int64_t index,unsigned type) { return expr_require(c,node->args[index],type); };
    switch (node->op) {
    case OP_GET: case OP_HAS:
        if (!require(0,EX_STRING) || (node->count==2 && !require(1,EX_OBJECT))) return nullptr;
        node->type=node->op==OP_HAS ? EX_BOOL : EX_ANY;
        if (node->count==1) node->dependencies|=GEOMAP_STYLE_FEATURE;
        break;
    case OP_ID: node->type=EX_ANY; node->dependencies|=GEOMAP_STYLE_FEATURE; break;
    case OP_GEOMETRY: node->type=EX_STRING; node->dependencies|=GEOMAP_STYLE_FEATURE; break;
    case OP_ZOOM: node->type=EX_NUMBER; node->dependencies|=GEOMAP_STYLE_ZOOM; break;
    case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        unsigned allowed=node->op==OP_EQ || node->op==OP_NE ? EX_NULL|EX_BOOL|EX_NUMBER|EX_STRING : EX_NUMBER|EX_STRING;
        if (!require(0,allowed) || !require(1,allowed)) return nullptr;
        unsigned a=node->args[0]->type,b=node->args[1]->type;
        if (a!=EX_ANY && b!=EX_ANY && a!=b) return expr_error(c,"comparison types do not match");
        if (node->op!=OP_EQ && node->op!=OP_NE && a==EX_ANY && b==EX_ANY)
            return expr_error(c,"ordered dynamic comparison needs a number or string assertion");
        node->type=EX_BOOL; break;
    }
    case OP_ALL: case OP_ANY: case OP_NOT:
        for (int64_t i=0;i<node->count;i++) if (!require(i,EX_BOOL)) return nullptr;
        node->type=EX_BOOL; break;
    case OP_COALESCE:
        for (int64_t i=0;i<node->count;i++) {
            unsigned type=node->args[i]->type;
            if (type!=EX_ANY && i<node->count-1) type&=~EX_NULL;
            node->type=expr_unify(c,node->type,type);
        }
        break;
    case OP_CASE:
        for (int64_t i=0;i<node->count-1;i+=2) {
            if (!require(i,EX_BOOL)) return nullptr;
            node->type=expr_unify(c,node->type,node->args[i+1]->type);
        }
        node->type=expr_unify(c,node->type,node->args[node->count-1]->type); break;
    case OP_MATCH: {
        if (!require(0,EX_NUMBER|EX_STRING)) return nullptr;
        unsigned labels=0;
        for (int64_t i=1;i<node->count-1;i+=2) {
            ItemReader label=node->args[i]->literal;
            int64_t count=label.isArray() ? label.asArray().length() : 1;
            if (!count) return expr_error(c,"match labels cannot be empty");
            if (count>1024 || c->nodes+(unsigned)count>1024) return expr_error(c,"match label quota exceeded");
            c->nodes+=(unsigned)count;
            for (int64_t j=0;j<count;j++) {
                ExprValue v=expr_value(label.isArray()?label.asArray().get(j):label);
                if ((v.type!=EX_NUMBER && v.type!=EX_STRING) || (v.type==EX_NUMBER && floor(v.number)!=v.number))
                    return expr_error(c,"match labels require strings or integer numbers");
                if (labels && labels!=v.type) return expr_error(c,"match labels must have one type");
                labels=v.type;
                for (int64_t k=1;k<=i;k+=2) {
                    ItemReader prior=node->args[k]->literal;
                    int64_t total=prior.isArray()?prior.asArray().length():1;
                    for (int64_t l=0;l<total && (k<i || l<j);l++)
                        if (expr_equal(v,expr_value(prior.isArray()?prior.asArray().get(l):prior)))
                            return expr_error(c,"match labels must be unique");
                }
            }
            node->type=expr_unify(c,node->type,node->args[i+1]->type);
        }
        if (node->args[0]->type!=EX_ANY && node->args[0]->type!=labels)
            return expr_error(c,"match input and labels must have one type");
        node->type=expr_unify(c,node->type,node->args[node->count-1]->type); break;
    }
    case OP_STEP: case OP_INTERPOLATE: {
        bool interpolate=node->op==OP_INTERPOLATE;
        if (interpolate) {
            ItemReader mode=node->args[0]->literal;
            if (!mode.isArray() || mode.asArray().length()!=1 || !expr_is(mode.asArray().get(0),"linear"))
                return expr_error(c,"only numeric linear interpolation is supported");
        }
        if (!require(interpolate?1:0,EX_NUMBER)) return nullptr;
        double previous=-INFINITY;
        if (!interpolate) node->type=node->args[1]->type;
        for (int64_t i=2;i<node->count;i+=2) {
            ExprValue stop=expr_value(node->args[i]->literal);
            if (node->args[i]->op!=OP_VALUE || stop.type!=EX_NUMBER || stop.number<=previous)
                return expr_error(c,"stops require strictly increasing literal numbers");
            previous=stop.number;
            if (interpolate && !require(i+1,EX_NUMBER)) return nullptr;
            node->type=expr_unify(c,node->type,node->args[i+1]->type);
        }
        break;
    }
    case OP_NUMBER: node->type=EX_NUMBER; break;
    case OP_STRING: node->type=EX_STRING; break;
    case OP_BOOLEAN: node->type=EX_BOOL; break;
    default: break;
    }
    return expr_finish(c,node,expected,color_output);
}
static ExprValue expr_eval(const GeoMapExpression* node, ItemReader feature, double zoom) {
    auto eval=[&](int64_t index) { return expr_eval(node->args[index],feature,zoom); };
    auto boolean=[](bool v) { ExprValue out={}; out.type=EX_BOOL; out.boolean=v; return out; };
    ExprValue invalid={},null_value={}; null_value.type=EX_NULL;
    switch (node->op) {
    case OP_VALUE: return expr_value(node->literal);
    case OP_ID: return expr_value(feature.isMap()?feature.asMap().get("id"):ItemReader());
    case OP_GEOMETRY: {
        ItemReader geometry=feature.isMap()?feature.asMap().get("geometry"):ItemReader();
        if (geometry.isNull()) geometry=feature;
        const char* type=geometry.isMap()?expr_text(geometry.asMap().get("type")):nullptr;
        ExprValue out={}; out.type=EX_STRING;
        out.string=type && !strncmp(type,"Multi",5) ? type+5 : type ? type : "";
        return out;
    }
    case OP_ZOOM: { ExprValue out={}; out.type=EX_NUMBER; out.number=zoom; return out; }
    case OP_GET: case OP_HAS: {
        ExprValue key=eval(0);
        ItemReader object=feature.isMap()?feature.asMap().get("properties"):ItemReader();
        if (node->count==2) {
            ExprValue explicit_object=eval(1);
            if (explicit_object.type!=EX_OBJECT) return invalid;
            object=explicit_object.item;
        }
        if (key.type!=EX_STRING) return invalid;
        if (!object.isMap()) return node->op==OP_HAS ? boolean(false) : null_value;
        return node->op==OP_HAS ? boolean(object.asMap().has(key.string)) : expr_value(object.asMap().get(key.string));
    }
    case OP_COALESCE:
        for (int64_t i=0;i<node->count;i++) { ExprValue v=eval(i); if (v.type!=EX_NULL) return v; }
        return null_value;
    case OP_ALL: case OP_ANY:
        for (int64_t i=0;i<node->count;i++) {
            ExprValue v=eval(i); if (v.type!=EX_BOOL) return invalid;
            if (v.boolean==(node->op==OP_ANY)) return v;
        }
        return boolean(node->op==OP_ALL);
    case OP_NOT: { ExprValue v=eval(0); return v.type==EX_BOOL ? boolean(!v.boolean) : invalid; }
    case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        ExprValue a=eval(0),b=eval(1);
        if (!a.type || !b.type) return invalid;
        if ((a.type|b.type)&(EX_ARRAY|EX_OBJECT)) return invalid;
        if (node->op==OP_EQ || node->op==OP_NE) return boolean(expr_equal(a,b)==(node->op==OP_EQ));
        if (a.type!=b.type || (a.type!=EX_NUMBER && a.type!=EX_STRING)) return invalid;
        double order=a.type==EX_NUMBER ? (a.number>b.number)-(a.number<b.number) : strcmp(a.string,b.string);
        return boolean(node->op==OP_LT?order<0:node->op==OP_LE?order<=0:node->op==OP_GT?order>0:order>=0);
    }
    case OP_CASE:
        for (int64_t i=0;i<node->count-1;i+=2) {
            ExprValue v=eval(i); if (v.type!=EX_BOOL) return invalid;
            if (v.boolean) return eval(i+1);
        }
        return eval(node->count-1);
    case OP_MATCH: {
        ExprValue input=eval(0); if (!input.type) return invalid;
        for (int64_t i=1;i<node->count-1;i+=2) {
            ItemReader label=node->args[i]->literal;
            int64_t count=label.isArray()?label.asArray().length():1;
            for (int64_t j=0;j<count;j++)
                if (expr_equal(input,expr_value(label.isArray()?label.asArray().get(j):label))) return eval(i+1);
        }
        return eval(node->count-1);
    }
    case OP_STEP: case OP_INTERPOLATE: {
        bool interpolate=node->op==OP_INTERPOLATE;
        ExprValue input=eval(interpolate?1:0); if (input.type!=EX_NUMBER) return invalid;
        int64_t chosen=interpolate?3:1;
        for (int64_t i=2;i<node->count;i+=2) {
            double stop=expr_value(node->args[i]->literal).number;
            if (input.number<stop) {
                if (!interpolate || i==2) return eval(chosen);
                ExprValue a=eval(chosen),b=eval(i+1);
                if (a.type!=EX_NUMBER || b.type!=EX_NUMBER) return invalid;
                double previous=expr_value(node->args[i-2]->literal).number;
                a.number+=(b.number-a.number)*(input.number-previous)/(stop-previous); return a;
            }
            chosen=i+1;
        }
        return eval(chosen);
    }
    case OP_NUMBER: case OP_STRING: case OP_BOOLEAN: {
        unsigned type=node->op==OP_NUMBER?EX_NUMBER:node->op==OP_STRING?EX_STRING:EX_BOOL;
        for (int64_t i=0;i<node->count;i++) { ExprValue v=eval(i); if (v.type==type) return v; }
        return invalid;
    }
    }
    return invalid;
}
static bool style_number(ItemReader value, double fallback, double* out) {
    if (value.isNull()) { *out=fallback; return true; }
    return item_try_to_double(value.item(),out) && isfinite(*out);
}
static bool style_color(ExprValue value, Color* out) {
    if (value.type!=EX_STRING) return false;
    if (!strcmp(value.string,"transparent")) { out->c=0; return true; }
    return color_parse_hex(value.string,&out->r,&out->g,&out->b,&out->a);
}
static GeoMapExpression* style_property(ExprCompiler* compiler, ItemReader paint, const char* key,
    unsigned type, double maximum=INFINITY) {
    if (compiler->error || paint.isNull()) return nullptr;
    ItemReader input=paint.asMap().get(key);
    if (input.isNull()) return nullptr;
    GeoMapExpression* node=expr_compile(compiler,input,0,false,type,type==EX_STRING);
    if (!expr_require(compiler,node,type)) return nullptr;
    if (node && !node->dependencies) {
        ExprValue value=expr_eval(node,ItemReader(),0);
        Color color={};
        bool valid=type==EX_STRING ? style_color(value,&color) : value.type==EX_NUMBER && value.number>=0 && value.number<=maximum;
        if (!valid) return expr_error(compiler,"constant paint expression has an invalid value");
    }
    return node;
}
bool geomap_style_compile(ElementReader layer, GeoMapStyle* style, char* diagnostic, size_t capacity) {
    *style={}; if (diagnostic && capacity) diagnostic[0]=0;
    style->arena=arena_create_default();
    ExprCompiler compiler={style->arena,0,nullptr,false};
    if (!style->arena) compiler.error="style allocation failed";
    const char* type=expr_text(layer.get_attr("type"));
    static const char* kinds[]={"background","fill","line","circle"};
    bool known=false;
    for (unsigned i=0;i<4;i++) if (type && !strcmp(type,kinds[i])) { style->kind=(GeoMapLayerKind)i; known=true; break; }
    if (!known) compiler.error="unsupported layer type";
    style->id=expr_text(layer.get_attr("id"));
    ItemReader paint=layer.get_attr("paint"),layout=layer.get_attr("layout");
    if ((!paint.isNull() && !paint.isMap()) || (!layout.isNull() && !layout.isMap())) compiler.error="paint and layout must be maps";
    if (!layer.get_attr("source-layer").isNull()) compiler.error="vector source layers are not yet supported";
    // one table owns property admission, compilation and dependency traversal.
    struct Property { const char* key; GeoMapLayerKind kind; size_t offset; unsigned type; double maximum; };
    static const Property properties[]={
        {"background-color",GEOMAP_BACKGROUND,offsetof(GeoMapStyle,color),EX_STRING,INFINITY},
        {"background-opacity",GEOMAP_BACKGROUND,offsetof(GeoMapStyle,opacity),EX_NUMBER,1},
        {"fill-color",GEOMAP_FILL,offsetof(GeoMapStyle,color),EX_STRING,INFINITY},
        {"fill-opacity",GEOMAP_FILL,offsetof(GeoMapStyle,opacity),EX_NUMBER,1},
        {"fill-outline-color",GEOMAP_FILL,offsetof(GeoMapStyle,outline),EX_STRING,INFINITY},
        {"line-color",GEOMAP_LINE,offsetof(GeoMapStyle,color),EX_STRING,INFINITY},
        {"line-opacity",GEOMAP_LINE,offsetof(GeoMapStyle,opacity),EX_NUMBER,1},
        {"line-width",GEOMAP_LINE,offsetof(GeoMapStyle,size),EX_NUMBER,4096},
        {"circle-color",GEOMAP_CIRCLE,offsetof(GeoMapStyle,color),EX_STRING,INFINITY},
        {"circle-opacity",GEOMAP_CIRCLE,offsetof(GeoMapStyle,opacity),EX_NUMBER,1},
        {"circle-radius",GEOMAP_CIRCLE,offsetof(GeoMapStyle,size),EX_NUMBER,4096},
        {"circle-stroke-color",GEOMAP_CIRCLE,offsetof(GeoMapStyle,stroke_color),EX_STRING,INFINITY},
        {"circle-stroke-width",GEOMAP_CIRCLE,offsetof(GeoMapStyle,stroke_width),EX_NUMBER,4096},
        {"circle-stroke-opacity",GEOMAP_CIRCLE,offsetof(GeoMapStyle,stroke_opacity),EX_NUMBER,1}
    };
    if (!compiler.error && paint.isMap()) {
        MapReader reader=paint.asMap();
        auto entries=reader.entries(); const char* key; ItemReader value;
        while (entries.next(&key,&value)) {
            bool supported=false;
            for (const auto& property:properties)
                if (property.kind==style->kind && !strcmp(key,property.key)) supported=true;
            if (!supported) compiler.error="unsupported paint property";
        }
    }
    style->cap=RDT_CAP_BUTT; style->join=RDT_JOIN_ROUND;
    if (!compiler.error && layout.isMap()) {
        MapReader reader=layout.asMap(); auto keys=reader.keys(); const char* key;
        while (keys.next(&key)) if (strcmp(key,"visibility") &&
            !(style->kind==GEOMAP_LINE && (!strcmp(key,"line-cap") || !strcmp(key,"line-join"))))
                compiler.error="unsupported layout property";
        const char* cap=expr_text(reader.get("line-cap"));
        const char* join=expr_text(reader.get("line-join"));
        if (!reader.get("line-cap").isNull()) {
            if (cap && !strcmp(cap,"round")) style->cap=RDT_CAP_ROUND;
            else if (cap && !strcmp(cap,"square")) style->cap=RDT_CAP_SQUARE;
            else if (!cap || strcmp(cap,"butt")) compiler.error="invalid line-cap";
        }
        if (!reader.get("line-join").isNull()) {
            if (join && !strcmp(join,"miter")) style->join=RDT_JOIN_MITER;
            else if (join && !strcmp(join,"bevel")) style->join=RDT_JOIN_BEVEL;
            else if (!join || strcmp(join,"round")) compiler.error="invalid line-join";
        }
    }
    ItemReader visibility=layout.isMap()?layout.asMap().get("visibility"):ItemReader();
    if (!visibility.isNull() && !expr_is(visibility,"none") && !expr_is(visibility,"visible")) compiler.error="invalid layer visibility";
    style->hidden=expr_is(visibility,"none");
    if (!style_number(layer.get_attr("minzoom"),-2,&style->minzoom) ||
        !style_number(layer.get_attr("maxzoom"),23,&style->maxzoom) || style->maxzoom<=style->minzoom) compiler.error="invalid layer zoom range";
    if (!compiler.error) {
        for (const auto& property:properties) if (property.kind==style->kind) {
            auto** program=(GeoMapExpression**)((char*)style+property.offset);
            *program=style_property(&compiler,paint,property.key,property.type,property.maximum);
        }
        ItemReader filter=layer.get_attr("filter");
        if (!filter.isNull()) {
            if (style->kind==GEOMAP_BACKGROUND) compiler.error="background layers cannot have filters";
            else { compiler.filter=true; style->filter=expr_compile(&compiler,filter,0,false,EX_BOOL); }
        }
    }
    for (const auto& property:properties) if (property.kind==style->kind) {
        auto* program=*(GeoMapExpression**)((char*)style+property.offset);
        if (program) style->dependencies|=program->dependencies;
    }
    if (style->filter) style->dependencies|=style->filter->dependencies;
    if (style->kind==GEOMAP_BACKGROUND && (style->dependencies&GEOMAP_STYLE_FEATURE))
        compiler.error="background paint cannot depend on feature properties";
    if (compiler.error) {
        if (diagnostic && capacity) str_copy(diagnostic,capacity,compiler.error,strlen(compiler.error));
        geomap_style_destroy(style); return false;
    }
    return true;
}
static double style_evaluate_number(const GeoMapExpression* program, ItemReader feature, double zoom,
    double fallback, double maximum, unsigned bit, unsigned* warnings) {
    if (!program) return fallback;
    ExprValue value=expr_eval(program,feature,zoom);
    if (value.type==EX_NUMBER && value.number>=0 && value.number<=maximum) return value.number;
    *warnings|=bit; return fallback;
}
static Color style_evaluate_color(const GeoMapExpression* program, ItemReader feature, double zoom,
    unsigned bit, unsigned* warnings) {
    Color color={}; color.a=255;
    if (program && !style_color(expr_eval(program,feature,zoom),&color)) *warnings|=bit;
    return color;
}
void geomap_style_evaluate(const GeoMapStyle* style, ItemReader feature, double zoom, GeoMapStyleResult* result) {
    *result={}; result->color.a=255; result->size=style->kind==GEOMAP_CIRCLE?5:1;
    result->cap=style->cap; result->join=style->join;
    result->visible=!style->hidden && zoom>=style->minzoom && zoom<style->maxzoom;
    if (!result->visible) return;
    if (style->filter) {
        ExprValue filter=expr_eval(style->filter,feature,floor(zoom));
        if (filter.type!=EX_BOOL) result->warnings|=8;
        if (filter.type!=EX_BOOL || !filter.boolean) { result->visible=false; return; }
    }
    result->color=style_evaluate_color(style->color,feature,zoom,1,&result->warnings);
    double opacity=style_evaluate_number(style->opacity,feature,zoom,1,1,2,&result->warnings);
    result->color.a=(uint8_t)round(result->color.a*opacity);
    result->size=(float)style_evaluate_number(style->size,feature,zoom,result->size,4096,4,&result->warnings);
    result->has_outline=style->outline!=nullptr;
    result->outline=style_evaluate_color(style->outline,feature,zoom,16,&result->warnings);
    result->outline.a=(uint8_t)round(result->outline.a*opacity);
    result->stroke_width=(float)style_evaluate_number(style->stroke_width,feature,zoom,0,4096,32,&result->warnings);
    result->stroke_color=style_evaluate_color(style->stroke_color,feature,zoom,64,&result->warnings);
    double stroke_opacity=style_evaluate_number(style->stroke_opacity,feature,zoom,1,1,128,&result->warnings);
    result->stroke_color.a=(uint8_t)round(result->stroke_color.a*stroke_opacity);
}
void geomap_style_destroy(GeoMapStyle* style) {
    if (style && style->arena) arena_destroy(style->arena);
    if (style) *style={};
}
