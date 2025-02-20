const std = @import("std");
const HashMap = std.AutoHashMap;
const Allocator = std.heap.page_allocator;

// define the type of the map to store `anyopaque` as values and `[]u8` as keys
pub fn hashmap_init() HashMap([]u8, ?anyopaque) {
    // Initialize the map
    const map: HashMap([]u8, ?anyopaque) = HashMap([]u8, ?anyopaque).init(Allocator);
    return map;
}

pub fn hashmap_put(map: HashMap, key: []const u8, value: anyopaque) void {
    // Insert the `anyopaque` value into the map with the given key
    try map.put(key, value);
}

pub fn hashmap_get(map: HashMap, key: []const u8) ?anyopaque {
    // Retrieve the `anyopaque` value from the map using the key
    const result = map.get(key);
    if (result) |v| {
        return v;
    }
    return null;
}

pub fn hashmap_cleanup(map: HashMap) void {
    // Cleanup the map
    map.deinit();
}
