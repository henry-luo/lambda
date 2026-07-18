// Regression: semi-native loop tests must re-evaluate a member-expression
// bound after the body mutates its owner.
const shrinking = [{index: 1}];
let removed = 0;
for (let i = 0; i < shrinking.length;) {
    removed++;
    shrinking.splice(i, 1);
}
console.log("shrink=" + removed + "," + shrinking.length);

const growing = [];
growing.limit = 1;
for (let i = 0; i < growing.limit; i++) {
    growing.push(i);
    if (i === 0) growing.limit = 3;
}
console.log("grow=" + growing.length + "," + growing.limit);
