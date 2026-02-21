// Test: markup-sourced container mutation (arena-allocated maps/elements)
// Elements and maps loaded from input files have is_heap=0 (arena-allocated).
// Type-changing mutations must use runtime pool allocation instead of calloc/free
// to avoid corrupting the input pool memory.

pn main() {
    // T1: JSON map - read original values (arena-allocated map, is_heap=0)
    let data^e1 = input("test/input/test_data.json")
    print(data.name)
    print(data.count)

    // T2: JSON map - same-type mutation (string to string, in-place, no rebuild)
    var obj^e2 = input("test/input/test_data.json")
    obj.name = "beta"
    print(obj.name)

    // T3: JSON map - type-changing mutation (int to string, triggers shape rebuild)
    // Data migrates from input pool to runtime pool
    obj.count = "two hundred"
    print(obj.count)

    // T4: JSON map - another type change (bool to int)
    obj.active = 42
    print(obj.active)

    // T5: JSON map - second type change on same field (string to int)
    // Data already migrated, old buffer pool_free'd from runtime pool
    obj.count = 999
    print(obj.count)

    // T6: JSON map - verify all fields preserved across rebuilds
    print(obj.name)
    print(obj.active)

    // T7: XML element - type-changing attr mutation on markup element
    let doc^e3 = input("test/input/test_markup.xml")
    var root = doc[0]
    var item = root[0]
    item.name = 999
    print(item.name)

    // T8: XML element - second type change (data already migrated)
    item.name = "delta"
    print(item.name)
    print(item.id)
}
