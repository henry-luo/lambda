// Observable strings are GC values, not NamePool records.  This exceeds a
// single NameId segment and must not fail or consume dynamic property ids.
var last = "";
for (var index = 0; index < 70000; index++) {
    last = String(index);
}
console.log(last);
console.log("OK");
