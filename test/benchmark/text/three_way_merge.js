// Text benchmark: line-level and word-level three-way merge of related texts.

const MERGE_ROUNDS = 11000;
const LINE_COUNT = 768;
const MODULUS = 1000000007;

function buildBase() {
  const lines = [];
  for (let index = 0; index < LINE_COUNT; index += 1) {
    lines.push(
      "section " + index +
      " records the base document with stable words for merging and review",
    );
  }
  return lines;
}

function makeVariant(base, side) {
  const lines = [];
  for (let index = 0; index < base.length; index += 1) {
    let line = base[index];
    if (index % 17 === 0) {
      line += " " + side + " edit " + (index % 31) + " keeps the paragraph useful";
    } else if (side === "left" && index % 23 === 0) {
      line += " left-only annotation";
    } else if (side === "right" && index % 29 === 0) {
      line += " right-only annotation";
    }
    lines.push(line);
  }
  return lines;
}

const base = buildBase();
const left = makeVariant(base, "left");
const right = makeVariant(base, "right");

function wordAt(words, index) {
  return index < words.length ? words[index] : "";
}

function mergeWords(baseLine, leftLine, rightLine) {
  if (leftLine === rightLine) return leftLine;
  if (leftLine === baseLine) return rightLine;
  if (rightLine === baseLine) return leftLine;
  const baseWords = baseLine.split(" ");
  const leftWords = leftLine.split(" ");
  const rightWords = rightLine.split(" ");
  const count = Math.max(baseWords.length, leftWords.length, rightWords.length);
  const words = [];
  for (let index = 0; index < count; index += 1) {
    const baseWord = wordAt(baseWords, index);
    const leftWord = wordAt(leftWords, index);
    const rightWord = wordAt(rightWords, index);
    if (leftWord === rightWord) words.push(leftWord);
    else if (leftWord === baseWord) words.push(rightWord);
    else if (rightWord === baseWord) words.push(leftWord);
    else {
      words.push("<<<<<<< LEFT", leftWord, "=======", rightWord, ">>>>>>> RIGHT");
    }
  }
  return words.join(" ");
}

function mergeLines(baseLines, leftLines, rightLines) {
  const merged = [];
  for (let index = 0; index < baseLines.length; index += 1) {
    const baseLine = baseLines[index];
    const leftLine = leftLines[index];
    const rightLine = rightLines[index];
    if (leftLine === rightLine) merged.push(leftLine);
    else if (leftLine === baseLine) merged.push(rightLine);
    else if (rightLine === baseLine) merged.push(leftLine);
    else merged.push(mergeWords(baseLine, leftLine, rightLine));
  }
  return merged.join("\n");
}

let checksum = 0;
const t0 = process.hrtime.bigint();
for (let round = 0; round < MERGE_ROUNDS; round += 1) {
  const merged = mergeLines(base, left, right);
  checksum =
    (checksum + merged.length * 31 + merged.charCodeAt((round * 37) % merged.length)) % MODULUS;
}
const t1 = process.hrtime.bigint();

if (checksum !== 342313356) throw new Error("unexpected three_way_merge checksum");
process.stdout.write("three_way_merge: CHECKSUM:" + checksum + "\n");
process.stdout.write("__TIMING__:" + Number(t1 - t0) / 1e6 + "\n");
