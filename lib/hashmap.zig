const std = @import("std");
const HashMap = std.AutoHashMap;

const Allocator = std.heap.page_allocator;

// Define the type of the map to store `anyopaque` as values and `[]u8` as keys
var map: HashMap([]u8, ?anyopaque) = HashMap([]u8, ?anyopaque).init(Allocator);

pub fn put(key: []const u8, value: anyopaque) void {
    // Insert the `anyopaque` value into the map with the given key
    try map.put(key, value);
}

pub fn get(key: []const u8) ?anyopaque {
    // Retrieve the `anyopaque` value from the map using the key
    const result = map.get(key);
    if (result) |v| {
        return v;
    }
    return null;
}

pub fn cleanup() void {
    // Cleanup the map
    map.deinit();
}
