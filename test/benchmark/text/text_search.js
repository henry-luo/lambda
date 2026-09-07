// Text benchmark: repeated naive, KMP, and Boyer–Moore substring searches.

const SEARCH_ROUNDS = 1536;
const MODULUS = 1000000007;

function buildCorpus() {
  const rows = [];
  for (let index = 0; index < 512; index += 1) {
    rows.push(
      "record-" + index +
      " alpha aaaaaaaaaaaaaaaaaaaaaaaa token-" + (index % 23) +
      " omega needle-" + (index % 11),
    );
  }
  return rows.join("\n");
}

const corpus = buildCorpus();
const patterns = [
  "record-0 alpha",
  "record-2048 alpha",
  "token-22 omega",
  "needle-10",
  "omega needle-7",
  "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3",
  "missing-marker",
  "record-2047 omega",
];

function toCodes(value) {
  const codes = [];
  for (let index = 0; index < value.length; index += 1) {
    codes.push(value.charCodeAt(index));
  }
  return codes;
}

const corpusCodes = toCodes(corpus);
const patternCodes = patterns.map(toCodes);

function naiveSearch(text, pattern, start) {
  if (pattern.length === 0) return start;
  for (let position = start; position <= text.length - pattern.length; position += 1) {
    let offset = 0;
    while (
      offset < pattern.length &&
      text[position + offset] === pattern[offset]
    ) {
      offset += 1;
    }
    if (offset === pattern.length) return position;
  }
  return -1;
}

function prefixTable(pattern) {
  const table = [];
  for (let index = 0; index < pattern.length; index += 1) table.push(0);
  let length = 0;
  let index = 1;
  while (index < pattern.length) {
    if (pattern[index] === pattern[length]) {
      length += 1;
      table[index] = length;
      index += 1;
    } else if (length > 0) {
      length = table[length - 1];
    } else {
      index += 1;
    }
  }
  return table;
}

function kmpSearch(text, pattern, start) {
  if (pattern.length === 0) return start;
  const table = prefixTable(pattern);
  let textIndex = start;
  let patternIndex = 0;
  while (textIndex < text.length) {
    if (text[textIndex] === pattern[patternIndex]) {
      textIndex += 1;
      patternIndex += 1;
      if (patternIndex === pattern.length) return textIndex - pattern.length;
    } else if (patternIndex > 0) {
      patternIndex = table[patternIndex - 1];
    } else {
      textIndex += 1;
    }
  }
  return -1;
}

function boyerMooreSearch(text, pattern, start) {
  if (pattern.length === 0) return start;
  const last = [];
  for (let index = 0; index < 256; index += 1) last.push(-1);
  for (let index = 0; index < pattern.length; index += 1) {
    last[pattern[index]] = index;
  }
  let position = start;
  while (position <= text.length - pattern.length) {
    let offset = pattern.length - 1;
    while (
      offset >= 0 &&
      text[position + offset] === pattern[offset]
    ) {
      offset -= 1;
    }
    if (offset < 0) return position;
    const previous = last[text[position + offset]];
    position += Math.max(1, offset - previous);
  }
  return -1;
}

let checksum = 0;
const t0 = process.hrtime.bigint();
for (let round = 0; round < SEARCH_ROUNDS; round += 1) {
  for (let index = 0; index < patterns.length; index += 1) {
    const start = (round * 17 + index * 13) % 97;
    const naive = naiveSearch(corpusCodes, patternCodes[index], start);
    const kmp = kmpSearch(corpusCodes, patternCodes[index], start);
    const boyerMoore = boyerMooreSearch(corpusCodes, patternCodes[index], start);
    if (naive !== kmp || kmp !== boyerMoore) {
      throw new Error("search algorithms disagree");
    }
    checksum =
      (checksum + (naive + 2) * (index + 3) + (round + 1) * 7) % MODULUS;
  }
}
const t1 = process.hrtime.bigint();

if (checksum !== 91395120) throw new Error("unexpected text_search checksum");
process.stdout.write("text_search: CHECKSUM:" + checksum + "\n");
process.stdout.write("__TIMING__:" + Number(t1 - t0) / 1e6 + "\n");
