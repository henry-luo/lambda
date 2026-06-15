// Js58 regression-lock: sparse companion-map entries must stay live across
// allocation churn. GC traces arr->extra, which is where sparse data entries
// live after a large index gap.

var arr = [1, 2, 3];
arr[1000004] = "keep";
arr[1100004] = { tag: "object" };

function churn() {
  var hold = [];
  for (var i = 0; i < 300; i++) {
    hold.push(("chunk-" + i + "-").repeat(200));
    if (hold.length > 25) {
      hold.shift();
    }
    var tmp = [i, i + 1, i + 2];
    tmp[1000004] = "tmp-" + i;
  }
  return hold.length;
}

console.log("before-1000004", arr[1000004]);
console.log("before-1100004", arr[1100004].tag);
console.log("churn-hold", churn());
console.log("after-1000004", arr[1000004]);
console.log("after-1100004", arr[1100004].tag);
console.log("key-1000004", Object.keys(arr).indexOf("1000004") >= 0);
console.log("key-1100004", Object.keys(arr).indexOf("1100004") >= 0);
