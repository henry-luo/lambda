# Moment.js JavaScript Feature Dependencies

Analysis of moment.js v2.30.1 source code. Moment.js is a conservative ES5-era library with minimal ES6+ usage.

---

## 1. Built-in Objects/APIs Used

| Object/API | Usage |
|---|---|
| `Date` | Core dependency — wraps native `Date` internally (`this._d = new Date(...)`) |
| `RegExp` | Extensive — dozens of regex patterns for parsing date formats, ISO 8601, RFC 2822, locales |
| `Math` | `Math.abs`, `Math.floor`, `Math.ceil`, `Math.round`, `Math.pow`, `Math.min`, `Math.max` |
| `Object` | `Object.prototype.toString.call()`, `Object.prototype.hasOwnProperty.call()`, `Object.keys`, `Object.getOwnPropertyNames` |
| `Array` | `Array.prototype.some`, `Array.prototype.indexOf`, `Array.prototype.filter`, `[].slice.call()` |
| `Number` | `isFinite()`, `isNaN()`, `parseInt()`, `parseFloat()`, `+` unary coercion |
| `String` | `String` constructor check (`input instanceof String`) |
| `Function` | `Function` constructor check (`input instanceof Function`) |
| `NaN` | Used for invalid dates (`new Date(NaN)`) |
| `Infinity` | Used in era definitions (`+Infinity`, `-Infinity`) |
| `console` | `console.warn` for deprecation warnings |

---

## 2. Prototype Methods Relied On

### String.prototype
| Method | Context |
|---|---|
| `.match()` | Regex matching for date parsing, format tokens |
| `.replace()` | Format string processing, regex escaping, locale processing |
| `.split()` | Splitting locale month/weekday name strings (e.g., `'January_February_...'.split('_')`) |
| `.toLowerCase()` | Locale normalization, AM/PM parsing |
| `.charAt()` | AM/PM detection (`(input + '').toLowerCase().charAt(0) === 'p'`) |
| `.substr()` / `.substring()` | Extracting parts of parsed input (hour/minute/second from `hmm`, `hmmss`) |
| `.slice()` | Extracting substrings, `shiftWeekdays()` |
| `.length` | Length checks throughout |
| `.indexOf()` | (via string matching) |
| `.toFixed()` | ISO duration seconds formatting (`seconds.toFixed(3)`) |

### Array.prototype
| Method | Context |
|---|---|
| `.some()` | Polyfilled if not present — used in validation |
| `.indexOf()` | Polyfilled if not present — weekday/month lookups |
| `.filter()` | Input type checking (`isNumberOrStringArray`) |
| `.push()` | Building result arrays |
| `.sort()` | Sorting month/weekday names by length for regex priority |
| `.slice()` | `[].slice.call(arguments, 0)` for converting arguments to array |
| `.concat()` | `shiftWeekdays()` — rotating weekday arrays |
| `.join()` | Building regex alternation patterns (`pieces.join('|')`) |
| `.length` | Iteration bounds |
| `.map()` | Custom `map()` function (not using Array.prototype.map) |

### Object.prototype
| Method | Context |
|---|---|
| `.toString.call()` | Type detection: `'[object Array]'`, `'[object Object]'`, `'[object Number]'`, `'[object Date]'`, `'[object Function]'` |
| `.hasOwnProperty.call()` | Safe property existence checks throughout |

### Date.prototype
See section 4 below.

### RegExp.prototype
| Method | Context |
|---|---|
| `.test()` | Month/weekday name matching, format validation |
| `.exec()` | ISO 8601 parsing, ASP.NET date parsing, RFC 2822 parsing, duration parsing |
| `.source` | Combining regex patterns (ordinal parse) |
| `.lastIndex` | Reset during format expansion loops |

---

## 3. ES6+ Features Used

Moment.js is almost entirely **ES5**. The only ES6 feature used:

| Feature | Usage | Required? |
|---|---|---|
| `Symbol` | `Symbol.for('nodejs.util.inspect.custom')` — for Node.js REPL display | **No** — guarded by `typeof Symbol !== 'undefined' && Symbol.for != null` |
| `Date.now` | Used with fallback: `Date.now ? Date.now() : +new Date()` | **No** — has ES5 fallback |

### NOT Used (ES6+ features absent)
- No `class` syntax
- No `let`/`const` (all `var`)
- No arrow functions
- No template literals
- No destructuring
- No `for...of`
- No `Promise`
- No `Map`/`Set`/`WeakMap`/`WeakSet`
- No `Proxy`/`Reflect`
- No iterators/generators
- No `Symbol.iterator`
- No `Object.assign`
- No `Object.create`
- No `Array.from`/`Array.of`
- No spread/rest operators
- No `Intl` APIs
- No `globalThis`

---

## 4. Date Methods Used Extensively

### Getters (Local)
| Method | Used In |
|---|---|
| `Date.prototype.getTime()` | Cloning moments, validity check (`!isNaN(m._d.getTime())`) |
| `Date.prototype.getFullYear()` | `get()` function for year |
| `Date.prototype.getMonth()` | `get()` function for month |
| `Date.prototype.getDate()` | `get()` function for day-of-month |
| `Date.prototype.getDay()` | `get()` function for day-of-week |
| `Date.prototype.getHours()` | `get()` function for hours |
| `Date.prototype.getMinutes()` | `get()` function for minutes |
| `Date.prototype.getSeconds()` | `get()` function for seconds |
| `Date.prototype.getMilliseconds()` | `get()` function for milliseconds |
| `Date.prototype.getTimezoneOffset()` | UTC offset calculation |

### Getters (UTC)
| Method | Used In |
|---|---|
| `Date.prototype.getUTCFullYear()` | UTC mode year |
| `Date.prototype.getUTCMonth()` | UTC mode month |
| `Date.prototype.getUTCDate()` | UTC mode date |
| `Date.prototype.getUTCDay()` | UTC mode day-of-week |
| `Date.prototype.getUTCHours()` | UTC mode hours |
| `Date.prototype.getUTCMinutes()` | UTC mode minutes |
| `Date.prototype.getUTCSeconds()` | UTC mode seconds |
| `Date.prototype.getUTCMilliseconds()` | UTC mode milliseconds |

### Setters (Local)
| Method | Used In |
|---|---|
| `Date.prototype.setFullYear()` | `set$1()` with year/month/date |
| `Date.prototype.setMonth()` | `setMonth()` function |
| `Date.prototype.setDate()` | `set$1()` for day |
| `Date.prototype.setHours()` | `set$1()` for hours |
| `Date.prototype.setMinutes()` | `set$1()` for minutes |
| `Date.prototype.setSeconds()` | `set$1()` for seconds |
| `Date.prototype.setMilliseconds()` | `set$1()` for milliseconds |
| `Date.prototype.setTime()` | `startOf()`/`endOf()` and `addSubtract()` for milliseconds |

### Setters (UTC)
| Method | Used In |
|---|---|
| `Date.prototype.setUTCFullYear()` | UTC mode year setting |
| `Date.prototype.setUTCMonth()` | UTC mode month setting |
| `Date.prototype.setUTCDate()` | UTC mode date setting |
| `Date.prototype.setUTCHours()` | UTC mode hours setting |
| `Date.prototype.setUTCMinutes()` | UTC mode minutes setting |
| `Date.prototype.setUTCSeconds()` | UTC mode seconds setting |
| `Date.prototype.setUTCMilliseconds()` | UTC mode milliseconds setting |

### Constructors & Static
| Method | Used In |
|---|---|
| `new Date(year, month, ...)` | `createDate()` — up to 7 arguments |
| `new Date(timestamp)` | Creating from millisecond timestamps |
| `new Date(string)` | Fallback parsing (deprecated path) |
| `new Date(NaN)` | Creating invalid date objects |
| `Date.now()` | Current time (with `+new Date()` fallback) |
| `Date.UTC()` | `createUTCDate()` for UTC date construction |
| `Date.prototype.toISOString()` | Used in `toISOString()` when available (feature-detected) |
| `Date.prototype.valueOf()` | Implicit via `+date` and `date.valueOf()` for timestamp arithmetic |
| `Date.prototype.getTimezoneOffset()` | Timezone offset calculation |

---

## 5. Prototype Modification & defineProperty

| Feature | Used? | Details |
|---|---|---|
| Modifies `Date.prototype` | **No** | Explicitly avoids this — creates wrapper instead |
| Modifies `Array.prototype` | **No** | Polyfills `some`/`indexOf` locally, not on prototype |
| Modifies `Object.prototype` | **No** | |
| `Object.defineProperty` | **No** | Not used anywhere |
| `Object.defineProperties` | **No** | Not used anywhere |
| Modifies `Moment.prototype` | **Yes** | All instance methods assigned directly: `proto.add = add;` etc. |
| Modifies `Duration.prototype` | **Yes** | Same pattern: `proto$2.add = add$1;` etc. |
| Modifies `Locale.prototype` | **Yes** | Same pattern: `proto$1.calendar = calendar;` etc. |

---

## 6. `arguments`, `apply`, `call`, `bind`

| Feature | Usage |
|---|---|
| `arguments` object | Used extensively — `[].slice.call(arguments, 0)` in `min()`/`max()`, deprecation warning formatting, `func.apply(this, arguments)` in format tokens |
| `Function.prototype.apply()` | `hookCallback.apply(null, arguments)` — core `hooks()` function; `func.apply(this, arguments)` in format tokens; `createLocal.apply(null, arguments)` in deprecated min/max |
| `Function.prototype.call()` | `Object.prototype.toString.call()`, `hasOwnProperty.call()`, `fun.call(this, ...)` in `some` polyfill, `output.call(mom, now)` for calendar functions, `computeMonthsParse.call(this)` |
| `Function.prototype.bind()` | **Not used** |

---

## 7. `typeof` and `instanceof`

### `typeof` checks
| Expression | Purpose |
|---|---|
| `typeof exports === 'object'` | UMD module detection (CommonJS) |
| `typeof module !== 'undefined'` | UMD module detection (Node.js) |
| `typeof define === 'function'` | UMD module detection (AMD) |
| `typeof input === 'number'` | Type checking in `isNumber()` |
| `typeof input === 'string'` | Token parsing, unit normalization |
| `typeof input === 'object'` | `stringSet()` object overload |
| `typeof console !== 'undefined'` | Deprecation warning guard |
| `typeof callback === 'string'` | Format token callback type |
| `typeof Function !== 'undefined'` | `isFunction()` check |
| `typeof Symbol !== 'undefined'` | ES6 Symbol feature detection |

### `instanceof` checks
| Expression | Purpose |
|---|---|
| `input instanceof Array` | `isArray()` check |
| `input instanceof Date` | `isDate()` check |
| `input instanceof Function` | `isFunction()` check |
| `input instanceof String` | `isString()` check |
| `obj instanceof Moment` | `isMoment()` check |
| `obj instanceof Duration` | `isDuration()` check |

---

## 8. Try/Catch Error Handling

**Yes** — used in exactly one place:

```js
// In loadLocale() for Node.js locale loading:
try {
    oldLocale = globalLocale._abbr;
    aliasedRequire = require;
    aliasedRequire('./locale/' + name);
    getSetGlobalLocale(oldLocale);
} catch (e) {
    // mark as not found to avoid repeating expensive file require call
    locales[name] = null;
}
```

Also `throw new Error('Unknown unit ' + units)` in the Duration `as()` method.

---

## 9. Getters/Setters

**No ES5 getters/setters** (`get`/`set` property descriptors) are used. All getter/setter behavior is implemented via **overloaded functions** (jQuery-style):

```js
// Call with no args = getter, with args = setter
function makeGetSet(unit, keepTime) {
    return function (value) {
        if (value != null) {
            set$1(this, unit, value);  // setter
            return this;
        } else {
            return get(this, unit);    // getter
        }
    };
}
```

---

## 10. `new Function()` or `eval()`

| Feature | Used? |
|---|---|
| `new Function()` | **No** |
| `eval()` | **No** |
| `setTimeout`/`setInterval` | **No** |

---

## 11. Module System

**UMD (Universal Module Definition)** — supports all three:

```js
;(function (global, factory) {
    typeof exports === 'object' && typeof module !== 'undefined' ? module.exports = factory() :
    typeof define === 'function' && define.amd ? define(factory) :
    global.moment = factory()
}(this, (function () { 'use strict';
    // ... entire library ...
    return hooks;
})));
```

| System | Support |
|---|---|
| CommonJS (`require`/`module.exports`) | Yes |
| AMD (`define`) | Yes |
| Browser global (`window.moment`) | Yes |
| ES modules | Not natively — consumers use bundler interop |

The `require` function is also used internally for dynamic locale loading in Node.js (aliased to avoid bundler issues):
```js
aliasedRequire = require;
aliasedRequire('./locale/' + name);
```

---

## 12. `this` Context Binding Patterns

Moment.js uses `this` extensively:

| Pattern | Usage |
|---|---|
| Constructor `this` | `Moment(config)` — copies config to `this`, creates `this._d` |
| Prototype method `this` | All `proto.*` methods — `this.year()`, `this._d`, `this._isUTC`, etc. |
| `this` in IIFE | UMD wrapper uses `this` as global reference |
| `.call(this, ...)` | `computeMonthsParse.call(this)`, `output.call(mom, now)` for calendar |
| `.apply(this, arguments)` | Format token functions: `func.apply(this, arguments)` |
| Locale methods `this` | `this._calendar[key]`, `this._relativeTime`, `this._months`, etc. |
| Fluent `return this` | All setter/manipulation methods return `this` for chaining |

---

## 13. String Manipulation Methods Used

| Method | Concrete Usage |
|---|---|
| `String.prototype.match()` | Regex matching for all date parsing |
| `String.prototype.replace()` | Format processing, regex escaping, locale name normalization (`'_'` → `'-'`), stripping brackets, ISO duration formatting |
| `String.prototype.split()` | Month/weekday name lists: `'Jan_Feb_...'.split('_')` |
| `String.prototype.toLowerCase()` | Locale key normalization, AM/PM parsing |
| `String.prototype.charAt()` | AM/PM detection |
| `String.prototype.substr()` | Extracting hour/minute/second from composite tokens like `hmm`, `hmmss`, `Hmm`, `Hmmss`; week year token extraction |
| `String.prototype.slice()` | Removing brackets from format tokens, `shiftWeekdays()` |
| `String.prototype.length` | Format token processing, input validation |
| `String.prototype.indexOf()` | (implicit via regex operations) |
| `String.prototype.toFixed()` | ISO duration seconds: `seconds.toFixed(3).replace(/\.?0+$/, '')` |
| String concatenation (`+`) | Extensive — building format output, regex patterns, error messages |
| `'' + value` / `input + ''` | String coercion |

---

## 14. `toISOString`, `toJSON`, `toString`, `valueOf` on Date

| Method | Usage |
|---|---|
| `Date.prototype.toISOString()` | **Feature-detected**: `isFunction(Date.prototype.toISOString)` — used in `moment.toISOString()` for performance (~50x faster than manual formatting) |
| `Date.prototype.toJSON()` | **Not directly used** — moment defines its own `toJSON()` that calls `this.toISOString()` |
| `Date.prototype.toString()` | **Not directly used** — moment defines its own `toString()` that formats via `moment.format()` |
| `Date.prototype.valueOf()` | **Implicitly used** — via `+date` arithmetic and `this._d.valueOf()` in moment's `valueOf()` method; also `mom._d.valueOf()` in `addSubtract()` and `startOf()`/`endOf()` |
| `Date.prototype.getTime()` | Used in `Moment` constructor for cloning: `config._d.getTime()` |

Moment defines its own versions of these on `Moment.prototype`:
- `proto.toJSON = toJSON` → returns `this.toISOString()` or `null`
- `proto.toString = toString` → returns English-formatted string
- `proto.valueOf = valueOf` → returns timestamp adjusted for offset

---

## 15. `Intl` APIs

**Not used.** Moment.js does not use any `Intl` APIs. All internationalization is handled through:
- Locale definition objects with hardcoded month/weekday name strings
- Custom regex-based parsing for locale-specific formats
- Manual AM/PM and ordinal formatting

The documentation mentions `Intl` only in the context of recommending alternatives to moment.js.

---

## Summary: Minimum JavaScript Engine Requirements

Moment.js requires essentially an **ES5-compliant** JavaScript engine with:

### Required Core
- `Date` (full API — constructor, getters, setters, UTC variants)
- `RegExp` (constructor, `exec`, `test`, `source`, `lastIndex`)
- `Math` (`abs`, `floor`, `ceil`, `round`, `pow`, `min`, `max`)
- `Object.prototype.toString`, `Object.prototype.hasOwnProperty`
- `parseInt`, `parseFloat`, `isFinite`, `isNaN`
- `typeof`, `instanceof`
- `Array` (basic: `push`, `sort`, `join`, `concat`, `slice`, `length`)
- `String` (basic: `match`, `replace`, `split`, `toLowerCase`, `charAt`, `substr`, `slice`, `length`)
- `arguments` object
- `Function.prototype.apply`, `Function.prototype.call`
- `new Date(y,m,d,h,m,s,ms)` — 7-argument constructor
- `Date.UTC()`

### Optional (with fallbacks)
- `Object.keys` → fallback `for...in` loop
- `Object.getOwnPropertyNames` → fallback `for...in` loop
- `Array.prototype.some` → inline polyfill
- `Array.prototype.indexOf` → inline polyfill
- `Array.prototype.filter` → only used in input validation
- `Date.now()` → fallback `+new Date()`
- `Date.prototype.toISOString()` → fallback manual formatting
- `Symbol.for` → guarded, only for Node.js inspect display
- `console.warn` → guarded, only for deprecation warnings
- `require` → only in Node.js for dynamic locale loading

### Not Required
- No ES6+ syntax features
- No `Intl`
- No `Proxy`/`Reflect`
- No `Promise`
- No `Map`/`Set`/`WeakMap`/`WeakSet`
- No `Object.defineProperty`/`Object.defineProperties`
- No getters/setters (property descriptors)
- No `eval`/`new Function()`
- No `bind()`
- No DOM APIs
