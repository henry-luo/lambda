
// Text benchmark: microdiff's recursive object/array snapshot diff.
// The workload compares four nested document-state pairs 512 times.
globalThis.global = globalThis;
globalThis.self = globalThis;
var module = { exports: {} };
var exports = module.exports;
"use strict";
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.default = diff;
const richTypes = { Date: true, RegExp: true, String: true, Number: true };
function diff(obj, newObj, options = { cyclesFix: true }, _stack = []) {
    let diffs = [];
    const isObjArray = Array.isArray(obj);
    for (const key in obj) {
        const objKey = obj[key];
        const path = isObjArray ? +key : key;
        if (!(key in newObj)) {
            diffs.push({
                type: "REMOVE",
                path: [path],
                oldValue: obj[key],
            });
            continue;
        }
        const newObjKey = newObj[key];
        const areCompatibleObjects = typeof objKey === "object" &&
            typeof newObjKey === "object" &&
            Array.isArray(objKey) === Array.isArray(newObjKey);
        if (objKey &&
            newObjKey &&
            areCompatibleObjects &&
            !richTypes[Object.getPrototypeOf(objKey)?.constructor?.name] &&
            (!options.cyclesFix || !_stack.includes(objKey))) {
            diffs.push.apply(diffs, diff(objKey, newObjKey, options, options.cyclesFix ? _stack.concat([objKey]) : []).map((difference) => {
                difference.path.unshift(path);
                return difference;
            }));
        }
        else if (objKey !== newObjKey &&
            // treat NaN values as equivalent
            !(Number.isNaN(objKey) && Number.isNaN(newObjKey)) &&
            !(areCompatibleObjects &&
                (isNaN(objKey)
                    ? objKey + "" === newObjKey + ""
                    : +objKey === +newObjKey))) {
            diffs.push({
                path: [path],
                type: "CHANGE",
                value: newObjKey,
                oldValue: objKey,
            });
        }
    }
    const isNewObjArray = Array.isArray(newObj);
    for (const key in newObj) {
        if (!(key in obj)) {
            diffs.push({
                type: "CREATE",
                path: [isNewObjArray ? +key : key],
                value: newObj[key],
            });
        }
    }
    return diffs;
}

var microdiff = module.exports.default;


var microdiff_pairs = [];
function make_microdiff_snapshot(version) {
  return {
    document: {
      title: version ? "Text benchmark — revised" : "Text benchmark",
      sections: [
        {
          id: "intro",
          blocks: [
            { type: "paragraph", text: "A short paragraph of source text." },
            { type: "code", language: "js", lines: version ? 18 : 12 }
          ]
        },
        {
          id: "body",
          blocks: [
            { type: "heading", level: version ? 2 : 1, text: "Algorithms" },
            { type: "list", items: version ? ["diff", "snapshot", "hyphen"] : ["diff", "snapshot"] }
          ]
        }
      ]
    },
    options: {
      theme: version ? "dark" : "light",
      flags: { trackChanges: !!version, preserveWhitespace: true }
    },
    tags: version ? ["text", "benchmark", "updated"] : ["text", "benchmark"],
    updated: new Date(version ? 1700000001000 : 1700000000000),
    pattern: version ? /source|text|diff/gi : /source|text/g,
    value: version ? 42 : 41
  };
}
for (var microdiff_pair_index = 0; microdiff_pair_index < 4; microdiff_pair_index++) {
  microdiff_pairs.push([
    make_microdiff_snapshot(microdiff_pair_index % 2 === 0),
    make_microdiff_snapshot(microdiff_pair_index % 2 !== 0)
  ]);
}

var microdiff_checksum = 0;
var microdiff_rounds = 512;
var microdiff_t0 = process.hrtime.bigint();
for (var microdiff_round = 0; microdiff_round < microdiff_rounds; microdiff_round++) {
  for (var microdiff_index = 0; microdiff_index < microdiff_pairs.length; microdiff_index++) {
    var microdiff_result = microdiff(
      microdiff_pairs[microdiff_index][0],
      microdiff_pairs[microdiff_index][1]
    );
    microdiff_checksum =
      (microdiff_checksum + microdiff_result.length * 19) % 1000000007;
    for (var microdiff_change = 0; microdiff_change < microdiff_result.length; microdiff_change++) {
      var microdiff_path = microdiff_result[microdiff_change].path;
      microdiff_checksum =
        (microdiff_checksum +
          microdiff_result[microdiff_change].type.length * 23 +
          microdiff_path.length) %
        1000000007;
    }
  }
}
var microdiff_t1 = process.hrtime.bigint();
if (microdiff_checksum === 0) {
  throw new Error("microdiff benchmark produced an empty checksum");
}
console.log("CHECKSUM:" + microdiff_checksum);
console.log(
  "__TIMING__:" +
    (Number(microdiff_t1 - microdiff_t0) / 1000000).toFixed(3)
);
