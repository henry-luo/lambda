// T12-0 diagnostic only: separate hash insertion from ordered-node update,
// delete/reinsert, and iteration work. It is not part of the 63-row metric.
function build_map(size) {
    var map = new Map();
    for (var index = 0; index < size; index++) map.set("key-" + index, index);
    return map;
}

function measure(label, work) {
    var started = Date.now();
    var checksum = work();
    // The paired runner strips timing records before checking deterministic
    // output, while a direct run retains this per-operation diagnostic.
    console.log("__TIMING__:" + (Date.now() - started) + ":" + label + ":" + checksum);
    return checksum;
}

var sizes = [1000, 2000, 4000, 8000];
var total_started = Date.now();
var total_checksum = 0;
for (var size_index = 0; size_index < sizes.length; size_index++) {
    var size = sizes[size_index];
    var map = build_map(size);
    total_checksum += measure("update-" + size, function() {
        var sum = 0;
        for (var index = 0; index < size; index++) {
            map.set("key-" + index, index + 1);
            sum += map.get("key-" + index);
        }
        return sum;
    });
    total_checksum += measure("reinsert-" + size, function() {
        var sum = 0;
        for (var index = 0; index < size; index++) {
            map.delete("key-" + index);
            map.set("key-" + index, index + 2);
            sum += map.get("key-" + index);
        }
        return sum;
    });
    total_checksum += measure("iterate-" + size, function() {
        var sum = 0;
        for (var entry of map) sum += entry[1];
        return sum;
    });
}
console.log("checksum:" + total_checksum);
console.log("__TIMING__:" + (Date.now() - total_started));
