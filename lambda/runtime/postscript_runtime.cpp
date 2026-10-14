#include "../input/input-postscript.hpp"
#include "../core/mark_reader.hpp"

extern "C" Item fn_ps_scan(Item source, Item offset, Item options) {
    if (get_type_id(offset) != LMD_TYPE_INT || offset.int_val < 0) return ItemNull;
    MapReader limits(options.get_safe_map());
    ItemReader bytes = limits.get("max_token_bytes"), depth = limits.get("max_procedure_depth");
    int64_t max_bytes = bytes.isInt() ? bytes.asInt() : 1048576;
    int64_t max_depth = depth.isInt() ? depth.asInt() : 128;
    ItemReader end = limits.get("source_end");
    ItemReader objects = limits.get("max_scan_objects");
    int64_t max_objects = objects.isInt() ? objects.asInt() : 65536;
    // lexical recursion has a host-stack ceiling independent of guest execution frames.
    if (max_bytes < 1 || max_depth < 1 || max_depth > 256 || max_objects < 1) return ItemNull;
    // all scan results use the context's Input owner; this bridge does not allocate GC values.
    return postscript_scan_io(runtime_shape_tree(), source, (size_t)offset.int_val,
                               (size_t)max_bytes, (size_t)max_depth,
                               end.isInt() && end.asInt() >= 0 ? (size_t)end.asInt() : SIZE_MAX,
                               (size_t)max_objects);
}

extern "C" Item fn_ps_read(Item source, Item offset, Item options) {
    if (get_type_id(offset) != LMD_TYPE_INT || offset.int_val < 0) return ItemNull;
    return postscript_read_io(runtime_shape_tree(), source, (size_t)offset.int_val, options);
}

extern "C" Item fn_ps_png(Item planes, Item geometry, Item limits) {
    return postscript_png_io(runtime_shape_tree(), planes, geometry, limits);
}
