// P2: bump-pointer fast path for js_new_object()
// heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS) skips the
// class-index lookup and uses the bump-pointer allocator directly.
// This test verifies correctness across many allocations.

class Box {
    constructor(v) {
        this.v = v;
    }
}

// Allocate 100 objects and sum their values: 0+1+...+99 = 4950
var sum = 0;
for (var i = 0; i < 100; i = i + 1) {
    sum = sum + (new Box(i)).v;
}

// Multiple live objects — GC must handle them all
var b1 = new Box(10);
var b2 = new Box(20);
var b3 = new Box(30);
var nested_sum = b1.v + b2.v + b3.v;  // 60

{ sum: sum, nested_sum: nested_sum };
