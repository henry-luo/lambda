#pragma once

#include "../lambda-data.hpp"
#include "js_typed_array.h"

// Shared with MIR guards; the map kind certifies this trailing payload.
struct JsTypedArrayMapCarrier {
    Map base;
    JsTypedArray payload;
};

struct JsArrayBufferMapCarrier {
    Map base;
    JsArrayBuffer* payload;
};

struct JsDataViewMapCarrier {
    Map base;
    JsDataView payload;
};

// Physical carriers, rather than a front end's class metadata, own GC edges.
inline JsArrayBuffer* js_get_arraybuffer_ptr(Map* map) {
    return map && map->map_kind == MAP_KIND_ARRAYBUFFER
        ? ((JsArrayBufferMapCarrier*)map)->payload : NULL;
}
inline JsDataView* js_get_dataview_ptr_from_map(Map* map) {
    return map && map->map_kind == MAP_KIND_DATAVIEW
        ? &((JsDataViewMapCarrier*)map)->payload : NULL;
}
