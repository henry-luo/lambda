// Js58 regression-lock: sparse companion-map entries must stay live across
// allocation churn. GC traces arr->extra, which is where sparse data entries
// live after a large index gap.

var arr = [1, 2, 3];
arr[20000] = "keep";
arr[30000] = { tag: "object" };

function churn() {
  var hold = [];
  for (var i = 0; i < 300; i++) {
    hold.push(("chunk-" + i + "-").repeat(200));
    if (hold.length > 25) {
      hold.shift();
    }
    var tmp = [i, i + 1, i + 2];
    tmp[20000] = "tmp-" + i;
  }
  return hold.length;
}

console.log("before-20000", arr[20000]);
console.log("before-30000", arr[30000].tag);
console.log("churn-hold", churn());
console.log("after-20000", arr[20000]);
console.log("after-30000", arr[30000].tag);
console.log("key-20000", Object.keys(arr).indexOf("20000") >= 0);
console.log("key-30000", Object.keys(arr).indexOf("30000") >= 0);
