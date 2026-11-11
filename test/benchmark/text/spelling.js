// Decode Word's vocabulary once; time tokenization, lookup and issue construction.
const fs = require("fs");
const dictionary = new Set(["Lambda", "Radiant"]);
const encoded = fs.readFileSync("lmd/package/doc/spelling/data/en-US.dict", "utf8");
for (const line of encoded.trimEnd().split("\n")) {
  const tokens = line.split(/([;|])/);
  let previous = "";
  let delta = false;
  for (let i = 0; i < tokens.length; i += 2) {
    const token = tokens[i];
    if (!delta) previous = token;
    else {
      const comma = token.indexOf(",");
      const count = comma < 0 ? Array.from(previous).length : Number(token.slice(0, comma));
      previous = Array.from(previous).slice(0, count).join("") + token.slice(comma + 1);
    }
    dictionary.add(previous);
    delta = tokens[i + 1] === ";";
  }
}
const text = fs.readFileSync("test/benchmark/text/spelling_document.txt", "utf8");
const expected = JSON.parse(fs.readFileSync("test/benchmark/text/spelling_expected.json", "utf8"));
const t0 = performance.now();
const issues = [];
let checked = 0;
for (const match of text.matchAll(/[A-Za-z]+(?:'[A-Za-z]+)*/g)) {
  const word = match[0];
  checked++;
  const lower = word.toLowerCase();
  const folded = word === word.toUpperCase() || word === word[0].toUpperCase() + lower.slice(1);
  if (!dictionary.has(word) && !(folded && dictionary.has(lower))) {
    issues.push({word, start: match.index, end: match.index + word.length});
  }
}
const elapsed = performance.now() - t0;
if (checked !== 7110 || JSON.stringify(issues) !== JSON.stringify(expected)) throw new Error("spelling: FAIL result mismatch");
console.log("spelling: CHECKED:7110 ISSUES:720");
console.log("__TIMING__:" + elapsed);
