// microdiff 1.5.0 Library Support Tests
// The published CommonJS entry point is embedded to exercise the real package.
// Source: https://github.com/AsyncBanana/microdiff
globalThis.global = globalThis;
globalThis.self = globalThis;
var module = { exports: {} };
var exports = module.exports;
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


// === Test 1: export and empty diff ===
console.log(typeof microdiff);
console.log(JSON.stringify(microdiff({}, {})));

// === Test 2: nested object and array changes ===
var before = {
  user: { name: "Ann", age: 30 },
  tags: ["a", "b"],
  removed: true,
};
var after = {
  user: { name: "Ana", age: 31 },
  tags: ["a", "c", "d"],
  added: "yes",
};
console.log(JSON.stringify(microdiff(before, after)));
console.log(JSON.stringify(before));

// === Test 3: array index changes and creation ===
console.log(JSON.stringify(microdiff([1, 2, 3], [1, 3, 4, 5])));

// === Test 4: deep paths and explicit options ===
console.log(JSON.stringify(microdiff(
  { config: { flags: { dark: false } } },
  { config: { flags: { dark: true }, mode: "safe" } }
)));
console.log(JSON.stringify(microdiff(
  { config: { flags: { dark: false } } },
  { config: { flags: { dark: true }, mode: "safe" } },
  { cyclesFix: false }
)));

// === Test 5: Date, RegExp, and NaN atomic values ===
var same_date_a = new Date(1700000000000);
var same_date_b = new Date(1700000000000);
var same_regex_a = /abc/g;
var same_regex_b = /abc/g;
console.log(JSON.stringify(microdiff(
  { when: same_date_a, pattern: same_regex_a, value: NaN },
  { when: same_date_b, pattern: same_regex_b, value: NaN }
)));
console.log(JSON.stringify(microdiff(
  { when: new Date(1700000000000), pattern: /abc/g },
  { when: new Date(1700000000001), pattern: /xyz/i }
)));

// === Test 6: cyclic object references ===
var cycle_before = { name: "old" };
cycle_before.self = cycle_before;
var cycle_after = { name: "new" };
cycle_after.self = cycle_after;
console.log(JSON.stringify(microdiff(cycle_before, cycle_after)));

// === Tests 7-15: ported from the upstream tests/ directory ===
function upstream_microdiff_case(name, result) {
  console.log("UPSTREAM_" + name + "=" + JSON.stringify(result));
}

// tests/arrays.js
upstream_microdiff_case(
  "arrays_top_level",
  microdiff(["test", "testing"], ["test"])
);
upstream_microdiff_case(
  "arrays_nested",
  microdiff(["test", ["test"]], ["test", ["test", "test2"]])
);
upstream_microdiff_case(
  "arrays_object_in_array_in_object",
  microdiff(
    { test: ["test", { test2: true }] },
    { test: ["test", { test2: false }] }
  )
);
upstream_microdiff_case(
  "arrays_array_to_object",
  microdiff({ data: [] }, { data: { val: "test" } })
);

// tests/basic.js
upstream_microdiff_case(
  "basic_new_raw_value",
  microdiff({ test: true }, { test: true, test2: true })
);
upstream_microdiff_case(
  "basic_change_raw_value",
  microdiff({ test: true }, { test: false })
);
upstream_microdiff_case(
  "basic_remove_raw_value",
  microdiff({ test: true, test2: true }, { test: true })
);
upstream_microdiff_case(
  "basic_replace_object_with_null",
  microdiff({ object: { test: true } }, { object: null })
);
upstream_microdiff_case(
  "basic_replace_null_with_object",
  microdiff({ object: null }, { object: { test: true } })
);
upstream_microdiff_case(
  "basic_replace_object_with_value",
  microdiff({ object: { test: true } }, { object: "string" })
);
upstream_microdiff_case(
  "basic_equal_null_prototype_objects",
  microdiff(Object.create(null), Object.create(null))
);
var null_prototype_old = Object.create(null);
var null_prototype_new = Object.create(null);
null_prototype_new.test = true;
upstream_microdiff_case(
  "basic_unequal_null_prototype_objects",
  microdiff(null_prototype_old, null_prototype_new)
);

// tests/class-primitives.js
upstream_microdiff_case(
  "class_equal_string",
  microdiff({ string: new String("hi") }, { string: new String("hi") })
);
upstream_microdiff_case(
  "class_equal_number",
  microdiff({ number: new Number(1) }, { number: new Number(1) })
);
upstream_microdiff_case(
  "class_unequal_number",
  microdiff({ number: new Number(1) }, { number: new Number(2) })
);

// tests/cycles.js
var recursive_object = {};
recursive_object.a = recursive_object;
upstream_microdiff_case(
  "cycles_recursive_reference",
  microdiff(recursive_object, recursive_object)
);
var nested_recursive_object = { a: {} };
nested_recursive_object.a.b = nested_recursive_object;
upstream_microdiff_case(
  "cycles_recursive_reference_two_levels",
  microdiff(nested_recursive_object, nested_recursive_object)
);

// tests/dates.js
upstream_microdiff_case(
  "dates_equal",
  microdiff({ date: new Date(1) }, { date: new Date(1) })
);
upstream_microdiff_case(
  "dates_unequal",
  microdiff({ date: new Date(1) }, { date: new Date(2) })
);
upstream_microdiff_case(
  "dates_to_string",
  microdiff({ date: new Date(1) }, { date: "not date" })
);
upstream_microdiff_case(
  "dates_from_string",
  microdiff({ date: "not date" }, { date: new Date(1) })
);

// tests/nan.js
upstream_microdiff_case(
  "nan_new_object",
  microdiff({}, { testNaN: NaN })
);
upstream_microdiff_case(
  "nan_change_object",
  microdiff({ testNaN: NaN }, { testNaN: 0 })
);
upstream_microdiff_case(
  "nan_equal_object",
  microdiff({ testNaN: NaN }, { testNaN: NaN })
);
upstream_microdiff_case(
  "nan_remove_object",
  microdiff({ testNaN: NaN }, {})
);
upstream_microdiff_case(
  "nan_new_array",
  microdiff([], [NaN])
);
upstream_microdiff_case(
  "nan_change_array",
  microdiff([NaN], [0])
);
upstream_microdiff_case(
  "nan_equal_array",
  microdiff([NaN], [NaN])
);

// tests/regex.js
upstream_microdiff_case(
  "regex_equal",
  microdiff({ regex: /a/ }, { regex: /a/ })
);
upstream_microdiff_case(
  "regex_unequal",
  microdiff({ regex: /a/ }, { regex: /b/ })
);

// tests/temporal.js
if (typeof Temporal === "undefined") {
  console.log("UPSTREAM_temporal=SKIPPED_NO_TEMPORAL");
} else {
  upstream_microdiff_case(
    "temporal_plain_date_equal",
    microdiff(
      { date: Temporal.PlainDate.from("2024-01-15") },
      { date: Temporal.PlainDate.from("2024-01-15") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_date_unequal",
    microdiff(
      { date: Temporal.PlainDate.from("2024-01-15") },
      { date: Temporal.PlainDate.from("2024-06-01") }
    )
  );
  upstream_microdiff_case(
    "temporal_instant_equal",
    microdiff(
      { ts: Temporal.Instant.from("2024-01-01T00:00:00Z") },
      { ts: Temporal.Instant.from("2024-01-01T00:00:00Z") }
    )
  );
  upstream_microdiff_case(
    "temporal_instant_unequal",
    microdiff(
      { ts: Temporal.Instant.from("2024-01-01T00:00:00Z") },
      { ts: Temporal.Instant.from("2024-06-01T12:00:00Z") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_datetime_equal",
    microdiff(
      { dt: Temporal.PlainDateTime.from("2024-01-15T10:30:00") },
      { dt: Temporal.PlainDateTime.from("2024-01-15T10:30:00") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_datetime_unequal",
    microdiff(
      { dt: Temporal.PlainDateTime.from("2024-01-15T10:30:00") },
      { dt: Temporal.PlainDateTime.from("2024-01-15T11:00:00") }
    )
  );
  upstream_microdiff_case(
    "temporal_zoned_datetime_equal",
    microdiff(
      { zdt: Temporal.ZonedDateTime.from("2024-01-15T10:30:00[UTC]") },
      { zdt: Temporal.ZonedDateTime.from("2024-01-15T10:30:00[UTC]") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_time_equal",
    microdiff(
      { time: Temporal.PlainTime.from("10:30:00") },
      { time: Temporal.PlainTime.from("10:30:00") }
    )
  );
  upstream_microdiff_case(
    "temporal_duration_equal",
    microdiff(
      { dur: Temporal.Duration.from({ hours: 1, minutes: 30 }) },
      { dur: Temporal.Duration.from({ hours: 1, minutes: 30 }) }
    )
  );
  upstream_microdiff_case(
    "temporal_duration_unequal",
    microdiff(
      { dur: Temporal.Duration.from({ hours: 1 }) },
      { dur: Temporal.Duration.from({ hours: 2 }) }
    )
  );
  upstream_microdiff_case(
    "temporal_value_to_string",
    microdiff(
      { date: Temporal.PlainDate.from("2024-01-15") },
      { date: "2024-01-15" }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_year_month_equal",
    microdiff(
      { ym: Temporal.PlainYearMonth.from("2024-01") },
      { ym: Temporal.PlainYearMonth.from("2024-01") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_year_month_unequal",
    microdiff(
      { ym: Temporal.PlainYearMonth.from("2024-01") },
      { ym: Temporal.PlainYearMonth.from("2024-06") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_month_day_equal",
    microdiff(
      { md: Temporal.PlainMonthDay.from("01-15") },
      { md: Temporal.PlainMonthDay.from("01-15") }
    )
  );
  upstream_microdiff_case(
    "temporal_plain_month_day_unequal",
    microdiff(
      { md: Temporal.PlainMonthDay.from("01-15") },
      { md: Temporal.PlainMonthDay.from("06-01") }
    )
  );
  upstream_microdiff_case(
    "temporal_string_to_value",
    microdiff(
      { date: "2024-01-15" },
      { date: Temporal.PlainDate.from("2024-01-15") }
    )
  );
}

console.log("MICRODIFF_DONE");
