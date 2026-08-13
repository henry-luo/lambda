// D5.3.3: for-in key collection and per-key liveness checks allocate while
// retaining the receiver, prototype-chain cursor, result, descriptor, and key.
const proto = {protoKeep: "keep", inherited: "delete-before-visit"};
const object = Object.create(proto);
object[10] = "ten";
object[1] = "one";
object.own = "own";

const seen = [];
for (const key in object) {
  seen.push(key);
  if (key === "1") delete proto.inherited;
}

if (seen.join(",") !== "1,10,own,protoKeep") {
  throw new Error("for-in roots or live-key filtering regressed: " + seen.join(","));
}
console.log("for-in-roots-ok");
