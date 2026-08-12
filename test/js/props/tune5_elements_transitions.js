// Tune5 elements-state lock: numeric arrays promote in place when an
// operation needs tagged presence or descriptor overlay semantics.
let packed = [1, 2, 3];
let packedAlias = packed;
console.log(packed === packedAlias, packed.length, packed[1],
            Object.hasOwn(packed, "1"), 1 in packed,
            Object.keys(packed).join(","));
packed[1] = undefined;
console.log(packed === packedAlias, packed.length, packed[1],
            Object.hasOwn(packed, "1"), 1 in packed,
            Object.keys(packed).join(","));

let holey = [4, 5, 6];
let holeyAlias = holey;
console.log(delete holey[1], holey === holeyAlias, holey.length,
            holey[1], Object.hasOwn(holey, "1"), 1 in holey,
            Object.keys(holey).join(","));

let gapped = [7, 8];
let gappedAlias = gapped;
gapped[5] = 12;
console.log(gapped === gappedAlias, gapped.length, gapped[2], gapped[5],
            Object.hasOwn(gapped, "2"), Object.hasOwn(gapped, "5"),
            Object.keys(gapped).join(","));

let named = [9, 10];
let namedAlias = named;
named.label = "ok";
console.log(named === namedAlias, named.length, named.label,
            Object.keys(named).join(","));
