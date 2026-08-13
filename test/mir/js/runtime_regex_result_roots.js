// D5.3.3: RegExp result, named-groups, indices, keys, and capture values stay
// rooted while property creation allocates and may collect.
const expression = /(?<left>a)(?<right>b)?/dg;
const match = expression.exec("ab");
if (!match || match[0] !== "ab" || match.groups.left !== "a" ||
    match.groups.right !== "b") {
  throw new Error("named RegExp capture result was not preserved");
}
if (!match.indices || match.indices[0][0] !== 0 || match.indices[0][1] !== 2 ||
    match.indices.groups.left[0] !== 0 || match.indices.groups.left[1] !== 1 ||
    match.indices.groups.right[0] !== 1 || match.indices.groups.right[1] !== 2) {
  throw new Error("RegExp indices result was not preserved");
}
console.log("regex-result-roots-ok");
