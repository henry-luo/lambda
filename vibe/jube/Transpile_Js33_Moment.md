# Gap Analysis: Moment.js Support in Lambda JS

## Summary

| Library | Version | Status | Key Blocker |
|---------|---------|--------|-------------|
| **Moment.js** | 2.30.1 | ✅ **Expected Full** | None — all required features are implemented |

Moment.js is an **ES5 library** with no ES6+ dependencies (no classes, arrow functions, template literals, destructuring, Map/Set, Promise, or Proxy). The only ES6 usage is a guarded `Symbol.for('nodejs.util.inspect.custom')` check behind `typeof Symbol !== 'undefined'`.

## Feature Requirements vs Lambda JS Support

### Core JavaScript Features

| Feature | Moment.js Usage | Lambda JS | Status |
|---------|----------------|-----------|--------|
| `var` declarations | Everywhere (no `let`/`const`) | ✅ | No gap |
| `for..in` loops | Property enumeration on objects | ✅ | No gap |
| `for` loops | Standard iteration | ✅ | No gap |
| `typeof` operator | Type detection (`typeof x === 'string'`, etc.) | ✅ | No gap |
| `instanceof` | `instanceof Date`, `instanceof Array`, `instanceof Function`, `instanceof Moment` | ✅ | No gap — user constructors supported |
| `delete` operator | `delete obj[prop]` in locale management | ✅ | No gap |
| `void` operator | `void (isUTC ? d.setUTCFoo() : d.setFoo())` | ✅ | No gap |
| `arguments` object | `arguments.length`, `arguments[i]`, `.apply(null, arguments)` | ✅ | No gap |
| `try/catch` | `require('./locale/' + name)` guard | ✅ | No gap |
| Ternary `?:` | Extensive | ✅ | No gap |
| `switch` statement | Date getter/setter dispatch | ✅ | No gap |
| Comma operator | Used in a few places | ✅ | No gap |

### Constructor & Prototype Patterns

| Feature | Moment.js Usage | Lambda JS | Status |
|---------|----------------|-----------|--------|
| Constructor functions with `new` | `new Moment(config)`, `new Locale(config)`, `new Duration(...)` | ✅ | No gap |
| `this` binding in constructors | `this._d = new Date(...)`, `this._data = {}`, etc. | ✅ | No gap |
| Prototype assignment | `Moment.prototype.add = add; proto.clone = clone;` (~80 methods) | ✅ | No gap |
| `instanceof` user constructors | `obj instanceof Moment`, `obj instanceof Duration` | ✅ | **Test needed** — relies on `__proto__` chain |
| IIFE factory pattern | `(function(global, factory) { global.moment = factory() }(this, ...))` | ✅ | No gap — `this` → globalThis in sloppy mode |

### Date Object

Moment.js is fundamentally a Date wrapper. It uses **every Date method**.

| Category | Methods Used | Lambda JS | Status |
|----------|-------------|-----------|--------|
| Constructors | `new Date()`, `new Date(ms)`, `new Date(string)`, `new Date(y,m,d,h,M,s,ms)` | ✅ All 4 forms | No gap |
| Static | `Date.now()`, `Date.UTC(y,m,d,h,M,s,ms)`, `Date.parse(string)` | ✅ All 3 | No gap |
| Local getters | `getFullYear`, `getMonth`, `getDate`, `getDay`, `getHours`, `getMinutes`, `getSeconds`, `getMilliseconds`, `getTime`, `getTimezoneOffset` | ✅ All 10 | No gap |
| UTC getters | `getUTCFullYear`, `getUTCMonth`, `getUTCDate`, `getUTCDay`, `getUTCHours`, `getUTCMinutes`, `getUTCSeconds`, `getUTCMilliseconds` | ✅ All 8 | No gap |
| Local setters | `setFullYear`, `setMonth`, `setDate`, `setHours`, `setMinutes`, `setSeconds`, `setMilliseconds`, `setTime` | ✅ All 8 | No gap |
| UTC setters | `setUTCFullYear`, `setUTCMonth`, `setUTCDate`, `setUTCHours`, `setUTCMinutes`, `setUTCSeconds`, `setUTCMilliseconds` | ✅ All 7 | No gap |
| Formatting | `toISOString()`, `valueOf()`, `toString()`, `toJSON()` | ✅ All 4 | No gap |
| Coercion | `+new Date()`, `isNaN(date)`, `isFinite(date)` | ✅ | No gap |

**Multi-arg setters**: Moment.js calls `d.setFullYear(year, month, date)` (3 args) and `d.setMonth(month, date)` (2 args). Lambda JS supports multi-arg setters. ✅

### RegExp

| Feature | Moment.js Usage | Lambda JS (RE2) | Status |
|---------|----------------|-----------------|--------|
| RegExp literals | ~30 patterns for parsing dates, formats, tokens | ✅ | No gap |
| `new RegExp(pattern, flags)` | Dynamic locale-based patterns | ✅ | No gap |
| `.exec(string)` | ISO/RFC parsing with capture groups | ✅ | No gap |
| `.test(string)` | Format token matching, month/weekday detection | ✅ | No gap |
| `.source` property | Composing regex from parts: `this._ordinalParse.source` | ✅ | No gap |
| `.lastIndex` read/write | `localFormattingTokens.lastIndex = 0` between uses | ✅ | No gap |
| `g` flag (global) | `formattingTokens` matching | ✅ | No gap |
| `i` flag (case-insensitive) | Month/weekday name matching | ✅ | No gap |
| Non-capturing groups `(?:...)` | ISO 8601 and RFC 2822 patterns | ✅ (RE2 native) | No gap |
| Non-greedy quantifiers `*?`, `+?` | Unicode word matching (`\s*?`) | ✅ (RE2 native) | No gap |
| Unicode `\uXXXX` in ranges | `[\u00A0-\u05FF]` character classes | ✅ (preprocessor converts) | No gap |
| Up to 10 capture groups | RFC 2822 pattern | ✅ (max 16 supported) | No gap |
| Lookahead/lookbehind | **Not used** | N/A | No gap |
| Back-references | **Not used** | N/A | No gap |

**All 12 regex patterns in moment.js are RE2-compatible.** Unlike lodash (which uses lookahead), moment.js uses only standard regex features.

### String Methods

| Method | Used By | Lambda JS | Status |
|--------|---------|-----------|--------|
| `.toLowerCase()` / `.toUpperCase()` | Locale parsing, unit normalization | ✅ | No gap |
| `.toLocaleLowerCase()` | Month/weekday name comparison | ✅ | No gap |
| `.indexOf()` / `.lastIndexOf()` | String searching | ✅ | No gap |
| `.charAt()` | AM/PM detection | ✅ | No gap |
| `.substr()` | Token extraction from format strings | ✅ | No gap |
| `.substring()` | Used in a few places | ✅ | No gap |
| `.slice()` | String manipulation | ✅ | No gap |
| `.match()` | Format token splitting | ✅ | No gap |
| `.replace()` | Format processing, regex escaping | ✅ | No gap |
| `.split()` | `'Jan_Feb_..._Dec'.split('_')` | ✅ | No gap |
| `.trim()` | Not directly used (but polyfilled patterns exist) | ✅ | No gap |

### Array Methods

| Method | Used By | Lambda JS | Status |
|--------|---------|-----------|--------|
| `.push()` | Building arrays throughout | ✅ | No gap |
| `.slice()` | `Array.prototype.slice.call(arguments)` | ✅ | No gap |
| `.sort()` | Sorting locale pieces by length | ✅ | No gap |
| `.indexOf()` | Inline polyfill (checks `Array.prototype.indexOf` first) | ✅ | No gap |
| `.some()` | Inline polyfill (checks `Array.prototype.some` first) | ✅ | No gap |
| `.join()` | Building regex from month/weekday names | ✅ | No gap |
| `.concat()` | `ws.slice(n, 7).concat(ws.slice(0, n))` | ✅ | No gap |
| `.filter()` | `isNumberOrStringArray` check | ✅ | No gap |
| `.map()` | `.match(formattingTokens).map(fn)` | ✅ | No gap |
| `.forEach()` | `localeFamilies[name].forEach(...)` | ✅ | No gap |
| `.length` | Everywhere | ✅ | No gap |

### Object Methods

| Method | Used By | Lambda JS | Status |
|--------|---------|-----------|--------|
| `Object.prototype.toString.call(x)` | Type detection: `[object Array]`, `[object Object]`, `[object Number]`, `[object Date]`, `[object Function]` | ✅ All tags correct | No gap |
| `Object.prototype.hasOwnProperty.call(a, b)` | Property checking throughout | ✅ | No gap |
| `Object.keys()` | Inline polyfill (checks `Object.keys` first) | ✅ | No gap |
| `Object.getOwnPropertyNames()` | `isObjectEmpty()` check | ✅ | No gap |
| `Object.isFrozen()` | `isValid()` guard | ✅ | No gap |
| `Object.assign()` | `humanize()` threshold merging | ✅ | No gap |

### Math Methods

| Method | Used By | Lambda JS | Status |
|--------|---------|-----------|--------|
| `Math.abs()` | Duration, offset, zeroFill | ✅ | No gap |
| `Math.floor()` | `absFloor`, week calculations | ✅ | No gap |
| `Math.ceil()` | `absCeil`, quarter calculations | ✅ | No gap |
| `Math.round()` | Duration rounding, timezone offset | ✅ | No gap |
| `Math.pow()` | `zeroFill` zero-padding | ✅ | No gap |
| `Math.min()` | Array/locale comparison | ✅ | No gap |
| `Math.max()` | `zeroFill` padding length | ✅ | No gap |

### Function Methods

| Method | Used By | Lambda JS | Status |
|--------|---------|-----------|--------|
| `.apply(null, arguments)` | `hookCallback.apply(null, arguments)`, `Date.UTC.apply(null, args)` | ✅ | No gap |
| `.call(this, ...)` | `some.call(flags.parsedDateParts, fn)`, `Object.prototype.toString.call(x)` | ✅ | No gap |

Note: `.bind()` is **not used** by moment.js.

### Global Functions

| Function | Used By | Lambda JS | Status |
|----------|---------|-----------|--------|
| `parseInt(str, radix)` | Year/month/day parsing | ✅ | No gap |
| `parseFloat(str)` | Duration parsing, timestamp parsing | ✅ | No gap |
| `isNaN(value)` | Date validation | ✅ | No gap |
| `isFinite(value)` | `toInt` coercion, year validation | ✅ | No gap |
| `NaN` | Invalid date sentinel | ✅ | No gap |
| `Infinity` / `-Infinity` | Era range sentinels | ✅ | No gap |

### Module System

| Pattern | Moment.js Usage | Lambda JS | Status |
|---------|----------------|-----------|--------|
| UMD wrapper | `typeof exports === 'object' ? module.exports = ... : global.moment = ...` | ✅ | CJS mode detects `module`/`exports`; standalone mode falls through to `global.moment` |
| Dynamic `require('./locale/' + name)` | Locale lazy loading | ⚠️ Graceful fallback | Locale files not on disk → caught by try/catch → only English locale |
| `console.warn()` | Deprecation warnings | ✅ | No gap |
| `new Error().stack` | Deprecation warning detail | ✅ | Stack format may differ from V8 |

### Special Patterns

| Pattern | Moment.js Usage | Lambda JS | Status |
|---------|----------------|-----------|--------|
| `Symbol.for('nodejs.util.inspect.custom')` | Guarded by `typeof Symbol !== 'undefined'` | ✅ | Symbol is supported; `Symbol.for` is implemented |
| Nested closures (factory IIFE) | Entire library is one big IIFE with deeply nested functions | ✅ | Closure capture analysis supports deep nesting |
| `~~(value)` (bitwise NOT NOT) | Used for truncation: `~~(this.millisecond() / 100)` | ✅ | Bitwise operators implemented |
| `(x > 0) - (x < 0) || +x` | Sign function | ✅ | Boolean-to-number coercion works |
| `.toFixed(3)` | Duration ISO string formatting | ✅ | `Number.prototype.toFixed` implemented |

## Potential Risk Areas (Low Risk)

### 1. `instanceof` with User-Defined Constructors
**Risk**: Medium-low
**Pattern**: `obj instanceof Moment`, `obj instanceof Duration`
**Details**: Moment.js defines `Moment` as a plain constructor function (not an ES6 class) and manually assigns prototype methods. The Jube engine supports `instanceof` with prototype chain walking. Since `new Moment(config)` creates objects with `__proto__` set to `Moment.prototype`, `instanceof` should resolve correctly. Needs testing to confirm.

### 2. Locale Loading
**Risk**: None (graceful degradation)
**Pattern**: `require('./locale/' + name)` inside try/catch
**Details**: Without locale files on disk, moment.js catches the error and marks the locale as not found. Only English (built-in) will be available. This is expected behavior for most use cases.

### 3. `Array.prototype.slice.call(arguments)`
**Risk**: Low
**Pattern**: Converting arguments object to array
**Details**: The `arguments` object in Jube is array-like with numeric indices. `Array.prototype.slice` via `.call()` should work through the runtime's method dispatch. If it doesn't, moment.js also uses `[].slice.call(arguments, 0)` which goes through the same path.

### 4. Performance
**Risk**: Low
**Pattern**: ~5600 lines of code, many closures, regex compilation
**Details**: Similar complexity to lodash (~35s debug build for lodash). Moment.js creates many regex objects dynamically and has deep closure chains. Compile time may be noticeable in debug builds.

## Comparison with Previous Libraries

| Library | Version | Status | Regex Issues | Key Challenges |
|---------|---------|--------|-------------|----------------|
| **jQuery 3.7.1** | ✅ Full | None | DOM shims |
| **highlight.js 11.9** | ✅ Full | None | Transpiler scope bugs |
| **Underscore 1.13.7** | ✅ Full (114/114) | None | Ternary/typeof, `with` stmt, Symbol tag |
| **Lodash 4.17.21** | ✅ Full (35/35) | RE2 lookahead (non-blocking) | Array/Object indirect call, var hoisting, prototype chain |
| **Moment.js 2.30.1** | ✅ Expected Full | **None** | `instanceof` user ctors (low risk) |

## Key Differences from Lodash/Underscore

Moment.js is a **significantly easier** target than lodash/underscore because:

1. **Pure ES5** — no minification, readable code, `var` everywhere
2. **No complex regex** — all patterns are RE2-compatible (no lookahead/lookbehind)
3. **No `.bind()`** — avoids the transpiler shortcut issue (Bug 9)
4. **No `with` statement** — avoids Bug 7
5. **No deeply nested minified scope** — avoids captured variable scope resolution issues
6. **Simple prototype pattern** — `Moment.prototype.method = fn` is straightforward
7. **Comprehensive Date support in Jube** — all 33 Date getter/setter methods are already implemented

## Recommended Test Plan

```bash
# 1. Download moment.js
curl -o test/js/moment.js https://raw.githubusercontent.com/moment/moment/develop/moment.js

# 2. Create test file: test/js/lib_moment.js
# Tests should cover: creation, formatting, parsing, manipulation, comparison, duration

# 3. Run test
./lambda.exe js test/js/lib_moment.js --no-log
```

### Suggested Test Cases

```js
// === Library Loading ===
console.log("moment version:", moment.version);  // "2.30.1"
console.log("moment type:", typeof moment);       // "function"

// === Creation ===
var now = moment();
console.log("now valid:", now.isValid());         // true
var d = moment("2024-03-15");
console.log("parsed:", d.format("YYYY-MM-DD"));   // "2024-03-15"
var u = moment.utc("2024-03-15T12:00:00Z");
console.log("utc:", u.format());                   // ISO format

// === Formatting ===
var m = moment("2024-01-15T14:30:45");
console.log("format LLLL:", m.format("dddd, MMMM D, YYYY h:mm A"));
console.log("format ISO:", m.toISOString());
console.log("unix:", m.unix());

// === Parsing ===
console.log("ISO:", moment("2024-03-15T10:30:00Z").isValid());       // true
console.log("RFC2822:", moment("Mon, 15 Mar 2024 10:30:00 +0000").isValid()); // true
console.log("unix ms:", moment(1710496200000).format("YYYY-MM-DD")); // date from ms

// === Manipulation ===
console.log("add 7 days:", moment("2024-03-15").add(7, 'days').format("YYYY-MM-DD"));  // "2024-03-22"
console.log("subtract 1 month:", moment("2024-03-15").subtract(1, 'month').format("YYYY-MM-DD")); // "2024-02-15"
console.log("startOf month:", moment("2024-03-15").startOf('month').format("YYYY-MM-DD")); // "2024-03-01"
console.log("endOf month:", moment("2024-03-15").endOf('month').format("YYYY-MM-DD"));     // "2024-03-31"

// === Getters/Setters ===
var g = moment("2024-06-15T14:30:45");
console.log("year:", g.year());                 // 2024
console.log("month:", g.month());               // 5 (0-based)
console.log("date:", g.date());                 // 15
console.log("day:", g.day());                   // 6 (Saturday)
console.log("hour:", g.hour());                 // 14
console.log("minute:", g.minute());             // 30
console.log("second:", g.second());             // 45

// === Comparison ===
var a = moment("2024-03-15");
var b = moment("2024-03-20");
console.log("isBefore:", a.isBefore(b));        // true
console.log("isAfter:", a.isAfter(b));          // false
console.log("isSame day:", a.isSame(b, 'month')); // true
console.log("diff days:", b.diff(a, 'days'));   // 5

// === Duration ===
var dur = moment.duration(2, 'hours');
console.log("duration hours:", dur.hours());     // 2
console.log("duration minutes:", dur.asMinutes()); // 120
console.log("humanize:", dur.humanize());        // "2 hours"
console.log("ISO duration:", moment.duration(90, 'seconds').toISOString()); // "PT1M30S"

// === Relative Time ===
console.log("fromNow:", moment("2024-03-15").fromNow());
console.log("from:", moment("2024-03-15").from(moment("2024-03-10"))); // "in 5 days"

// === isMoment / isDate ===
console.log("isMoment:", moment.isMoment(moment()));  // true
console.log("isMoment obj:", moment.isMoment({}));     // false
console.log("isDate:", moment.isDate(new Date()));     // true

// === Clone ===
var orig = moment("2024-03-15");
var cloned = orig.clone();
cloned.add(1, 'day');
console.log("orig:", orig.format("YYYY-MM-DD"));     // "2024-03-15" (unchanged)
console.log("cloned:", cloned.format("YYYY-MM-DD")); // "2024-03-16"

// === Leap Year ===
console.log("2024 leap:", moment([2024]).isLeapYear()); // true
console.log("2023 leap:", moment([2023]).isLeapYear()); // false

// === Quarter ===
console.log("quarter:", moment("2024-06-15").quarter()); // 2

// === Week ===
console.log("week:", moment("2024-03-15").week());
console.log("isoWeek:", moment("2024-03-15").isoWeek());
console.log("weekday:", moment("2024-03-15").weekday());

// === Calendar ===
console.log("calendar:", moment("2024-03-15").calendar(moment("2024-03-15")));

// === Chaining ===
console.log("chain:", moment("2024-03-15").add(1, 'month').subtract(2, 'days').format("YYYY-MM-DD"));
```

## Conclusion

Moment.js v2.30.1 should work **out of the box** with Lambda JS. The library's ES5-only codebase, conservative regex patterns, and comprehensive Date/String/Array usage map perfectly to Jube's already-complete built-in support. The only feature not exercised by previous libraries is `instanceof` with user-defined constructor functions (not ES6 classes), which is a low-risk area.

**Estimated effort**: Minimal — likely just test creation and verification, no engine changes expected.
