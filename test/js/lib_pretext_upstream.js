// bun:test shim v3 - synchronous
var __test_beforeAll_fns = [];
var __test_beforeEach_fns = [];
var __test_describes = [];

function __test_beforeAll(fn) { __test_beforeAll_fns.push(fn); }
function __test_beforeEach(fn) { __test_beforeEach_fns.push(fn); }

function __test_describe(name, fn) {
  var desc = { name: name, tests: [] };
  var prev_desc = __test_current_describe;
  __test_current_describe = desc;
  fn();
  __test_current_describe = prev_desc;
  __test_describes.push(desc);
}
var __test_current_describe = null;

function __test_test(name, fn) {
  if (__test_current_describe) {
    __test_current_describe.tests.push({ name: name, fn: fn });
  }
}

function __test_deep_equal(a, b) {
  if (a === b) return true;
  if (a === null || b === null) return false;
  if (a === undefined || b === undefined) return false;
  if (typeof a !== typeof b) return false;
  if (typeof a === "number") {
    if (a !== a && b !== b) return true;
    return false;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (!__test_deep_equal(a[i], b[i])) return false;
    }
    return true;
  }
  if (typeof a === "object") {
    var ka = Object.keys(a), kb = Object.keys(b);
    if (ka.length !== kb.length) return false;
    for (var j = 0; j < ka.length; j++) {
      if (!__test_deep_equal(a[ka[j]], b[ka[j]])) return false;
    }
    return true;
  }
  return false;
}

function __test_expect(actual) {
  var negate = false;
  var api = {
    toBe: function(expected) {
      var pass = negate ? actual !== expected : actual === expected;
      if (!pass) throw new Error("Expected " + JSON.stringify(actual) + (negate ? " not " : " ") + "toBe " + JSON.stringify(expected));
    },
    toEqual: function(expected) {
      var pass = negate ? !__test_deep_equal(actual, expected) : __test_deep_equal(actual, expected);
      if (!pass) throw new Error("Expected " + JSON.stringify(actual) + (negate ? " not " : " ") + "toEqual " + JSON.stringify(expected));
    },
    toBeNull: function() {
      var pass = negate ? actual !== null : actual === null;
      if (!pass) throw new Error("Expected " + JSON.stringify(actual) + (negate ? " not " : " ") + "toBeNull");
    },
    toHaveLength: function(expected) {
      var len = actual && actual.length !== undefined ? actual.length : -1;
      var pass = negate ? len !== expected : len === expected;
      if (!pass) throw new Error("Expected length " + len + (negate ? " not " : " ") + "toBe " + expected);
    },
    toBeGreaterThan: function(expected) {
      var pass = negate ? !(actual > expected) : actual > expected;
      if (!pass) throw new Error("Expected " + actual + (negate ? " not " : " ") + "> " + expected);
    },
    toBeGreaterThanOrEqual: function(expected) {
      var pass = negate ? !(actual >= expected) : actual >= expected;
      if (!pass) throw new Error("Expected " + actual + (negate ? " not " : " ") + ">= " + expected);
    },
    toBeCloseTo: function(expected, digits) {
      if (digits === undefined) digits = 2;
      var diff = Math.abs(actual - expected);
      var epsilon = Math.pow(10, -digits) / 2;
      var pass = negate ? diff >= epsilon : diff < epsilon;
      if (!pass) throw new Error("Expected " + actual + (negate ? " not " : " ") + "toBeCloseTo " + expected);
    }
  };
  Object.defineProperty(api, "not", {
    get: function() { negate = !negate; return api; }
  });
  return api;
}

var require = function(name) {
  if (name === "bun:test") {
    return {
      describe: __test_describe,
      test: __test_test,
      expect: __test_expect,
      beforeAll: __test_beforeAll,
      beforeEach: __test_beforeEach
    };
  }
  throw new Error("Cannot require: " + name);
};

// Intl.Segmenter mock
if (typeof Intl === "undefined") globalThis.Intl = {};
Intl.Segmenter = function(locale, opts) {
  this.locale = locale;
  this.granularity = opts ? opts.granularity : "grapheme";
};
Intl.Segmenter.prototype.segment = function(str) {
  var segs = [];
  if (this.granularity === "word") {
    var re = /\w+|\s+|[^\w\s]+/g;
    var m;
    while ((m = re.exec(str)) !== null) {
      segs.push({ segment: m[0], index: m.index, isWordLike: /\w/.test(m[0]) });
    }
  } else {
    for (var i = 0; i < str.length; i++) {
      var code = str.charCodeAt(i);
      if (code >= 0xD800 && code <= 0xDBFF && i + 1 < str.length) {
        var next = str.charCodeAt(i + 1);
        if (next >= 0xDC00 && next <= 0xDFFF) {
          segs.push({ segment: str[i] + str[i + 1], index: i });
          i++;
          continue;
        }
      }
      segs.push({ segment: str[i], index: i });
    }
  }
  var result = { _segs: segs };
  result[Symbol.iterator] = function() {
    var idx = 0;
    return {
      next: function() {
        if (idx < segs.length) return { value: segs[idx++], done: false };
        return { done: true };
      }
    };
  };
  return result;
};
(() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __defNormalProp = (obj, key, value) => key in obj ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value }) : obj[key] = value;
  var __require = /* @__PURE__ */ ((x) => typeof require !== "undefined" ? require : typeof Proxy !== "undefined" ? new Proxy(x, {
    get: (a, b) => (typeof require !== "undefined" ? require : a)[b]
  }) : x)(function(x) {
    if (typeof require !== "undefined") return require.apply(this, arguments);
    throw Error('Dynamic require of "' + x + '" is not supported');
  });
  var __esm = (fn, res) => function __init() {
    return fn && (res = (0, fn[__getOwnPropNames(fn)[0]])(fn = 0)), res;
  };
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __publicField = (obj, key, value) => __defNormalProp(obj, typeof key !== "symbol" ? key + "" : key, value);

  // src/analysis.ts
  var analysis_exports = {};
  __export(analysis_exports, {
    analyzeText: () => analyzeText,
    canContinueKeepAllTextRun: () => canContinueKeepAllTextRun,
    clearAnalysisCaches: () => clearAnalysisCaches,
    endsWithClosingQuote: () => endsWithClosingQuote,
    isCJK: () => isCJK,
    isNumericRunSegment: () => isNumericRunSegment,
    kinsokuEnd: () => kinsokuEnd,
    kinsokuStart: () => kinsokuStart,
    leftStickyPunctuation: () => leftStickyPunctuation,
    normalizeWhitespaceNormal: () => normalizeWhitespaceNormal,
    setAnalysisLocale: () => setAnalysisLocale
  });
  function getWhiteSpaceProfile(whiteSpace) {
    const mode = whiteSpace ?? "normal";
    return mode === "pre-wrap" ? { mode, preserveOrdinarySpaces: true, preserveHardBreaks: true } : { mode, preserveOrdinarySpaces: false, preserveHardBreaks: false };
  }
  function normalizeWhitespaceNormal(text) {
    if (!needsWhitespaceNormalizationRe.test(text)) return text;
    let normalized = text.replace(collapsibleWhitespaceRunRe, " ");
    if (normalized.charCodeAt(0) === 32) {
      normalized = normalized.slice(1);
    }
    if (normalized.length > 0 && normalized.charCodeAt(normalized.length - 1) === 32) {
      normalized = normalized.slice(0, -1);
    }
    return normalized;
  }
  function normalizeWhitespacePreWrap(text) {
    if (!/[\r\f]/.test(text)) return text.replace(/\r\n/g, "\n");
    return text.replace(/\r\n/g, "\n").replace(/[\r\f]/g, "\n");
  }
  function getSharedWordSegmenter() {
    if (sharedWordSegmenter === null) {
      sharedWordSegmenter = new Intl.Segmenter(segmenterLocale, { granularity: "word" });
    }
    return sharedWordSegmenter;
  }
  function clearAnalysisCaches() {
    sharedWordSegmenter = null;
  }
  function setAnalysisLocale(locale) {
    const nextLocale = locale && locale.length > 0 ? locale : void 0;
    if (segmenterLocale === nextLocale) return;
    segmenterLocale = nextLocale;
    sharedWordSegmenter = null;
  }
  function containsArabicScript(text) {
    return arabicScriptRe.test(text);
  }
  function isCJKCodePoint(codePoint) {
    return codePoint >= 19968 && codePoint <= 40959 || codePoint >= 13312 && codePoint <= 19903 || codePoint >= 131072 && codePoint <= 173791 || codePoint >= 173824 && codePoint <= 177983 || codePoint >= 177984 && codePoint <= 178207 || codePoint >= 178208 && codePoint <= 183983 || codePoint >= 183984 && codePoint <= 191471 || codePoint >= 191472 && codePoint <= 192093 || codePoint >= 194560 && codePoint <= 195103 || codePoint >= 196608 && codePoint <= 201551 || codePoint >= 201552 && codePoint <= 205743 || codePoint >= 205744 && codePoint <= 210041 || codePoint >= 63744 && codePoint <= 64255 || codePoint >= 12288 && codePoint <= 12351 || codePoint >= 12352 && codePoint <= 12447 || codePoint >= 12448 && codePoint <= 12543 || codePoint >= 44032 && codePoint <= 55215 || codePoint >= 65280 && codePoint <= 65519;
  }
  function isCJK(s) {
    for (let i = 0; i < s.length; i++) {
      const first = s.charCodeAt(i);
      if (first < 12288) continue;
      if (first >= 55296 && first <= 56319 && i + 1 < s.length) {
        const second = s.charCodeAt(i + 1);
        if (second >= 56320 && second <= 57343) {
          const codePoint = (first - 55296 << 10) + (second - 56320) + 65536;
          if (isCJKCodePoint(codePoint)) return true;
          i++;
          continue;
        }
      }
      if (isCJKCodePoint(first)) return true;
    }
    return false;
  }
  function endsWithLineStartProhibitedText(text) {
    const last = getLastCodePoint(text);
    return last !== null && (kinsokuStart.has(last) || leftStickyPunctuation.has(last));
  }
  function containsCJKText(text) {
    return isCJK(text);
  }
  function endsWithKeepAllGlueText(text) {
    const last = getLastCodePoint(text);
    return last !== null && keepAllGlueChars.has(last);
  }
  function canContinueKeepAllTextRun(previousText) {
    return !endsWithLineStartProhibitedText(previousText) && !endsWithKeepAllGlueText(previousText);
  }
  function isLeftStickyPunctuationSegment(segment) {
    if (isEscapedQuoteClusterSegment(segment)) return true;
    let sawPunctuation = false;
    for (const ch of segment) {
      if (leftStickyPunctuation.has(ch)) {
        sawPunctuation = true;
        continue;
      }
      if (sawPunctuation && combiningMarkRe.test(ch)) continue;
      return false;
    }
    return sawPunctuation;
  }
  function isCJKLineStartProhibitedSegment(segment) {
    for (const ch of segment) {
      if (!kinsokuStart.has(ch) && !leftStickyPunctuation.has(ch)) return false;
    }
    return segment.length > 0;
  }
  function isForwardStickyClusterSegment(segment) {
    if (isEscapedQuoteClusterSegment(segment)) return true;
    for (const ch of segment) {
      if (!kinsokuEnd.has(ch) && !forwardStickyGlue.has(ch) && !combiningMarkRe.test(ch)) return false;
    }
    return segment.length > 0;
  }
  function isEscapedQuoteClusterSegment(segment) {
    let sawQuote = false;
    for (const ch of segment) {
      if (ch === "\\" || combiningMarkRe.test(ch)) continue;
      if (kinsokuEnd.has(ch) || leftStickyPunctuation.has(ch) || forwardStickyGlue.has(ch)) {
        sawQuote = true;
        continue;
      }
      return false;
    }
    return sawQuote;
  }
  function previousCodePointStart(text, end) {
    const last = end - 1;
    if (last <= 0) return Math.max(last, 0);
    const lastCodeUnit = text.charCodeAt(last);
    if (lastCodeUnit < 56320 || lastCodeUnit > 57343) return last;
    const maybeHigh = last - 1;
    if (maybeHigh < 0) return last;
    const highCodeUnit = text.charCodeAt(maybeHigh);
    return highCodeUnit >= 55296 && highCodeUnit <= 56319 ? maybeHigh : last;
  }
  function getLastCodePoint(text) {
    if (text.length === 0) return null;
    const start = previousCodePointStart(text, text.length);
    return text.slice(start);
  }
  function splitTrailingForwardStickyCluster(text) {
    const chars = Array.from(text);
    let splitIndex = chars.length;
    while (splitIndex > 0) {
      const ch = chars[splitIndex - 1];
      if (combiningMarkRe.test(ch)) {
        splitIndex--;
        continue;
      }
      if (kinsokuEnd.has(ch) || forwardStickyGlue.has(ch)) {
        splitIndex--;
        continue;
      }
      break;
    }
    if (splitIndex <= 0 || splitIndex === chars.length) return null;
    return {
      head: chars.slice(0, splitIndex).join(""),
      tail: chars.slice(splitIndex).join("")
    };
  }
  function getRepeatableSingleCharRunChar(text, isWordLike, kind) {
    return kind === "text" && !isWordLike && text.length === 1 && text !== "-" && text !== "\u2014" ? text : null;
  }
  function materializeDeferredSingleCharRun(texts, chars, lengths, index) {
    const ch = chars[index];
    const text = texts[index];
    if (ch == null) return text;
    const length = lengths[index];
    if (text.length === length) return text;
    const materialized = ch.repeat(length);
    texts[index] = materialized;
    return materialized;
  }
  function hasArabicNoSpacePunctuation(containsArabic, lastCodePoint) {
    return containsArabic && lastCodePoint !== null && arabicNoSpaceTrailingPunctuation.has(lastCodePoint);
  }
  function endsWithMyanmarMedialGlue(segment) {
    const lastCodePoint = getLastCodePoint(segment);
    return lastCodePoint !== null && myanmarMedialGlue.has(lastCodePoint);
  }
  function splitLeadingSpaceAndMarks(segment) {
    if (segment.length < 2 || segment[0] !== " ") return null;
    const marks = segment.slice(1);
    if (/^\p{M}+$/u.test(marks)) {
      return { space: " ", marks };
    }
    return null;
  }
  function endsWithClosingQuote(text) {
    let end = text.length;
    while (end > 0) {
      const start = previousCodePointStart(text, end);
      const ch = text.slice(start, end);
      if (closingQuoteChars.has(ch)) return true;
      if (!leftStickyPunctuation.has(ch)) return false;
      end = start;
    }
    return false;
  }
  function classifySegmentBreakChar(ch, whiteSpaceProfile) {
    if (whiteSpaceProfile.preserveOrdinarySpaces || whiteSpaceProfile.preserveHardBreaks) {
      if (ch === " ") return "preserved-space";
      if (ch === "	") return "tab";
      if (whiteSpaceProfile.preserveHardBreaks && ch === "\n") return "hard-break";
    }
    if (ch === " ") return "space";
    if (ch === "\xA0" || ch === "\u202F" || ch === "\u2060" || ch === "\uFEFF") {
      return "glue";
    }
    if (ch === "\u200B") return "zero-width-break";
    if (ch === "\xAD") return "soft-hyphen";
    return "text";
  }
  function joinTextParts(parts) {
    return parts.length === 1 ? parts[0] : parts.join("");
  }
  function joinReversedPrefixParts(prefixParts, tail) {
    const parts = [];
    for (let i = prefixParts.length - 1; i >= 0; i--) {
      parts.push(prefixParts[i]);
    }
    parts.push(tail);
    return joinTextParts(parts);
  }
  function splitSegmentByBreakKind(segment, isWordLike, start, whiteSpaceProfile) {
    if (!breakCharRe.test(segment)) {
      return [{ text: segment, isWordLike, kind: "text", start }];
    }
    const pieces = [];
    let currentKind = null;
    let currentTextParts = [];
    let currentStart = start;
    let currentWordLike = false;
    let offset = 0;
    for (const ch of segment) {
      const kind = classifySegmentBreakChar(ch, whiteSpaceProfile);
      const wordLike = kind === "text" && isWordLike;
      if (currentKind !== null && kind === currentKind && wordLike === currentWordLike) {
        currentTextParts.push(ch);
        offset += ch.length;
        continue;
      }
      if (currentKind !== null) {
        pieces.push({
          text: joinTextParts(currentTextParts),
          isWordLike: currentWordLike,
          kind: currentKind,
          start: currentStart
        });
      }
      currentKind = kind;
      currentTextParts = [ch];
      currentStart = start + offset;
      currentWordLike = wordLike;
      offset += ch.length;
    }
    if (currentKind !== null) {
      pieces.push({
        text: joinTextParts(currentTextParts),
        isWordLike: currentWordLike,
        kind: currentKind,
        start: currentStart
      });
    }
    return pieces;
  }
  function isTextRunBoundary(kind) {
    return kind === "space" || kind === "preserved-space" || kind === "zero-width-break" || kind === "hard-break";
  }
  function isUrlLikeRunStart(segmentation, index) {
    const text = segmentation.texts[index];
    if (text.startsWith("www.")) return true;
    return urlSchemeSegmentRe.test(text) && index + 1 < segmentation.len && segmentation.kinds[index + 1] === "text" && segmentation.texts[index + 1] === "//";
  }
  function isUrlQueryBoundarySegment(text) {
    return text.includes("?") && (text.includes("://") || text.startsWith("www."));
  }
  function mergeUrlLikeRuns(segmentation) {
    const texts = segmentation.texts.slice();
    const isWordLike = segmentation.isWordLike.slice();
    const kinds = segmentation.kinds.slice();
    const starts = segmentation.starts.slice();
    for (let i = 0; i < segmentation.len; i++) {
      if (kinds[i] !== "text" || !isUrlLikeRunStart(segmentation, i)) continue;
      const mergedParts = [texts[i]];
      let j = i + 1;
      while (j < segmentation.len && !isTextRunBoundary(kinds[j])) {
        mergedParts.push(texts[j]);
        isWordLike[i] = true;
        const endsQueryPrefix = texts[j].includes("?");
        kinds[j] = "text";
        texts[j] = "";
        j++;
        if (endsQueryPrefix) break;
      }
      texts[i] = joinTextParts(mergedParts);
    }
    let compactLen = 0;
    for (let read = 0; read < texts.length; read++) {
      const text = texts[read];
      if (text.length === 0) continue;
      if (compactLen !== read) {
        texts[compactLen] = text;
        isWordLike[compactLen] = isWordLike[read];
        kinds[compactLen] = kinds[read];
        starts[compactLen] = starts[read];
      }
      compactLen++;
    }
    texts.length = compactLen;
    isWordLike.length = compactLen;
    kinds.length = compactLen;
    starts.length = compactLen;
    return {
      len: compactLen,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function mergeUrlQueryRuns(segmentation) {
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    for (let i = 0; i < segmentation.len; i++) {
      const text = segmentation.texts[i];
      texts.push(text);
      isWordLike.push(segmentation.isWordLike[i]);
      kinds.push(segmentation.kinds[i]);
      starts.push(segmentation.starts[i]);
      if (!isUrlQueryBoundarySegment(text)) continue;
      const nextIndex = i + 1;
      if (nextIndex >= segmentation.len || isTextRunBoundary(segmentation.kinds[nextIndex])) {
        continue;
      }
      const queryParts = [];
      const queryStart = segmentation.starts[nextIndex];
      let j = nextIndex;
      while (j < segmentation.len && !isTextRunBoundary(segmentation.kinds[j])) {
        queryParts.push(segmentation.texts[j]);
        j++;
      }
      if (queryParts.length > 0) {
        texts.push(joinTextParts(queryParts));
        isWordLike.push(true);
        kinds.push("text");
        starts.push(queryStart);
        i = j - 1;
      }
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function segmentContainsDecimalDigit(text) {
    for (const ch of text) {
      if (decimalDigitRe.test(ch)) return true;
    }
    return false;
  }
  function isNumericRunSegment(text) {
    if (text.length === 0) return false;
    for (const ch of text) {
      if (decimalDigitRe.test(ch) || numericJoinerChars.has(ch)) continue;
      return false;
    }
    return true;
  }
  function mergeNumericRuns(segmentation) {
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    for (let i = 0; i < segmentation.len; i++) {
      const text = segmentation.texts[i];
      const kind = segmentation.kinds[i];
      if (kind === "text" && isNumericRunSegment(text) && segmentContainsDecimalDigit(text)) {
        const mergedParts = [text];
        let j = i + 1;
        while (j < segmentation.len && segmentation.kinds[j] === "text" && isNumericRunSegment(segmentation.texts[j])) {
          mergedParts.push(segmentation.texts[j]);
          j++;
        }
        texts.push(joinTextParts(mergedParts));
        isWordLike.push(true);
        kinds.push("text");
        starts.push(segmentation.starts[i]);
        i = j - 1;
        continue;
      }
      texts.push(text);
      isWordLike.push(segmentation.isWordLike[i]);
      kinds.push(kind);
      starts.push(segmentation.starts[i]);
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function mergeAsciiPunctuationChains(segmentation) {
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    for (let i = 0; i < segmentation.len; i++) {
      const text = segmentation.texts[i];
      const kind = segmentation.kinds[i];
      const wordLike = segmentation.isWordLike[i];
      if (kind === "text" && wordLike && asciiPunctuationChainSegmentRe.test(text)) {
        const mergedParts = [text];
        let endsWithJoiners = asciiPunctuationChainTrailingJoinersRe.test(text);
        let j = i + 1;
        while (endsWithJoiners && j < segmentation.len && segmentation.kinds[j] === "text" && segmentation.isWordLike[j] && asciiPunctuationChainSegmentRe.test(segmentation.texts[j])) {
          const nextText = segmentation.texts[j];
          mergedParts.push(nextText);
          endsWithJoiners = asciiPunctuationChainTrailingJoinersRe.test(nextText);
          j++;
        }
        texts.push(joinTextParts(mergedParts));
        isWordLike.push(true);
        kinds.push("text");
        starts.push(segmentation.starts[i]);
        i = j - 1;
        continue;
      }
      texts.push(text);
      isWordLike.push(wordLike);
      kinds.push(kind);
      starts.push(segmentation.starts[i]);
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function splitHyphenatedNumericRuns(segmentation) {
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    for (let i = 0; i < segmentation.len; i++) {
      const text = segmentation.texts[i];
      if (segmentation.kinds[i] === "text" && text.includes("-")) {
        const parts = text.split("-");
        let shouldSplit = parts.length > 1;
        for (let j = 0; j < parts.length; j++) {
          const part = parts[j];
          if (!shouldSplit) break;
          if (part.length === 0 || !segmentContainsDecimalDigit(part) || !isNumericRunSegment(part)) {
            shouldSplit = false;
          }
        }
        if (shouldSplit) {
          let offset = 0;
          for (let j = 0; j < parts.length; j++) {
            const part = parts[j];
            const splitText = j < parts.length - 1 ? `${part}-` : part;
            texts.push(splitText);
            isWordLike.push(true);
            kinds.push("text");
            starts.push(segmentation.starts[i] + offset);
            offset += splitText.length;
          }
          continue;
        }
      }
      texts.push(text);
      isWordLike.push(segmentation.isWordLike[i]);
      kinds.push(segmentation.kinds[i]);
      starts.push(segmentation.starts[i]);
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function mergeGlueConnectedTextRuns(segmentation) {
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    let read = 0;
    while (read < segmentation.len) {
      const textParts = [segmentation.texts[read]];
      let wordLike = segmentation.isWordLike[read];
      let kind = segmentation.kinds[read];
      let start = segmentation.starts[read];
      if (kind === "glue") {
        const glueParts = [textParts[0]];
        const glueStart = start;
        read++;
        while (read < segmentation.len && segmentation.kinds[read] === "glue") {
          glueParts.push(segmentation.texts[read]);
          read++;
        }
        const glueText = joinTextParts(glueParts);
        if (read < segmentation.len && segmentation.kinds[read] === "text") {
          textParts[0] = glueText;
          textParts.push(segmentation.texts[read]);
          wordLike = segmentation.isWordLike[read];
          kind = "text";
          start = glueStart;
          read++;
        } else {
          texts.push(glueText);
          isWordLike.push(false);
          kinds.push("glue");
          starts.push(glueStart);
          continue;
        }
      } else {
        read++;
      }
      if (kind === "text") {
        while (read < segmentation.len && segmentation.kinds[read] === "glue") {
          const glueParts = [];
          while (read < segmentation.len && segmentation.kinds[read] === "glue") {
            glueParts.push(segmentation.texts[read]);
            read++;
          }
          const glueText = joinTextParts(glueParts);
          if (read < segmentation.len && segmentation.kinds[read] === "text") {
            textParts.push(glueText, segmentation.texts[read]);
            wordLike = wordLike || segmentation.isWordLike[read];
            read++;
            continue;
          }
          textParts.push(glueText);
        }
      }
      texts.push(joinTextParts(textParts));
      isWordLike.push(wordLike);
      kinds.push(kind);
      starts.push(start);
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function carryTrailingForwardStickyAcrossCJKBoundary(segmentation) {
    const texts = segmentation.texts.slice();
    const isWordLike = segmentation.isWordLike.slice();
    const kinds = segmentation.kinds.slice();
    const starts = segmentation.starts.slice();
    for (let i = 0; i < texts.length - 1; i++) {
      if (kinds[i] !== "text" || kinds[i + 1] !== "text") continue;
      if (!isCJK(texts[i]) || !isCJK(texts[i + 1])) continue;
      const split = splitTrailingForwardStickyCluster(texts[i]);
      if (split === null) continue;
      texts[i] = split.head;
      texts[i + 1] = split.tail + texts[i + 1];
      starts[i + 1] = starts[i] + split.head.length;
    }
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function buildMergedSegmentation(normalized, profile, whiteSpaceProfile) {
    const wordSegmenter = getSharedWordSegmenter();
    let mergedLen = 0;
    const mergedTexts = [];
    const mergedTextParts = [];
    const mergedWordLike = [];
    const mergedKinds = [];
    const mergedStarts = [];
    const mergedSingleCharRunChars = [];
    const mergedSingleCharRunLengths = [];
    const mergedContainsCJK = [];
    const mergedContainsArabicScript = [];
    const mergedEndsWithClosingQuote = [];
    const mergedEndsWithMyanmarMedialGlue = [];
    const mergedHasArabicNoSpacePunctuation = [];
    for (const s of wordSegmenter.segment(normalized)) {
      for (const piece of splitSegmentByBreakKind(s.segment, s.isWordLike ?? false, s.index, whiteSpaceProfile)) {
        let appendPieceToPrevious = function() {
          if (mergedSingleCharRunChars[prevIndex] !== null) {
            mergedTextParts[prevIndex] = [
              materializeDeferredSingleCharRun(
                mergedTexts,
                mergedSingleCharRunChars,
                mergedSingleCharRunLengths,
                prevIndex
              )
            ];
            mergedSingleCharRunChars[prevIndex] = null;
          }
          mergedTextParts[prevIndex].push(piece.text);
          mergedWordLike[prevIndex] = mergedWordLike[prevIndex] || piece.isWordLike;
          mergedContainsCJK[prevIndex] = mergedContainsCJK[prevIndex] || pieceContainsCJK;
          mergedContainsArabicScript[prevIndex] = mergedContainsArabicScript[prevIndex] || pieceContainsArabicScript;
          mergedEndsWithClosingQuote[prevIndex] = pieceEndsWithClosingQuote;
          mergedEndsWithMyanmarMedialGlue[prevIndex] = pieceEndsWithMyanmarMedialGlue;
          mergedHasArabicNoSpacePunctuation[prevIndex] = hasArabicNoSpacePunctuation(
            mergedContainsArabicScript[prevIndex],
            pieceLastCodePoint
          );
        };
        const isText = piece.kind === "text";
        const repeatableSingleCharRunChar = getRepeatableSingleCharRunChar(piece.text, piece.isWordLike, piece.kind);
        const pieceContainsCJK = isCJK(piece.text);
        const pieceContainsArabicScript = containsArabicScript(piece.text);
        const pieceLastCodePoint = getLastCodePoint(piece.text);
        const pieceEndsWithClosingQuote = endsWithClosingQuote(piece.text);
        const pieceEndsWithMyanmarMedialGlue = endsWithMyanmarMedialGlue(piece.text);
        const prevIndex = mergedLen - 1;
        if (profile.carryCJKAfterClosingQuote && isText && mergedLen > 0 && mergedKinds[prevIndex] === "text" && pieceContainsCJK && mergedContainsCJK[prevIndex] && mergedEndsWithClosingQuote[prevIndex]) {
          appendPieceToPrevious();
        } else if (isText && mergedLen > 0 && mergedKinds[prevIndex] === "text" && isCJKLineStartProhibitedSegment(piece.text) && mergedContainsCJK[prevIndex]) {
          appendPieceToPrevious();
        } else if (isText && mergedLen > 0 && mergedKinds[prevIndex] === "text" && mergedEndsWithMyanmarMedialGlue[prevIndex]) {
          appendPieceToPrevious();
        } else if (isText && mergedLen > 0 && mergedKinds[prevIndex] === "text" && piece.isWordLike && pieceContainsArabicScript && mergedHasArabicNoSpacePunctuation[prevIndex]) {
          appendPieceToPrevious();
          mergedWordLike[prevIndex] = true;
        } else if (repeatableSingleCharRunChar !== null && mergedLen > 0 && mergedKinds[prevIndex] === "text" && mergedSingleCharRunChars[prevIndex] === repeatableSingleCharRunChar) {
          mergedSingleCharRunLengths[prevIndex] = (mergedSingleCharRunLengths[prevIndex] ?? 1) + 1;
        } else if (isText && !piece.isWordLike && mergedLen > 0 && mergedKinds[prevIndex] === "text" && (isLeftStickyPunctuationSegment(piece.text) || piece.text === "-" && mergedWordLike[prevIndex])) {
          appendPieceToPrevious();
        } else {
          mergedTexts[mergedLen] = piece.text;
          mergedTextParts[mergedLen] = [piece.text];
          mergedWordLike[mergedLen] = piece.isWordLike;
          mergedKinds[mergedLen] = piece.kind;
          mergedStarts[mergedLen] = piece.start;
          mergedSingleCharRunChars[mergedLen] = repeatableSingleCharRunChar;
          mergedSingleCharRunLengths[mergedLen] = repeatableSingleCharRunChar === null ? 0 : 1;
          mergedContainsCJK[mergedLen] = pieceContainsCJK;
          mergedContainsArabicScript[mergedLen] = pieceContainsArabicScript;
          mergedEndsWithClosingQuote[mergedLen] = pieceEndsWithClosingQuote;
          mergedEndsWithMyanmarMedialGlue[mergedLen] = pieceEndsWithMyanmarMedialGlue;
          mergedHasArabicNoSpacePunctuation[mergedLen] = hasArabicNoSpacePunctuation(
            pieceContainsArabicScript,
            pieceLastCodePoint
          );
          mergedLen++;
        }
      }
    }
    for (let i = 0; i < mergedLen; i++) {
      if (mergedSingleCharRunChars[i] !== null) {
        mergedTexts[i] = materializeDeferredSingleCharRun(
          mergedTexts,
          mergedSingleCharRunChars,
          mergedSingleCharRunLengths,
          i
        );
        continue;
      }
      mergedTexts[i] = joinTextParts(mergedTextParts[i]);
    }
    for (let i = 1; i < mergedLen; i++) {
      if (mergedKinds[i] === "text" && !mergedWordLike[i] && isEscapedQuoteClusterSegment(mergedTexts[i]) && mergedKinds[i - 1] === "text") {
        mergedTexts[i - 1] += mergedTexts[i];
        mergedWordLike[i - 1] = mergedWordLike[i - 1] || mergedWordLike[i];
        mergedTexts[i] = "";
      }
    }
    const forwardStickyPrefixParts = Array.from({ length: mergedLen }, () => null);
    let nextLiveIndex = -1;
    for (let i = mergedLen - 1; i >= 0; i--) {
      const text = mergedTexts[i];
      if (text.length === 0) continue;
      if (mergedKinds[i] === "text" && !mergedWordLike[i] && isForwardStickyClusterSegment(text) && nextLiveIndex >= 0 && mergedKinds[nextLiveIndex] === "text") {
        const prefixParts = forwardStickyPrefixParts[nextLiveIndex] ?? [];
        prefixParts.push(text);
        forwardStickyPrefixParts[nextLiveIndex] = prefixParts;
        mergedStarts[nextLiveIndex] = mergedStarts[i];
        mergedTexts[i] = "";
        continue;
      }
      nextLiveIndex = i;
    }
    for (let i = 0; i < mergedLen; i++) {
      const prefixParts = forwardStickyPrefixParts[i];
      if (prefixParts == null) continue;
      mergedTexts[i] = joinReversedPrefixParts(prefixParts, mergedTexts[i]);
    }
    let compactLen = 0;
    for (let read = 0; read < mergedLen; read++) {
      const text = mergedTexts[read];
      if (text.length === 0) continue;
      if (compactLen !== read) {
        mergedTexts[compactLen] = text;
        mergedWordLike[compactLen] = mergedWordLike[read];
        mergedKinds[compactLen] = mergedKinds[read];
        mergedStarts[compactLen] = mergedStarts[read];
      }
      compactLen++;
    }
    mergedTexts.length = compactLen;
    mergedWordLike.length = compactLen;
    mergedKinds.length = compactLen;
    mergedStarts.length = compactLen;
    const compacted = mergeGlueConnectedTextRuns({
      len: compactLen,
      texts: mergedTexts,
      isWordLike: mergedWordLike,
      kinds: mergedKinds,
      starts: mergedStarts
    });
    const withMergedUrls = carryTrailingForwardStickyAcrossCJKBoundary(
      mergeAsciiPunctuationChains(
        splitHyphenatedNumericRuns(mergeNumericRuns(mergeUrlQueryRuns(mergeUrlLikeRuns(compacted))))
      )
    );
    for (let i = 0; i < withMergedUrls.len - 1; i++) {
      const split = splitLeadingSpaceAndMarks(withMergedUrls.texts[i]);
      if (split === null) continue;
      if (withMergedUrls.kinds[i] !== "space" && withMergedUrls.kinds[i] !== "preserved-space" || withMergedUrls.kinds[i + 1] !== "text" || !containsArabicScript(withMergedUrls.texts[i + 1])) {
        continue;
      }
      withMergedUrls.texts[i] = split.space;
      withMergedUrls.isWordLike[i] = false;
      withMergedUrls.kinds[i] = withMergedUrls.kinds[i] === "preserved-space" ? "preserved-space" : "space";
      withMergedUrls.texts[i + 1] = split.marks + withMergedUrls.texts[i + 1];
      withMergedUrls.starts[i + 1] = withMergedUrls.starts[i] + split.space.length;
    }
    return withMergedUrls;
  }
  function compileAnalysisChunks(segmentation, whiteSpaceProfile) {
    if (segmentation.len === 0) return [];
    if (!whiteSpaceProfile.preserveHardBreaks) {
      return [{
        startSegmentIndex: 0,
        endSegmentIndex: segmentation.len,
        consumedEndSegmentIndex: segmentation.len
      }];
    }
    const chunks = [];
    let startSegmentIndex = 0;
    for (let i = 0; i < segmentation.len; i++) {
      if (segmentation.kinds[i] !== "hard-break") continue;
      chunks.push({
        startSegmentIndex,
        endSegmentIndex: i,
        consumedEndSegmentIndex: i + 1
      });
      startSegmentIndex = i + 1;
    }
    if (startSegmentIndex < segmentation.len) {
      chunks.push({
        startSegmentIndex,
        endSegmentIndex: segmentation.len,
        consumedEndSegmentIndex: segmentation.len
      });
    }
    return chunks;
  }
  function mergeKeepAllTextSegments(segmentation) {
    if (segmentation.len <= 1) return segmentation;
    const texts = [];
    const isWordLike = [];
    const kinds = [];
    const starts = [];
    let pendingTextParts = null;
    let pendingWordLike = false;
    let pendingStart = 0;
    let pendingContainsCJK = false;
    let pendingCanContinue = false;
    function flushPendingText() {
      if (pendingTextParts === null) return;
      texts.push(joinTextParts(pendingTextParts));
      isWordLike.push(pendingWordLike);
      kinds.push("text");
      starts.push(pendingStart);
      pendingTextParts = null;
    }
    for (let i = 0; i < segmentation.len; i++) {
      const text = segmentation.texts[i];
      const kind = segmentation.kinds[i];
      const wordLike = segmentation.isWordLike[i];
      const start = segmentation.starts[i];
      if (kind === "text") {
        const textContainsCJK = containsCJKText(text);
        const textCanContinue = canContinueKeepAllTextRun(text);
        if (pendingTextParts !== null && pendingContainsCJK && pendingCanContinue) {
          pendingTextParts.push(text);
          pendingWordLike = pendingWordLike || wordLike;
          pendingContainsCJK = pendingContainsCJK || textContainsCJK;
          pendingCanContinue = textCanContinue;
          continue;
        }
        flushPendingText();
        pendingTextParts = [text];
        pendingWordLike = wordLike;
        pendingStart = start;
        pendingContainsCJK = textContainsCJK;
        pendingCanContinue = textCanContinue;
        continue;
      }
      flushPendingText();
      texts.push(text);
      isWordLike.push(wordLike);
      kinds.push(kind);
      starts.push(start);
    }
    flushPendingText();
    return {
      len: texts.length,
      texts,
      isWordLike,
      kinds,
      starts
    };
  }
  function analyzeText(text, profile, whiteSpace = "normal", wordBreak = "normal") {
    const whiteSpaceProfile = getWhiteSpaceProfile(whiteSpace);
    const normalized = whiteSpaceProfile.mode === "pre-wrap" ? normalizeWhitespacePreWrap(text) : normalizeWhitespaceNormal(text);
    if (normalized.length === 0) {
      return {
        normalized,
        chunks: [],
        len: 0,
        texts: [],
        isWordLike: [],
        kinds: [],
        starts: []
      };
    }
    const segmentation = wordBreak === "keep-all" ? mergeKeepAllTextSegments(buildMergedSegmentation(normalized, profile, whiteSpaceProfile)) : buildMergedSegmentation(normalized, profile, whiteSpaceProfile);
    return {
      normalized,
      chunks: compileAnalysisChunks(segmentation, whiteSpaceProfile),
      ...segmentation
    };
  }
  var collapsibleWhitespaceRunRe, needsWhitespaceNormalizationRe, sharedWordSegmenter, segmenterLocale, arabicScriptRe, combiningMarkRe, decimalDigitRe, keepAllGlueChars, kinsokuStart, kinsokuEnd, forwardStickyGlue, leftStickyPunctuation, arabicNoSpaceTrailingPunctuation, myanmarMedialGlue, closingQuoteChars, breakCharRe, urlSchemeSegmentRe, numericJoinerChars, asciiPunctuationChainSegmentRe, asciiPunctuationChainTrailingJoinersRe;
  var init_analysis = __esm({
    "src/analysis.ts"() {
      collapsibleWhitespaceRunRe = /[ \t\n\r\f]+/g;
      needsWhitespaceNormalizationRe = /[\t\n\r\f]| {2,}|^ | $/;
      sharedWordSegmenter = null;
      arabicScriptRe = /\p{Script=Arabic}/u;
      combiningMarkRe = /\p{M}/u;
      decimalDigitRe = /\p{Nd}/u;
      keepAllGlueChars = /* @__PURE__ */ new Set([
        "\xA0",
        "\u202F",
        "\u2060",
        "\uFEFF"
      ]);
      kinsokuStart = /* @__PURE__ */ new Set([
        "\uFF0C",
        "\uFF0E",
        "\uFF01",
        "\uFF1A",
        "\uFF1B",
        "\uFF1F",
        "\u3001",
        "\u3002",
        "\u30FB",
        "\uFF09",
        "\u3015",
        "\u3009",
        "\u300B",
        "\u300D",
        "\u300F",
        "\u3011",
        "\u3017",
        "\u3019",
        "\u301B",
        "\u30FC",
        "\u3005",
        "\u303B",
        "\u309D",
        "\u309E",
        "\u30FD",
        "\u30FE"
      ]);
      kinsokuEnd = /* @__PURE__ */ new Set([
        '"',
        "(",
        "[",
        "{",
        "\u201C",
        "\u2018",
        "\xAB",
        "\u2039",
        "\uFF08",
        "\u3014",
        "\u3008",
        "\u300A",
        "\u300C",
        "\u300E",
        "\u3010",
        "\u3016",
        "\u3018",
        "\u301A"
      ]);
      forwardStickyGlue = /* @__PURE__ */ new Set([
        "'",
        "\u2019"
      ]);
      leftStickyPunctuation = /* @__PURE__ */ new Set([
        ".",
        ",",
        "!",
        "?",
        ":",
        ";",
        "\u060C",
        "\u061B",
        "\u061F",
        "\u0964",
        "\u0965",
        "\u104A",
        "\u104B",
        "\u104C",
        "\u104D",
        "\u104F",
        ")",
        "]",
        "}",
        "%",
        '"',
        "\u201D",
        "\u2019",
        "\xBB",
        "\u203A",
        "\u2026"
      ]);
      arabicNoSpaceTrailingPunctuation = /* @__PURE__ */ new Set([
        ":",
        ".",
        "\u060C",
        "\u061B"
      ]);
      myanmarMedialGlue = /* @__PURE__ */ new Set([
        "\u104F"
      ]);
      closingQuoteChars = /* @__PURE__ */ new Set([
        "\u201D",
        "\u2019",
        "\xBB",
        "\u203A",
        "\u300D",
        "\u300F",
        "\u3011",
        "\u300B",
        "\u3009",
        "\u3015",
        "\uFF09"
      ]);
      breakCharRe = /[\x20\t\n\xA0\xAD\u200B\u202F\u2060\uFEFF]/;
      urlSchemeSegmentRe = /^[A-Za-z][A-Za-z0-9+.-]*:$/;
      numericJoinerChars = /* @__PURE__ */ new Set([
        ":",
        "-",
        "/",
        "\xD7",
        ",",
        ".",
        "+",
        "\u2013",
        "\u2014"
      ]);
      asciiPunctuationChainSegmentRe = /^[A-Za-z0-9_]+[,:;]*$/;
      asciiPunctuationChainTrailingJoinersRe = /[,:;]+$/;
    }
  });

  // src/generated/bidi-data.ts
  var latin1BidiTypes, nonLatin1BidiRanges;
  var init_bidi_data = __esm({
    "src/generated/bidi-data.ts"() {
      latin1BidiTypes = [
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "S",
        "B",
        "S",
        "WS",
        "B",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "B",
        "B",
        "B",
        "S",
        "WS",
        "ON",
        "ON",
        "ET",
        "ET",
        "ET",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "ES",
        "CS",
        "ES",
        "CS",
        "CS",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "EN",
        "CS",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "ON",
        "ON",
        "ON",
        "ON",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "B",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "BN",
        "CS",
        "ON",
        "ET",
        "ET",
        "ET",
        "ET",
        "ON",
        "ON",
        "ON",
        "ON",
        "L",
        "ON",
        "ON",
        "BN",
        "ON",
        "ON",
        "ET",
        "ET",
        "EN",
        "EN",
        "ON",
        "L",
        "ON",
        "ON",
        "ON",
        "EN",
        "L",
        "ON",
        "ON",
        "ON",
        "ON",
        "ON",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "ON",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "ON",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L",
        "L"
      ];
      nonLatin1BidiRanges = [
        [697, 698, "ON"],
        [706, 719, "ON"],
        [722, 735, "ON"],
        [741, 749, "ON"],
        [751, 767, "ON"],
        [768, 879, "NSM"],
        [884, 885, "ON"],
        [894, 894, "ON"],
        [900, 901, "ON"],
        [903, 903, "ON"],
        [1014, 1014, "ON"],
        [1155, 1161, "NSM"],
        [1418, 1418, "ON"],
        [1421, 1422, "ON"],
        [1423, 1423, "ET"],
        [1424, 1424, "R"],
        [1425, 1469, "NSM"],
        [1470, 1470, "R"],
        [1471, 1471, "NSM"],
        [1472, 1472, "R"],
        [1473, 1474, "NSM"],
        [1475, 1475, "R"],
        [1476, 1477, "NSM"],
        [1478, 1478, "R"],
        [1479, 1479, "NSM"],
        [1480, 1535, "R"],
        [1536, 1541, "AN"],
        [1542, 1543, "ON"],
        [1544, 1544, "AL"],
        [1545, 1546, "ET"],
        [1547, 1547, "AL"],
        [1548, 1548, "CS"],
        [1549, 1549, "AL"],
        [1550, 1551, "ON"],
        [1552, 1562, "NSM"],
        [1563, 1610, "AL"],
        [1611, 1631, "NSM"],
        [1632, 1641, "AN"],
        [1642, 1642, "ET"],
        [1643, 1644, "AN"],
        [1645, 1647, "AL"],
        [1648, 1648, "NSM"],
        [1649, 1749, "AL"],
        [1750, 1756, "NSM"],
        [1757, 1757, "AN"],
        [1758, 1758, "ON"],
        [1759, 1764, "NSM"],
        [1765, 1766, "AL"],
        [1767, 1768, "NSM"],
        [1769, 1769, "ON"],
        [1770, 1773, "NSM"],
        [1774, 1775, "AL"],
        [1776, 1785, "EN"],
        [1786, 1808, "AL"],
        [1809, 1809, "NSM"],
        [1810, 1839, "AL"],
        [1840, 1866, "NSM"],
        [1867, 1957, "AL"],
        [1958, 1968, "NSM"],
        [1969, 1983, "AL"],
        [1984, 2026, "R"],
        [2027, 2035, "NSM"],
        [2036, 2037, "R"],
        [2038, 2041, "ON"],
        [2042, 2044, "R"],
        [2045, 2045, "NSM"],
        [2046, 2069, "R"],
        [2070, 2073, "NSM"],
        [2074, 2074, "R"],
        [2075, 2083, "NSM"],
        [2084, 2084, "R"],
        [2085, 2087, "NSM"],
        [2088, 2088, "R"],
        [2089, 2093, "NSM"],
        [2094, 2136, "R"],
        [2137, 2139, "NSM"],
        [2140, 2143, "R"],
        [2144, 2191, "AL"],
        [2192, 2193, "AN"],
        [2194, 2198, "AL"],
        [2199, 2207, "NSM"],
        [2208, 2249, "AL"],
        [2250, 2273, "NSM"],
        [2274, 2274, "AN"],
        [2275, 2306, "NSM"],
        [2362, 2362, "NSM"],
        [2364, 2364, "NSM"],
        [2369, 2376, "NSM"],
        [2381, 2381, "NSM"],
        [2385, 2391, "NSM"],
        [2402, 2403, "NSM"],
        [2433, 2433, "NSM"],
        [2492, 2492, "NSM"],
        [2497, 2500, "NSM"],
        [2509, 2509, "NSM"],
        [2530, 2531, "NSM"],
        [2546, 2547, "ET"],
        [2555, 2555, "ET"],
        [2558, 2558, "NSM"],
        [2561, 2562, "NSM"],
        [2620, 2620, "NSM"],
        [2625, 2626, "NSM"],
        [2631, 2632, "NSM"],
        [2635, 2637, "NSM"],
        [2641, 2641, "NSM"],
        [2672, 2673, "NSM"],
        [2677, 2677, "NSM"],
        [2689, 2690, "NSM"],
        [2748, 2748, "NSM"],
        [2753, 2757, "NSM"],
        [2759, 2760, "NSM"],
        [2765, 2765, "NSM"],
        [2786, 2787, "NSM"],
        [2801, 2801, "ET"],
        [2810, 2815, "NSM"],
        [2817, 2817, "NSM"],
        [2876, 2876, "NSM"],
        [2879, 2879, "NSM"],
        [2881, 2884, "NSM"],
        [2893, 2893, "NSM"],
        [2901, 2902, "NSM"],
        [2914, 2915, "NSM"],
        [2946, 2946, "NSM"],
        [3008, 3008, "NSM"],
        [3021, 3021, "NSM"],
        [3059, 3064, "ON"],
        [3065, 3065, "ET"],
        [3066, 3066, "ON"],
        [3072, 3072, "NSM"],
        [3076, 3076, "NSM"],
        [3132, 3132, "NSM"],
        [3134, 3136, "NSM"],
        [3142, 3144, "NSM"],
        [3146, 3149, "NSM"],
        [3157, 3158, "NSM"],
        [3170, 3171, "NSM"],
        [3192, 3198, "ON"],
        [3201, 3201, "NSM"],
        [3260, 3260, "NSM"],
        [3276, 3277, "NSM"],
        [3298, 3299, "NSM"],
        [3328, 3329, "NSM"],
        [3387, 3388, "NSM"],
        [3393, 3396, "NSM"],
        [3405, 3405, "NSM"],
        [3426, 3427, "NSM"],
        [3457, 3457, "NSM"],
        [3530, 3530, "NSM"],
        [3538, 3540, "NSM"],
        [3542, 3542, "NSM"],
        [3633, 3633, "NSM"],
        [3636, 3642, "NSM"],
        [3647, 3647, "ET"],
        [3655, 3662, "NSM"],
        [3761, 3761, "NSM"],
        [3764, 3772, "NSM"],
        [3784, 3790, "NSM"],
        [3864, 3865, "NSM"],
        [3893, 3893, "NSM"],
        [3895, 3895, "NSM"],
        [3897, 3897, "NSM"],
        [3898, 3901, "ON"],
        [3953, 3966, "NSM"],
        [3968, 3972, "NSM"],
        [3974, 3975, "NSM"],
        [3981, 3991, "NSM"],
        [3993, 4028, "NSM"],
        [4038, 4038, "NSM"],
        [4141, 4144, "NSM"],
        [4146, 4151, "NSM"],
        [4153, 4154, "NSM"],
        [4157, 4158, "NSM"],
        [4184, 4185, "NSM"],
        [4190, 4192, "NSM"],
        [4209, 4212, "NSM"],
        [4226, 4226, "NSM"],
        [4229, 4230, "NSM"],
        [4237, 4237, "NSM"],
        [4253, 4253, "NSM"],
        [4957, 4959, "NSM"],
        [5008, 5017, "ON"],
        [5120, 5120, "ON"],
        [5760, 5760, "WS"],
        [5787, 5788, "ON"],
        [5906, 5908, "NSM"],
        [5938, 5939, "NSM"],
        [5970, 5971, "NSM"],
        [6002, 6003, "NSM"],
        [6068, 6069, "NSM"],
        [6071, 6077, "NSM"],
        [6086, 6086, "NSM"],
        [6089, 6099, "NSM"],
        [6107, 6107, "ET"],
        [6109, 6109, "NSM"],
        [6128, 6137, "ON"],
        [6144, 6154, "ON"],
        [6155, 6157, "NSM"],
        [6158, 6158, "BN"],
        [6159, 6159, "NSM"],
        [6277, 6278, "NSM"],
        [6313, 6313, "NSM"],
        [6432, 6434, "NSM"],
        [6439, 6440, "NSM"],
        [6450, 6450, "NSM"],
        [6457, 6459, "NSM"],
        [6464, 6464, "ON"],
        [6468, 6469, "ON"],
        [6622, 6655, "ON"],
        [6679, 6680, "NSM"],
        [6683, 6683, "NSM"],
        [6742, 6742, "NSM"],
        [6744, 6750, "NSM"],
        [6752, 6752, "NSM"],
        [6754, 6754, "NSM"],
        [6757, 6764, "NSM"],
        [6771, 6780, "NSM"],
        [6783, 6783, "NSM"],
        [6832, 6877, "NSM"],
        [6880, 6891, "NSM"],
        [6912, 6915, "NSM"],
        [6964, 6964, "NSM"],
        [6966, 6970, "NSM"],
        [6972, 6972, "NSM"],
        [6978, 6978, "NSM"],
        [7019, 7027, "NSM"],
        [7040, 7041, "NSM"],
        [7074, 7077, "NSM"],
        [7080, 7081, "NSM"],
        [7083, 7085, "NSM"],
        [7142, 7142, "NSM"],
        [7144, 7145, "NSM"],
        [7149, 7149, "NSM"],
        [7151, 7153, "NSM"],
        [7212, 7219, "NSM"],
        [7222, 7223, "NSM"],
        [7376, 7378, "NSM"],
        [7380, 7392, "NSM"],
        [7394, 7400, "NSM"],
        [7405, 7405, "NSM"],
        [7412, 7412, "NSM"],
        [7416, 7417, "NSM"],
        [7616, 7679, "NSM"],
        [8125, 8125, "ON"],
        [8127, 8129, "ON"],
        [8141, 8143, "ON"],
        [8157, 8159, "ON"],
        [8173, 8175, "ON"],
        [8189, 8190, "ON"],
        [8192, 8202, "WS"],
        [8203, 8205, "BN"],
        [8207, 8207, "R"],
        [8208, 8231, "ON"],
        [8232, 8232, "WS"],
        [8233, 8233, "B"],
        [8234, 8238, "BN"],
        [8239, 8239, "CS"],
        [8240, 8244, "ET"],
        [8245, 8259, "ON"],
        [8260, 8260, "CS"],
        [8261, 8286, "ON"],
        [8287, 8287, "WS"],
        [8288, 8303, "BN"],
        [8304, 8304, "EN"],
        [8308, 8313, "EN"],
        [8314, 8315, "ES"],
        [8316, 8318, "ON"],
        [8320, 8329, "EN"],
        [8330, 8331, "ES"],
        [8332, 8334, "ON"],
        [8352, 8399, "ET"],
        [8400, 8432, "NSM"],
        [8448, 8449, "ON"],
        [8451, 8454, "ON"],
        [8456, 8457, "ON"],
        [8468, 8468, "ON"],
        [8470, 8472, "ON"],
        [8478, 8483, "ON"],
        [8485, 8485, "ON"],
        [8487, 8487, "ON"],
        [8489, 8489, "ON"],
        [8494, 8494, "ET"],
        [8506, 8507, "ON"],
        [8512, 8516, "ON"],
        [8522, 8525, "ON"],
        [8528, 8543, "ON"],
        [8585, 8587, "ON"],
        [8592, 8721, "ON"],
        [8722, 8722, "ES"],
        [8723, 8723, "ET"],
        [8724, 9013, "ON"],
        [9083, 9108, "ON"],
        [9110, 9257, "ON"],
        [9280, 9290, "ON"],
        [9312, 9351, "ON"],
        [9352, 9371, "EN"],
        [9450, 9899, "ON"],
        [9901, 10239, "ON"],
        [10496, 11123, "ON"],
        [11126, 11263, "ON"],
        [11493, 11498, "ON"],
        [11503, 11505, "NSM"],
        [11513, 11519, "ON"],
        [11647, 11647, "NSM"],
        [11744, 11775, "NSM"],
        [11776, 11869, "ON"],
        [11904, 11929, "ON"],
        [11931, 12019, "ON"],
        [12032, 12245, "ON"],
        [12272, 12287, "ON"],
        [12288, 12288, "WS"],
        [12289, 12292, "ON"],
        [12296, 12320, "ON"],
        [12330, 12333, "NSM"],
        [12336, 12336, "ON"],
        [12342, 12343, "ON"],
        [12349, 12351, "ON"],
        [12441, 12442, "NSM"],
        [12443, 12444, "ON"],
        [12448, 12448, "ON"],
        [12539, 12539, "ON"],
        [12736, 12773, "ON"],
        [12783, 12783, "ON"],
        [12829, 12830, "ON"],
        [12880, 12895, "ON"],
        [12924, 12926, "ON"],
        [12977, 12991, "ON"],
        [13004, 13007, "ON"],
        [13175, 13178, "ON"],
        [13278, 13279, "ON"],
        [13311, 13311, "ON"],
        [19904, 19967, "ON"],
        [42128, 42182, "ON"],
        [42509, 42511, "ON"],
        [42607, 42610, "NSM"],
        [42611, 42611, "ON"],
        [42612, 42621, "NSM"],
        [42622, 42623, "ON"],
        [42654, 42655, "NSM"],
        [42736, 42737, "NSM"],
        [42752, 42785, "ON"],
        [42888, 42888, "ON"],
        [43010, 43010, "NSM"],
        [43014, 43014, "NSM"],
        [43019, 43019, "NSM"],
        [43045, 43046, "NSM"],
        [43048, 43051, "ON"],
        [43052, 43052, "NSM"],
        [43064, 43065, "ET"],
        [43124, 43127, "ON"],
        [43204, 43205, "NSM"],
        [43232, 43249, "NSM"],
        [43263, 43263, "NSM"],
        [43302, 43309, "NSM"],
        [43335, 43345, "NSM"],
        [43392, 43394, "NSM"],
        [43443, 43443, "NSM"],
        [43446, 43449, "NSM"],
        [43452, 43453, "NSM"],
        [43493, 43493, "NSM"],
        [43561, 43566, "NSM"],
        [43569, 43570, "NSM"],
        [43573, 43574, "NSM"],
        [43587, 43587, "NSM"],
        [43596, 43596, "NSM"],
        [43644, 43644, "NSM"],
        [43696, 43696, "NSM"],
        [43698, 43700, "NSM"],
        [43703, 43704, "NSM"],
        [43710, 43711, "NSM"],
        [43713, 43713, "NSM"],
        [43756, 43757, "NSM"],
        [43766, 43766, "NSM"],
        [43882, 43883, "ON"],
        [44005, 44005, "NSM"],
        [44008, 44008, "NSM"],
        [44013, 44013, "NSM"],
        [64285, 64285, "R"],
        [64286, 64286, "NSM"],
        [64287, 64296, "R"],
        [64297, 64297, "ES"],
        [64298, 64335, "R"],
        [64336, 64450, "AL"],
        [64451, 64466, "ON"],
        [64467, 64829, "AL"],
        [64830, 64847, "ON"],
        [64848, 64911, "AL"],
        [64912, 64913, "ON"],
        [64914, 64967, "AL"],
        [64968, 64975, "ON"],
        [64976, 65007, "BN"],
        [65008, 65020, "AL"],
        [65021, 65023, "ON"],
        [65024, 65039, "NSM"],
        [65040, 65049, "ON"],
        [65056, 65071, "NSM"],
        [65072, 65103, "ON"],
        [65104, 65104, "CS"],
        [65105, 65105, "ON"],
        [65106, 65106, "CS"],
        [65108, 65108, "ON"],
        [65109, 65109, "CS"],
        [65110, 65118, "ON"],
        [65119, 65119, "ET"],
        [65120, 65121, "ON"],
        [65122, 65123, "ES"],
        [65124, 65126, "ON"],
        [65128, 65128, "ON"],
        [65129, 65130, "ET"],
        [65131, 65131, "ON"],
        [65136, 65278, "AL"],
        [65279, 65279, "BN"],
        [65281, 65282, "ON"],
        [65283, 65285, "ET"],
        [65286, 65290, "ON"],
        [65291, 65291, "ES"],
        [65292, 65292, "CS"],
        [65293, 65293, "ES"],
        [65294, 65295, "CS"],
        [65296, 65305, "EN"],
        [65306, 65306, "CS"],
        [65307, 65312, "ON"],
        [65339, 65344, "ON"],
        [65371, 65381, "ON"],
        [65504, 65505, "ET"],
        [65506, 65508, "ON"],
        [65509, 65510, "ET"],
        [65512, 65518, "ON"],
        [65520, 65528, "BN"],
        [65529, 65533, "ON"],
        [65534, 65535, "BN"],
        [65793, 65793, "ON"],
        [65856, 65932, "ON"],
        [65936, 65948, "ON"],
        [65952, 65952, "ON"],
        [66045, 66045, "NSM"],
        [66272, 66272, "NSM"],
        [66273, 66299, "EN"],
        [66422, 66426, "NSM"],
        [67584, 67870, "R"],
        [67871, 67871, "ON"],
        [67872, 68096, "R"],
        [68097, 68099, "NSM"],
        [68100, 68100, "R"],
        [68101, 68102, "NSM"],
        [68103, 68107, "R"],
        [68108, 68111, "NSM"],
        [68112, 68151, "R"],
        [68152, 68154, "NSM"],
        [68155, 68158, "R"],
        [68159, 68159, "NSM"],
        [68160, 68324, "R"],
        [68325, 68326, "NSM"],
        [68327, 68408, "R"],
        [68409, 68415, "ON"],
        [68416, 68863, "R"],
        [68864, 68899, "AL"],
        [68900, 68903, "NSM"],
        [68904, 68911, "AL"],
        [68912, 68921, "AN"],
        [68922, 68927, "AL"],
        [68928, 68937, "AN"],
        [68938, 68968, "R"],
        [68969, 68973, "NSM"],
        [68974, 68974, "ON"],
        [68975, 69215, "R"],
        [69216, 69246, "AN"],
        [69247, 69290, "R"],
        [69291, 69292, "NSM"],
        [69293, 69311, "R"],
        [69312, 69327, "AL"],
        [69328, 69336, "ON"],
        [69337, 69369, "AL"],
        [69370, 69375, "NSM"],
        [69376, 69423, "R"],
        [69424, 69445, "AL"],
        [69446, 69456, "NSM"],
        [69457, 69487, "AL"],
        [69488, 69505, "R"],
        [69506, 69509, "NSM"],
        [69510, 69631, "R"],
        [69633, 69633, "NSM"],
        [69688, 69702, "NSM"],
        [69714, 69733, "ON"],
        [69744, 69744, "NSM"],
        [69747, 69748, "NSM"],
        [69759, 69761, "NSM"],
        [69811, 69814, "NSM"],
        [69817, 69818, "NSM"],
        [69826, 69826, "NSM"],
        [69888, 69890, "NSM"],
        [69927, 69931, "NSM"],
        [69933, 69940, "NSM"],
        [70003, 70003, "NSM"],
        [70016, 70017, "NSM"],
        [70070, 70078, "NSM"],
        [70089, 70092, "NSM"],
        [70095, 70095, "NSM"],
        [70191, 70193, "NSM"],
        [70196, 70196, "NSM"],
        [70198, 70199, "NSM"],
        [70206, 70206, "NSM"],
        [70209, 70209, "NSM"],
        [70367, 70367, "NSM"],
        [70371, 70378, "NSM"],
        [70400, 70401, "NSM"],
        [70459, 70460, "NSM"],
        [70464, 70464, "NSM"],
        [70502, 70508, "NSM"],
        [70512, 70516, "NSM"],
        [70587, 70592, "NSM"],
        [70606, 70606, "NSM"],
        [70608, 70608, "NSM"],
        [70610, 70610, "NSM"],
        [70625, 70626, "NSM"],
        [70712, 70719, "NSM"],
        [70722, 70724, "NSM"],
        [70726, 70726, "NSM"],
        [70750, 70750, "NSM"],
        [70835, 70840, "NSM"],
        [70842, 70842, "NSM"],
        [70847, 70848, "NSM"],
        [70850, 70851, "NSM"],
        [71090, 71093, "NSM"],
        [71100, 71101, "NSM"],
        [71103, 71104, "NSM"],
        [71132, 71133, "NSM"],
        [71219, 71226, "NSM"],
        [71229, 71229, "NSM"],
        [71231, 71232, "NSM"],
        [71264, 71276, "ON"],
        [71339, 71339, "NSM"],
        [71341, 71341, "NSM"],
        [71344, 71349, "NSM"],
        [71351, 71351, "NSM"],
        [71453, 71453, "NSM"],
        [71455, 71455, "NSM"],
        [71458, 71461, "NSM"],
        [71463, 71467, "NSM"],
        [71727, 71735, "NSM"],
        [71737, 71738, "NSM"],
        [71995, 71996, "NSM"],
        [71998, 71998, "NSM"],
        [72003, 72003, "NSM"],
        [72148, 72151, "NSM"],
        [72154, 72155, "NSM"],
        [72160, 72160, "NSM"],
        [72193, 72198, "NSM"],
        [72201, 72202, "NSM"],
        [72243, 72248, "NSM"],
        [72251, 72254, "NSM"],
        [72263, 72263, "NSM"],
        [72273, 72278, "NSM"],
        [72281, 72283, "NSM"],
        [72330, 72342, "NSM"],
        [72344, 72345, "NSM"],
        [72544, 72544, "NSM"],
        [72546, 72548, "NSM"],
        [72550, 72550, "NSM"],
        [72752, 72758, "NSM"],
        [72760, 72765, "NSM"],
        [72850, 72871, "NSM"],
        [72874, 72880, "NSM"],
        [72882, 72883, "NSM"],
        [72885, 72886, "NSM"],
        [73009, 73014, "NSM"],
        [73018, 73018, "NSM"],
        [73020, 73021, "NSM"],
        [73023, 73029, "NSM"],
        [73031, 73031, "NSM"],
        [73104, 73105, "NSM"],
        [73109, 73109, "NSM"],
        [73111, 73111, "NSM"],
        [73459, 73460, "NSM"],
        [73472, 73473, "NSM"],
        [73526, 73530, "NSM"],
        [73536, 73536, "NSM"],
        [73538, 73538, "NSM"],
        [73562, 73562, "NSM"],
        [73685, 73692, "ON"],
        [73693, 73696, "ET"],
        [73697, 73713, "ON"],
        [78912, 78912, "NSM"],
        [78919, 78933, "NSM"],
        [90398, 90409, "NSM"],
        [90413, 90415, "NSM"],
        [92912, 92916, "NSM"],
        [92976, 92982, "NSM"],
        [94031, 94031, "NSM"],
        [94095, 94098, "NSM"],
        [94178, 94178, "ON"],
        [94180, 94180, "NSM"],
        [113821, 113822, "NSM"],
        [113824, 113827, "BN"],
        [117760, 117973, "ON"],
        [118e3, 118009, "EN"],
        [118010, 118012, "ON"],
        [118016, 118451, "ON"],
        [118458, 118480, "ON"],
        [118496, 118512, "ON"],
        [118528, 118573, "NSM"],
        [118576, 118598, "NSM"],
        [119143, 119145, "NSM"],
        [119155, 119162, "BN"],
        [119163, 119170, "NSM"],
        [119173, 119179, "NSM"],
        [119210, 119213, "NSM"],
        [119273, 119274, "ON"],
        [119296, 119361, "ON"],
        [119362, 119364, "NSM"],
        [119365, 119365, "ON"],
        [119552, 119638, "ON"],
        [120513, 120513, "ON"],
        [120539, 120539, "ON"],
        [120571, 120571, "ON"],
        [120597, 120597, "ON"],
        [120629, 120629, "ON"],
        [120655, 120655, "ON"],
        [120687, 120687, "ON"],
        [120713, 120713, "ON"],
        [120745, 120745, "ON"],
        [120771, 120771, "ON"],
        [120782, 120831, "EN"],
        [121344, 121398, "NSM"],
        [121403, 121452, "NSM"],
        [121461, 121461, "NSM"],
        [121476, 121476, "NSM"],
        [121499, 121503, "NSM"],
        [121505, 121519, "NSM"],
        [122880, 122886, "NSM"],
        [122888, 122904, "NSM"],
        [122907, 122913, "NSM"],
        [122915, 122916, "NSM"],
        [122918, 122922, "NSM"],
        [123023, 123023, "NSM"],
        [123184, 123190, "NSM"],
        [123566, 123566, "NSM"],
        [123628, 123631, "NSM"],
        [123647, 123647, "ET"],
        [124140, 124143, "NSM"],
        [124398, 124399, "NSM"],
        [124643, 124643, "NSM"],
        [124646, 124646, "NSM"],
        [124654, 124655, "NSM"],
        [124661, 124661, "NSM"],
        [124928, 125135, "R"],
        [125136, 125142, "NSM"],
        [125143, 125251, "R"],
        [125252, 125258, "NSM"],
        [125259, 126063, "R"],
        [126064, 126143, "AL"],
        [126144, 126207, "R"],
        [126208, 126287, "AL"],
        [126288, 126463, "R"],
        [126464, 126703, "AL"],
        [126704, 126705, "ON"],
        [126706, 126719, "AL"],
        [126720, 126975, "R"],
        [126976, 127019, "ON"],
        [127024, 127123, "ON"],
        [127136, 127150, "ON"],
        [127153, 127167, "ON"],
        [127169, 127183, "ON"],
        [127185, 127221, "ON"],
        [127232, 127242, "EN"],
        [127243, 127247, "ON"],
        [127279, 127279, "ON"],
        [127338, 127343, "ON"],
        [127405, 127405, "ON"],
        [127584, 127589, "ON"],
        [127744, 128728, "ON"],
        [128732, 128748, "ON"],
        [128752, 128764, "ON"],
        [128768, 128985, "ON"],
        [128992, 129003, "ON"],
        [129008, 129008, "ON"],
        [129024, 129035, "ON"],
        [129040, 129095, "ON"],
        [129104, 129113, "ON"],
        [129120, 129159, "ON"],
        [129168, 129197, "ON"],
        [129200, 129211, "ON"],
        [129216, 129217, "ON"],
        [129232, 129240, "ON"],
        [129280, 129623, "ON"],
        [129632, 129645, "ON"],
        [129648, 129660, "ON"],
        [129664, 129674, "ON"],
        [129678, 129734, "ON"],
        [129736, 129736, "ON"],
        [129741, 129756, "ON"],
        [129759, 129770, "ON"],
        [129775, 129784, "ON"],
        [129792, 129938, "ON"],
        [129940, 130031, "ON"],
        [130032, 130041, "EN"],
        [130042, 130042, "ON"],
        [131070, 131071, "BN"],
        [196606, 196607, "BN"],
        [262142, 262143, "BN"],
        [327678, 327679, "BN"],
        [393214, 393215, "BN"],
        [458750, 458751, "BN"],
        [524286, 524287, "BN"],
        [589822, 589823, "BN"],
        [655358, 655359, "BN"],
        [720894, 720895, "BN"],
        [786430, 786431, "BN"],
        [851966, 851967, "BN"],
        [917502, 917759, "BN"],
        [917760, 917999, "NSM"],
        [918e3, 921599, "BN"],
        [983038, 983039, "BN"],
        [1048574, 1048575, "BN"],
        [1114110, 1114111, "BN"]
      ];
    }
  });

  // src/bidi.ts
  function classifyCodePoint(codePoint) {
    if (codePoint <= 255) return latin1BidiTypes[codePoint];
    let lo = 0;
    let hi = nonLatin1BidiRanges.length - 1;
    while (lo <= hi) {
      const mid = lo + hi >> 1;
      const range = nonLatin1BidiRanges[mid];
      if (codePoint < range[0]) {
        hi = mid - 1;
        continue;
      }
      if (codePoint > range[1]) {
        lo = mid + 1;
        continue;
      }
      return range[2];
    }
    return "L";
  }
  function computeBidiLevels(str) {
    const len = str.length;
    if (len === 0) return null;
    const types = new Array(len);
    let sawBidi = false;
    for (let i = 0; i < len; ) {
      const first = str.charCodeAt(i);
      let codePoint = first;
      let codeUnitLength = 1;
      if (first >= 55296 && first <= 56319 && i + 1 < len) {
        const second = str.charCodeAt(i + 1);
        if (second >= 56320 && second <= 57343) {
          codePoint = (first - 55296 << 10) + (second - 56320) + 65536;
          codeUnitLength = 2;
        }
      }
      const t = classifyCodePoint(codePoint);
      if (t === "R" || t === "AL" || t === "AN") sawBidi = true;
      for (let j = 0; j < codeUnitLength; j++) {
        types[i + j] = t;
      }
      i += codeUnitLength;
    }
    if (!sawBidi) return null;
    let startLevel = 0;
    for (let i = 0; i < len; i++) {
      const t = types[i];
      if (t === "L") {
        startLevel = 0;
        break;
      }
      if (t === "R" || t === "AL") {
        startLevel = 1;
        break;
      }
    }
    const levels = new Int8Array(len);
    for (let i = 0; i < len; i++) levels[i] = startLevel;
    const e = startLevel & 1 ? "R" : "L";
    const sor = e;
    let lastType = sor;
    for (let i = 0; i < len; i++) {
      if (types[i] === "NSM") types[i] = lastType;
      else lastType = types[i];
    }
    lastType = sor;
    for (let i = 0; i < len; i++) {
      const t = types[i];
      if (t === "EN") types[i] = lastType === "AL" ? "AN" : "EN";
      else if (t === "R" || t === "L" || t === "AL") lastType = t;
    }
    for (let i = 0; i < len; i++) {
      if (types[i] === "AL") types[i] = "R";
    }
    for (let i = 1; i < len - 1; i++) {
      if (types[i] === "ES" && types[i - 1] === "EN" && types[i + 1] === "EN") {
        types[i] = "EN";
      }
      if (types[i] === "CS" && (types[i - 1] === "EN" || types[i - 1] === "AN") && types[i + 1] === types[i - 1]) {
        types[i] = types[i - 1];
      }
    }
    for (let i = 0; i < len; i++) {
      if (types[i] !== "EN") continue;
      let j;
      for (j = i - 1; j >= 0 && types[j] === "ET"; j--) types[j] = "EN";
      for (j = i + 1; j < len && types[j] === "ET"; j++) types[j] = "EN";
    }
    for (let i = 0; i < len; i++) {
      const t = types[i];
      if (t === "WS" || t === "ES" || t === "ET" || t === "CS") types[i] = "ON";
    }
    lastType = sor;
    for (let i = 0; i < len; i++) {
      const t = types[i];
      if (t === "EN") types[i] = lastType === "L" ? "L" : "EN";
      else if (t === "R" || t === "L") lastType = t;
    }
    for (let i = 0; i < len; i++) {
      if (types[i] !== "ON") continue;
      let end = i + 1;
      while (end < len && types[end] === "ON") end++;
      const before = i > 0 ? types[i - 1] : sor;
      const after = end < len ? types[end] : sor;
      const bDir = before !== "L" ? "R" : "L";
      const aDir = after !== "L" ? "R" : "L";
      if (bDir === aDir) {
        for (let j = i; j < end; j++) types[j] = bDir;
      }
      i = end - 1;
    }
    for (let i = 0; i < len; i++) {
      if (types[i] === "ON") types[i] = e;
    }
    for (let i = 0; i < len; i++) {
      const t = types[i];
      if ((levels[i] & 1) === 0) {
        if (t === "R") levels[i]++;
        else if (t === "AN" || t === "EN") levels[i] += 2;
      } else if (t === "L" || t === "AN" || t === "EN") {
        levels[i]++;
      }
    }
    return levels;
  }
  function computeSegmentLevels(normalized, segStarts) {
    const bidiLevels = computeBidiLevels(normalized);
    if (bidiLevels === null) return null;
    const segLevels = new Int8Array(segStarts.length);
    for (let i = 0; i < segStarts.length; i++) {
      segLevels[i] = bidiLevels[segStarts[i]];
    }
    return segLevels;
  }
  var init_bidi = __esm({
    "src/bidi.ts"() {
      init_bidi_data();
    }
  });

  // src/measurement.ts
  function getMeasureContext() {
    if (measureContext !== null) return measureContext;
    if (typeof OffscreenCanvas !== "undefined") {
      measureContext = new OffscreenCanvas(1, 1).getContext("2d");
      return measureContext;
    }
    if (typeof document !== "undefined") {
      measureContext = document.createElement("canvas").getContext("2d");
      return measureContext;
    }
    throw new Error("Text measurement requires OffscreenCanvas or a DOM canvas context.");
  }
  function getSegmentMetricCache(font) {
    let cache = segmentMetricCaches.get(font);
    if (!cache) {
      cache = /* @__PURE__ */ new Map();
      segmentMetricCaches.set(font, cache);
    }
    return cache;
  }
  function getSegmentMetrics(seg, cache) {
    let metrics = cache.get(seg);
    if (metrics === void 0) {
      const ctx = getMeasureContext();
      metrics = {
        width: ctx.measureText(seg).width,
        containsCJK: isCJK(seg)
      };
      cache.set(seg, metrics);
    }
    return metrics;
  }
  function getEngineProfile() {
    if (cachedEngineProfile !== null) return cachedEngineProfile;
    if (typeof navigator === "undefined") {
      cachedEngineProfile = {
        lineFitEpsilon: 5e-3,
        carryCJKAfterClosingQuote: false,
        preferPrefixWidthsForBreakableRuns: false,
        preferEarlySoftHyphenBreak: false
      };
      return cachedEngineProfile;
    }
    const ua = navigator.userAgent;
    const vendor = navigator.vendor;
    const isSafari = vendor === "Apple Computer, Inc." && ua.includes("Safari/") && !ua.includes("Chrome/") && !ua.includes("Chromium/") && !ua.includes("CriOS/") && !ua.includes("FxiOS/") && !ua.includes("EdgiOS/");
    const isChromium = ua.includes("Chrome/") || ua.includes("Chromium/") || ua.includes("CriOS/") || ua.includes("Edg/");
    cachedEngineProfile = {
      lineFitEpsilon: isSafari ? 1 / 64 : 5e-3,
      carryCJKAfterClosingQuote: isChromium,
      preferPrefixWidthsForBreakableRuns: isSafari,
      preferEarlySoftHyphenBreak: isSafari
    };
    return cachedEngineProfile;
  }
  function parseFontSize(font) {
    const m = font.match(/(\d+(?:\.\d+)?)\s*px/);
    return m ? parseFloat(m[1]) : 16;
  }
  function getSharedGraphemeSegmenter() {
    if (sharedGraphemeSegmenter === null) {
      sharedGraphemeSegmenter = new Intl.Segmenter(void 0, { granularity: "grapheme" });
    }
    return sharedGraphemeSegmenter;
  }
  function isEmojiGrapheme(g) {
    return emojiPresentationRe.test(g) || g.includes("\uFE0F");
  }
  function textMayContainEmoji(text) {
    return maybeEmojiRe.test(text);
  }
  function getEmojiCorrection(font, fontSize) {
    let correction = emojiCorrectionCache.get(font);
    if (correction !== void 0) return correction;
    const ctx = getMeasureContext();
    ctx.font = font;
    const canvasW = ctx.measureText("\u{1F600}").width;
    correction = 0;
    if (canvasW > fontSize + 0.5 && typeof document !== "undefined" && document.body !== null) {
      const span = document.createElement("span");
      span.style.font = font;
      span.style.display = "inline-block";
      span.style.visibility = "hidden";
      span.style.position = "absolute";
      span.textContent = "\u{1F600}";
      document.body.appendChild(span);
      const domW = span.getBoundingClientRect().width;
      document.body.removeChild(span);
      if (canvasW - domW > 0.5) {
        correction = canvasW - domW;
      }
    }
    emojiCorrectionCache.set(font, correction);
    return correction;
  }
  function countEmojiGraphemes(text) {
    let count = 0;
    const graphemeSegmenter2 = getSharedGraphemeSegmenter();
    for (const g of graphemeSegmenter2.segment(text)) {
      if (isEmojiGrapheme(g.segment)) count++;
    }
    return count;
  }
  function getEmojiCount(seg, metrics) {
    if (metrics.emojiCount === void 0) {
      metrics.emojiCount = countEmojiGraphemes(seg);
    }
    return metrics.emojiCount;
  }
  function getCorrectedSegmentWidth(seg, metrics, emojiCorrection) {
    if (emojiCorrection === 0) return metrics.width;
    return metrics.width - getEmojiCount(seg, metrics) * emojiCorrection;
  }
  function getSegmentBreakableFitAdvances(seg, metrics, cache, emojiCorrection, mode) {
    if (metrics.breakableFitAdvances !== void 0) return metrics.breakableFitAdvances;
    const graphemeSegmenter2 = getSharedGraphemeSegmenter();
    const graphemes = [];
    for (const gs of graphemeSegmenter2.segment(seg)) {
      graphemes.push(gs.segment);
    }
    if (graphemes.length <= 1) {
      metrics.breakableFitAdvances = null;
      return metrics.breakableFitAdvances;
    }
    if (mode === "sum-graphemes") {
      const advances2 = [];
      for (const grapheme of graphemes) {
        const graphemeMetrics = getSegmentMetrics(grapheme, cache);
        advances2.push(getCorrectedSegmentWidth(grapheme, graphemeMetrics, emojiCorrection));
      }
      metrics.breakableFitAdvances = advances2;
      return metrics.breakableFitAdvances;
    }
    if (mode === "pair-context" || graphemes.length > MAX_PREFIX_FIT_GRAPHEMES) {
      const advances2 = [];
      let previousGrapheme = null;
      let previousWidth = 0;
      for (const grapheme of graphemes) {
        const graphemeMetrics = getSegmentMetrics(grapheme, cache);
        const currentWidth = getCorrectedSegmentWidth(grapheme, graphemeMetrics, emojiCorrection);
        if (previousGrapheme === null) {
          advances2.push(currentWidth);
        } else {
          const pair = previousGrapheme + grapheme;
          const pairMetrics = getSegmentMetrics(pair, cache);
          advances2.push(getCorrectedSegmentWidth(pair, pairMetrics, emojiCorrection) - previousWidth);
        }
        previousGrapheme = grapheme;
        previousWidth = currentWidth;
      }
      metrics.breakableFitAdvances = advances2;
      return metrics.breakableFitAdvances;
    }
    const advances = [];
    let prefix = "";
    let prefixWidth = 0;
    for (const grapheme of graphemes) {
      prefix += grapheme;
      const prefixMetrics = getSegmentMetrics(prefix, cache);
      const nextPrefixWidth = getCorrectedSegmentWidth(prefix, prefixMetrics, emojiCorrection);
      advances.push(nextPrefixWidth - prefixWidth);
      prefixWidth = nextPrefixWidth;
    }
    metrics.breakableFitAdvances = advances;
    return metrics.breakableFitAdvances;
  }
  function getFontMeasurementState(font, needsEmojiCorrection) {
    const ctx = getMeasureContext();
    ctx.font = font;
    const cache = getSegmentMetricCache(font);
    const fontSize = parseFontSize(font);
    const emojiCorrection = needsEmojiCorrection ? getEmojiCorrection(font, fontSize) : 0;
    return { cache, fontSize, emojiCorrection };
  }
  function clearMeasurementCaches() {
    segmentMetricCaches.clear();
    emojiCorrectionCache.clear();
    sharedGraphemeSegmenter = null;
  }
  var measureContext, segmentMetricCaches, cachedEngineProfile, MAX_PREFIX_FIT_GRAPHEMES, emojiPresentationRe, maybeEmojiRe, sharedGraphemeSegmenter, emojiCorrectionCache;
  var init_measurement = __esm({
    "src/measurement.ts"() {
      init_analysis();
      measureContext = null;
      segmentMetricCaches = /* @__PURE__ */ new Map();
      cachedEngineProfile = null;
      MAX_PREFIX_FIT_GRAPHEMES = 96;
      emojiPresentationRe = /\p{Emoji_Presentation}/u;
      maybeEmojiRe = /[\p{Emoji_Presentation}\p{Extended_Pictographic}\p{Regional_Indicator}\uFE0F\u20E3]/u;
      sharedGraphemeSegmenter = null;
      emojiCorrectionCache = /* @__PURE__ */ new Map();
    }
  });

  // src/line-break.ts
  var line_break_exports = {};
  __export(line_break_exports, {
    countPreparedLines: () => countPreparedLines,
    layoutNextLineRange: () => layoutNextLineRange,
    measurePreparedLineGeometry: () => measurePreparedLineGeometry,
    normalizeLineStart: () => normalizeLineStart,
    stepPreparedLineGeometry: () => stepPreparedLineGeometry,
    walkPreparedLines: () => walkPreparedLines
  });
  function normalizeSimpleLineStartSegmentIndex(prepared, segmentIndex) {
    while (segmentIndex < prepared.widths.length) {
      const kind = prepared.kinds[segmentIndex];
      if (kind !== "space" && kind !== "zero-width-break" && kind !== "soft-hyphen") break;
      segmentIndex++;
    }
    return segmentIndex;
  }
  function getTabAdvance(lineWidth, tabStopAdvance) {
    if (tabStopAdvance <= 0) return 0;
    const remainder = lineWidth % tabStopAdvance;
    if (Math.abs(remainder) <= 1e-6) return tabStopAdvance;
    return tabStopAdvance - remainder;
  }
  function fitSoftHyphenBreak(graphemeFitAdvances, initialWidth, maxWidth, lineFitEpsilon, discretionaryHyphenWidth) {
    let fitCount = 0;
    let fittedWidth = initialWidth;
    while (fitCount < graphemeFitAdvances.length) {
      const nextWidth = fittedWidth + graphemeFitAdvances[fitCount];
      const nextLineWidth = fitCount + 1 < graphemeFitAdvances.length ? nextWidth + discretionaryHyphenWidth : nextWidth;
      if (nextLineWidth > maxWidth + lineFitEpsilon) break;
      fittedWidth = nextWidth;
      fitCount++;
    }
    return { fitCount, fittedWidth };
  }
  function findChunkIndexForStart(prepared, segmentIndex) {
    let lo = 0;
    let hi = prepared.chunks.length;
    while (lo < hi) {
      const mid = Math.floor((lo + hi) / 2);
      if (segmentIndex < prepared.chunks[mid].consumedEndSegmentIndex) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo < prepared.chunks.length ? lo : -1;
  }
  function normalizeLineStartInChunk(prepared, chunkIndex, cursor) {
    let segmentIndex = cursor.segmentIndex;
    if (cursor.graphemeIndex > 0) return chunkIndex;
    const chunk = prepared.chunks[chunkIndex];
    if (chunk.startSegmentIndex === chunk.endSegmentIndex && segmentIndex === chunk.startSegmentIndex) {
      cursor.segmentIndex = segmentIndex;
      cursor.graphemeIndex = 0;
      return chunkIndex;
    }
    if (segmentIndex < chunk.startSegmentIndex) segmentIndex = chunk.startSegmentIndex;
    while (segmentIndex < chunk.endSegmentIndex) {
      const kind = prepared.kinds[segmentIndex];
      if (kind !== "space" && kind !== "zero-width-break" && kind !== "soft-hyphen") {
        cursor.segmentIndex = segmentIndex;
        cursor.graphemeIndex = 0;
        return chunkIndex;
      }
      segmentIndex++;
    }
    if (chunk.consumedEndSegmentIndex >= prepared.widths.length) return -1;
    cursor.segmentIndex = chunk.consumedEndSegmentIndex;
    cursor.graphemeIndex = 0;
    return chunkIndex + 1;
  }
  function normalizeLineStartChunkIndex(prepared, cursor) {
    if (cursor.segmentIndex >= prepared.widths.length) return -1;
    const chunkIndex = findChunkIndexForStart(prepared, cursor.segmentIndex);
    if (chunkIndex < 0) return -1;
    return normalizeLineStartInChunk(prepared, chunkIndex, cursor);
  }
  function normalizeLineStartChunkIndexFromHint(prepared, chunkIndex, cursor) {
    if (cursor.segmentIndex >= prepared.widths.length) return -1;
    let nextChunkIndex = chunkIndex;
    while (nextChunkIndex < prepared.chunks.length && cursor.segmentIndex >= prepared.chunks[nextChunkIndex].consumedEndSegmentIndex) {
      nextChunkIndex++;
    }
    if (nextChunkIndex >= prepared.chunks.length) return -1;
    return normalizeLineStartInChunk(prepared, nextChunkIndex, cursor);
  }
  function normalizeLineStart(prepared, start) {
    const cursor = {
      segmentIndex: start.segmentIndex,
      graphemeIndex: start.graphemeIndex
    };
    const chunkIndex = normalizeLineStartChunkIndex(prepared, cursor);
    return chunkIndex < 0 ? null : cursor;
  }
  function countPreparedLines(prepared, maxWidth) {
    if (prepared.simpleLineWalkFastPath) {
      return walkPreparedLinesSimple(prepared, maxWidth);
    }
    return walkPreparedLines(prepared, maxWidth);
  }
  function walkPreparedLinesSimple(prepared, maxWidth, onLine) {
    const { widths, kinds, breakableFitAdvances } = prepared;
    if (widths.length === 0) return 0;
    const engineProfile = getEngineProfile();
    const lineFitEpsilon = engineProfile.lineFitEpsilon;
    const fitLimit = maxWidth + lineFitEpsilon;
    let lineCount = 0;
    let lineW = 0;
    let hasContent = false;
    let lineStartSegmentIndex = 0;
    let lineStartGraphemeIndex = 0;
    let lineEndSegmentIndex = 0;
    let lineEndGraphemeIndex = 0;
    let pendingBreakSegmentIndex = -1;
    let pendingBreakPaintWidth = 0;
    function clearPendingBreak() {
      pendingBreakSegmentIndex = -1;
      pendingBreakPaintWidth = 0;
    }
    function emitCurrentLine(endSegmentIndex = lineEndSegmentIndex, endGraphemeIndex = lineEndGraphemeIndex, width = lineW) {
      lineCount++;
      onLine?.({
        startSegmentIndex: lineStartSegmentIndex,
        startGraphemeIndex: lineStartGraphemeIndex,
        endSegmentIndex,
        endGraphemeIndex,
        width
      });
      lineW = 0;
      hasContent = false;
      clearPendingBreak();
    }
    function startLineAtSegment(segmentIndex, width) {
      hasContent = true;
      lineStartSegmentIndex = segmentIndex;
      lineStartGraphemeIndex = 0;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
      lineW = width;
    }
    function startLineAtGrapheme(segmentIndex, graphemeIndex, width) {
      hasContent = true;
      lineStartSegmentIndex = segmentIndex;
      lineStartGraphemeIndex = graphemeIndex;
      lineEndSegmentIndex = segmentIndex;
      lineEndGraphemeIndex = graphemeIndex + 1;
      lineW = width;
    }
    function appendWholeSegment(segmentIndex, width) {
      if (!hasContent) {
        startLineAtSegment(segmentIndex, width);
        return;
      }
      lineW += width;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
    }
    function appendBreakableSegmentFrom(segmentIndex, startGraphemeIndex) {
      const fitAdvances = breakableFitAdvances[segmentIndex];
      for (let g = startGraphemeIndex; g < fitAdvances.length; g++) {
        const gw = fitAdvances[g];
        if (!hasContent) {
          startLineAtGrapheme(segmentIndex, g, gw);
        } else if (lineW + gw > fitLimit) {
          emitCurrentLine();
          startLineAtGrapheme(segmentIndex, g, gw);
        } else {
          lineW += gw;
          lineEndSegmentIndex = segmentIndex;
          lineEndGraphemeIndex = g + 1;
        }
      }
      if (hasContent && lineEndSegmentIndex === segmentIndex && lineEndGraphemeIndex === fitAdvances.length) {
        lineEndSegmentIndex = segmentIndex + 1;
        lineEndGraphemeIndex = 0;
      }
    }
    let i = 0;
    while (i < widths.length) {
      if (!hasContent) {
        i = normalizeSimpleLineStartSegmentIndex(prepared, i);
        if (i >= widths.length) break;
      }
      const w = widths[i];
      const kind = kinds[i];
      const breakAfter = kind === "space" || kind === "preserved-space" || kind === "tab" || kind === "zero-width-break" || kind === "soft-hyphen";
      if (!hasContent) {
        if (w > maxWidth && breakableFitAdvances[i] !== null) {
          appendBreakableSegmentFrom(i, 0);
        } else {
          startLineAtSegment(i, w);
        }
        if (breakAfter) {
          pendingBreakSegmentIndex = i + 1;
          pendingBreakPaintWidth = lineW - w;
        }
        i++;
        continue;
      }
      const newW = lineW + w;
      if (newW > fitLimit) {
        if (breakAfter) {
          appendWholeSegment(i, w);
          emitCurrentLine(i + 1, 0, lineW - w);
          i++;
          continue;
        }
        if (pendingBreakSegmentIndex >= 0) {
          if (lineEndSegmentIndex > pendingBreakSegmentIndex || lineEndSegmentIndex === pendingBreakSegmentIndex && lineEndGraphemeIndex > 0) {
            emitCurrentLine();
            continue;
          }
          emitCurrentLine(pendingBreakSegmentIndex, 0, pendingBreakPaintWidth);
          continue;
        }
        if (w > maxWidth && breakableFitAdvances[i] !== null) {
          emitCurrentLine();
          appendBreakableSegmentFrom(i, 0);
          i++;
          continue;
        }
        emitCurrentLine();
        continue;
      }
      appendWholeSegment(i, w);
      if (breakAfter) {
        pendingBreakSegmentIndex = i + 1;
        pendingBreakPaintWidth = lineW - w;
      }
      i++;
    }
    if (hasContent) emitCurrentLine();
    return lineCount;
  }
  function walkPreparedLines(prepared, maxWidth, onLine) {
    if (prepared.simpleLineWalkFastPath) {
      return walkPreparedLinesSimple(prepared, maxWidth, onLine);
    }
    const {
      widths,
      lineEndFitAdvances,
      lineEndPaintAdvances,
      kinds,
      breakableFitAdvances,
      discretionaryHyphenWidth,
      tabStopAdvance,
      chunks
    } = prepared;
    if (widths.length === 0 || chunks.length === 0) return 0;
    const engineProfile = getEngineProfile();
    const lineFitEpsilon = engineProfile.lineFitEpsilon;
    const fitLimit = maxWidth + lineFitEpsilon;
    let lineCount = 0;
    let lineW = 0;
    let hasContent = false;
    let lineStartSegmentIndex = 0;
    let lineStartGraphemeIndex = 0;
    let lineEndSegmentIndex = 0;
    let lineEndGraphemeIndex = 0;
    let pendingBreakSegmentIndex = -1;
    let pendingBreakFitWidth = 0;
    let pendingBreakPaintWidth = 0;
    let pendingBreakKind = null;
    function clearPendingBreak() {
      pendingBreakSegmentIndex = -1;
      pendingBreakFitWidth = 0;
      pendingBreakPaintWidth = 0;
      pendingBreakKind = null;
    }
    function emitCurrentLine(endSegmentIndex = lineEndSegmentIndex, endGraphemeIndex = lineEndGraphemeIndex, width = lineW) {
      lineCount++;
      onLine?.({
        startSegmentIndex: lineStartSegmentIndex,
        startGraphemeIndex: lineStartGraphemeIndex,
        endSegmentIndex,
        endGraphemeIndex,
        width
      });
      lineW = 0;
      hasContent = false;
      clearPendingBreak();
    }
    function startLineAtSegment(segmentIndex, width) {
      hasContent = true;
      lineStartSegmentIndex = segmentIndex;
      lineStartGraphemeIndex = 0;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
      lineW = width;
    }
    function startLineAtGrapheme(segmentIndex, graphemeIndex, width) {
      hasContent = true;
      lineStartSegmentIndex = segmentIndex;
      lineStartGraphemeIndex = graphemeIndex;
      lineEndSegmentIndex = segmentIndex;
      lineEndGraphemeIndex = graphemeIndex + 1;
      lineW = width;
    }
    function appendWholeSegment(segmentIndex, width) {
      if (!hasContent) {
        startLineAtSegment(segmentIndex, width);
        return;
      }
      lineW += width;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
    }
    function updatePendingBreakForWholeSegment(kind, breakAfter, segmentIndex, segmentWidth) {
      if (!breakAfter) return;
      const fitAdvance = kind === "tab" ? 0 : lineEndFitAdvances[segmentIndex];
      const paintAdvance = kind === "tab" ? segmentWidth : lineEndPaintAdvances[segmentIndex];
      pendingBreakSegmentIndex = segmentIndex + 1;
      pendingBreakFitWidth = lineW - segmentWidth + fitAdvance;
      pendingBreakPaintWidth = lineW - segmentWidth + paintAdvance;
      pendingBreakKind = kind;
    }
    function appendBreakableSegmentFrom(segmentIndex, startGraphemeIndex) {
      const fitAdvances = breakableFitAdvances[segmentIndex];
      for (let g = startGraphemeIndex; g < fitAdvances.length; g++) {
        const gw = fitAdvances[g];
        if (!hasContent) {
          startLineAtGrapheme(segmentIndex, g, gw);
        } else if (lineW + gw > fitLimit) {
          emitCurrentLine();
          startLineAtGrapheme(segmentIndex, g, gw);
        } else {
          lineW += gw;
          lineEndSegmentIndex = segmentIndex;
          lineEndGraphemeIndex = g + 1;
        }
      }
      if (hasContent && lineEndSegmentIndex === segmentIndex && lineEndGraphemeIndex === fitAdvances.length) {
        lineEndSegmentIndex = segmentIndex + 1;
        lineEndGraphemeIndex = 0;
      }
    }
    function continueSoftHyphenBreakableSegment(segmentIndex) {
      if (pendingBreakKind !== "soft-hyphen") return false;
      const fitWidths = breakableFitAdvances[segmentIndex];
      if (fitWidths == null) return false;
      const { fitCount, fittedWidth } = fitSoftHyphenBreak(
        fitWidths,
        lineW,
        maxWidth,
        lineFitEpsilon,
        discretionaryHyphenWidth
      );
      if (fitCount === 0) return false;
      lineW = fittedWidth;
      lineEndSegmentIndex = segmentIndex;
      lineEndGraphemeIndex = fitCount;
      clearPendingBreak();
      if (fitCount === fitWidths.length) {
        lineEndSegmentIndex = segmentIndex + 1;
        lineEndGraphemeIndex = 0;
        return true;
      }
      emitCurrentLine(
        segmentIndex,
        fitCount,
        fittedWidth + discretionaryHyphenWidth
      );
      appendBreakableSegmentFrom(segmentIndex, fitCount);
      return true;
    }
    function emitEmptyChunk(chunk) {
      lineCount++;
      onLine?.({
        startSegmentIndex: chunk.startSegmentIndex,
        startGraphemeIndex: 0,
        endSegmentIndex: chunk.consumedEndSegmentIndex,
        endGraphemeIndex: 0,
        width: 0
      });
      clearPendingBreak();
    }
    for (let chunkIndex = 0; chunkIndex < chunks.length; chunkIndex++) {
      const chunk = chunks[chunkIndex];
      if (chunk.startSegmentIndex === chunk.endSegmentIndex) {
        emitEmptyChunk(chunk);
        continue;
      }
      hasContent = false;
      lineW = 0;
      lineStartSegmentIndex = chunk.startSegmentIndex;
      lineStartGraphemeIndex = 0;
      lineEndSegmentIndex = chunk.startSegmentIndex;
      lineEndGraphemeIndex = 0;
      clearPendingBreak();
      let i = chunk.startSegmentIndex;
      while (i < chunk.endSegmentIndex) {
        const kind = kinds[i];
        const breakAfter = kind === "space" || kind === "preserved-space" || kind === "tab" || kind === "zero-width-break" || kind === "soft-hyphen";
        const w = kind === "tab" ? getTabAdvance(lineW, tabStopAdvance) : widths[i];
        if (kind === "soft-hyphen") {
          if (hasContent) {
            lineEndSegmentIndex = i + 1;
            lineEndGraphemeIndex = 0;
            pendingBreakSegmentIndex = i + 1;
            pendingBreakFitWidth = lineW + discretionaryHyphenWidth;
            pendingBreakPaintWidth = lineW + discretionaryHyphenWidth;
            pendingBreakKind = kind;
          }
          i++;
          continue;
        }
        if (!hasContent) {
          if (w > maxWidth && breakableFitAdvances[i] !== null) {
            appendBreakableSegmentFrom(i, 0);
          } else {
            startLineAtSegment(i, w);
          }
          updatePendingBreakForWholeSegment(kind, breakAfter, i, w);
          i++;
          continue;
        }
        const newW = lineW + w;
        if (newW > fitLimit) {
          const currentBreakFitWidth = lineW + (kind === "tab" ? 0 : lineEndFitAdvances[i]);
          const currentBreakPaintWidth = lineW + (kind === "tab" ? w : lineEndPaintAdvances[i]);
          if (pendingBreakKind === "soft-hyphen" && engineProfile.preferEarlySoftHyphenBreak && pendingBreakFitWidth <= fitLimit) {
            emitCurrentLine(pendingBreakSegmentIndex, 0, pendingBreakPaintWidth);
            continue;
          }
          if (pendingBreakKind === "soft-hyphen" && continueSoftHyphenBreakableSegment(i)) {
            i++;
            continue;
          }
          if (breakAfter && currentBreakFitWidth <= fitLimit) {
            appendWholeSegment(i, w);
            emitCurrentLine(i + 1, 0, currentBreakPaintWidth);
            i++;
            continue;
          }
          if (pendingBreakSegmentIndex >= 0 && pendingBreakFitWidth <= fitLimit) {
            if (lineEndSegmentIndex > pendingBreakSegmentIndex || lineEndSegmentIndex === pendingBreakSegmentIndex && lineEndGraphemeIndex > 0) {
              emitCurrentLine();
              continue;
            }
            const nextSegmentIndex = pendingBreakSegmentIndex;
            emitCurrentLine(nextSegmentIndex, 0, pendingBreakPaintWidth);
            i = nextSegmentIndex;
            continue;
          }
          if (w > maxWidth && breakableFitAdvances[i] !== null) {
            emitCurrentLine();
            appendBreakableSegmentFrom(i, 0);
            i++;
            continue;
          }
          emitCurrentLine();
          continue;
        }
        appendWholeSegment(i, w);
        updatePendingBreakForWholeSegment(kind, breakAfter, i, w);
        i++;
      }
      if (hasContent) {
        const finalPaintWidth = pendingBreakSegmentIndex === chunk.consumedEndSegmentIndex ? pendingBreakPaintWidth : lineW;
        emitCurrentLine(chunk.consumedEndSegmentIndex, 0, finalPaintWidth);
      }
    }
    return lineCount;
  }
  function stepPreparedChunkLineGeometry(prepared, cursor, chunkIndex, maxWidth) {
    const chunk = prepared.chunks[chunkIndex];
    if (chunk.startSegmentIndex === chunk.endSegmentIndex) {
      cursor.segmentIndex = chunk.consumedEndSegmentIndex;
      cursor.graphemeIndex = 0;
      return 0;
    }
    const {
      widths,
      lineEndFitAdvances,
      lineEndPaintAdvances,
      kinds,
      breakableFitAdvances,
      discretionaryHyphenWidth,
      tabStopAdvance
    } = prepared;
    const engineProfile = getEngineProfile();
    const lineFitEpsilon = engineProfile.lineFitEpsilon;
    const fitLimit = maxWidth + lineFitEpsilon;
    let lineW = 0;
    let hasContent = false;
    let lineEndSegmentIndex = cursor.segmentIndex;
    let lineEndGraphemeIndex = cursor.graphemeIndex;
    let pendingBreakSegmentIndex = -1;
    let pendingBreakFitWidth = 0;
    let pendingBreakPaintWidth = 0;
    let pendingBreakKind = null;
    function clearPendingBreak() {
      pendingBreakSegmentIndex = -1;
      pendingBreakFitWidth = 0;
      pendingBreakPaintWidth = 0;
      pendingBreakKind = null;
    }
    function finishLine(endSegmentIndex = lineEndSegmentIndex, endGraphemeIndex = lineEndGraphemeIndex, width = lineW) {
      if (!hasContent) return null;
      cursor.segmentIndex = endSegmentIndex;
      cursor.graphemeIndex = endGraphemeIndex;
      return width;
    }
    function startLineAtSegment(segmentIndex, width) {
      hasContent = true;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
      lineW = width;
    }
    function startLineAtGrapheme(segmentIndex, graphemeIndex, width) {
      hasContent = true;
      lineEndSegmentIndex = segmentIndex;
      lineEndGraphemeIndex = graphemeIndex + 1;
      lineW = width;
    }
    function appendWholeSegment(segmentIndex, width) {
      if (!hasContent) {
        startLineAtSegment(segmentIndex, width);
        return;
      }
      lineW += width;
      lineEndSegmentIndex = segmentIndex + 1;
      lineEndGraphemeIndex = 0;
    }
    function updatePendingBreakForWholeSegment(kind, breakAfter, segmentIndex, segmentWidth) {
      if (!breakAfter) return;
      const fitAdvance = kind === "tab" ? 0 : lineEndFitAdvances[segmentIndex];
      const paintAdvance = kind === "tab" ? segmentWidth : lineEndPaintAdvances[segmentIndex];
      pendingBreakSegmentIndex = segmentIndex + 1;
      pendingBreakFitWidth = lineW - segmentWidth + fitAdvance;
      pendingBreakPaintWidth = lineW - segmentWidth + paintAdvance;
      pendingBreakKind = kind;
    }
    function appendBreakableSegmentFrom(segmentIndex, startGraphemeIndex) {
      const fitAdvances = breakableFitAdvances[segmentIndex];
      for (let g = startGraphemeIndex; g < fitAdvances.length; g++) {
        const gw = fitAdvances[g];
        if (!hasContent) {
          startLineAtGrapheme(segmentIndex, g, gw);
        } else {
          if (lineW + gw > fitLimit) {
            return finishLine();
          }
          lineW += gw;
          lineEndSegmentIndex = segmentIndex;
          lineEndGraphemeIndex = g + 1;
        }
      }
      if (hasContent && lineEndSegmentIndex === segmentIndex && lineEndGraphemeIndex === fitAdvances.length) {
        lineEndSegmentIndex = segmentIndex + 1;
        lineEndGraphemeIndex = 0;
      }
      return null;
    }
    function maybeFinishAtSoftHyphen(segmentIndex) {
      if (pendingBreakKind !== "soft-hyphen" || pendingBreakSegmentIndex < 0) return null;
      const fitWidths = breakableFitAdvances[segmentIndex] ?? null;
      if (fitWidths !== null) {
        const { fitCount, fittedWidth } = fitSoftHyphenBreak(
          fitWidths,
          lineW,
          maxWidth,
          lineFitEpsilon,
          discretionaryHyphenWidth
        );
        if (fitCount === fitWidths.length) {
          lineW = fittedWidth;
          lineEndSegmentIndex = segmentIndex + 1;
          lineEndGraphemeIndex = 0;
          clearPendingBreak();
          return null;
        }
        if (fitCount > 0) {
          return finishLine(
            segmentIndex,
            fitCount,
            fittedWidth + discretionaryHyphenWidth
          );
        }
      }
      if (pendingBreakFitWidth <= fitLimit) {
        return finishLine(pendingBreakSegmentIndex, 0, pendingBreakPaintWidth);
      }
      return null;
    }
    for (let i = cursor.segmentIndex; i < chunk.endSegmentIndex; i++) {
      const kind = kinds[i];
      const breakAfter = kind === "space" || kind === "preserved-space" || kind === "tab" || kind === "zero-width-break" || kind === "soft-hyphen";
      const startGraphemeIndex = i === cursor.segmentIndex ? cursor.graphemeIndex : 0;
      const w = kind === "tab" ? getTabAdvance(lineW, tabStopAdvance) : widths[i];
      if (kind === "soft-hyphen" && startGraphemeIndex === 0) {
        if (hasContent) {
          lineEndSegmentIndex = i + 1;
          lineEndGraphemeIndex = 0;
          pendingBreakSegmentIndex = i + 1;
          pendingBreakFitWidth = lineW + discretionaryHyphenWidth;
          pendingBreakPaintWidth = lineW + discretionaryHyphenWidth;
          pendingBreakKind = kind;
        }
        continue;
      }
      if (!hasContent) {
        if (startGraphemeIndex > 0) {
          const line = appendBreakableSegmentFrom(i, startGraphemeIndex);
          if (line !== null) return line;
        } else if (w > maxWidth && breakableFitAdvances[i] !== null) {
          const line = appendBreakableSegmentFrom(i, 0);
          if (line !== null) return line;
        } else {
          startLineAtSegment(i, w);
        }
        updatePendingBreakForWholeSegment(kind, breakAfter, i, w);
        continue;
      }
      const newW = lineW + w;
      if (newW > fitLimit) {
        const currentBreakFitWidth = lineW + (kind === "tab" ? 0 : lineEndFitAdvances[i]);
        const currentBreakPaintWidth = lineW + (kind === "tab" ? w : lineEndPaintAdvances[i]);
        if (pendingBreakKind === "soft-hyphen" && engineProfile.preferEarlySoftHyphenBreak && pendingBreakFitWidth <= fitLimit) {
          return finishLine(pendingBreakSegmentIndex, 0, pendingBreakPaintWidth);
        }
        const softBreakLine = maybeFinishAtSoftHyphen(i);
        if (softBreakLine !== null) return softBreakLine;
        if (breakAfter && currentBreakFitWidth <= fitLimit) {
          appendWholeSegment(i, w);
          return finishLine(i + 1, 0, currentBreakPaintWidth);
        }
        if (pendingBreakSegmentIndex >= 0 && pendingBreakFitWidth <= fitLimit) {
          if (lineEndSegmentIndex > pendingBreakSegmentIndex || lineEndSegmentIndex === pendingBreakSegmentIndex && lineEndGraphemeIndex > 0) {
            return finishLine();
          }
          return finishLine(pendingBreakSegmentIndex, 0, pendingBreakPaintWidth);
        }
        if (w > maxWidth && breakableFitAdvances[i] !== null) {
          const currentLine = finishLine();
          if (currentLine !== null) return currentLine;
          const line = appendBreakableSegmentFrom(i, 0);
          if (line !== null) return line;
        }
        return finishLine();
      }
      appendWholeSegment(i, w);
      updatePendingBreakForWholeSegment(kind, breakAfter, i, w);
    }
    if (pendingBreakSegmentIndex === chunk.consumedEndSegmentIndex && lineEndGraphemeIndex === 0) {
      return finishLine(chunk.consumedEndSegmentIndex, 0, pendingBreakPaintWidth);
    }
    return finishLine(chunk.consumedEndSegmentIndex, 0, lineW);
  }
  function stepPreparedSimpleLineGeometry(prepared, cursor, maxWidth) {
    const { widths, kinds, breakableFitAdvances } = prepared;
    const engineProfile = getEngineProfile();
    const lineFitEpsilon = engineProfile.lineFitEpsilon;
    const fitLimit = maxWidth + lineFitEpsilon;
    let lineW = 0;
    let hasContent = false;
    let lineEndSegmentIndex = cursor.segmentIndex;
    let lineEndGraphemeIndex = cursor.graphemeIndex;
    let pendingBreakSegmentIndex = -1;
    let pendingBreakPaintWidth = 0;
    for (let i = cursor.segmentIndex; i < widths.length; i++) {
      const w = widths[i];
      const kind = kinds[i];
      const breakAfter = kind === "space" || kind === "preserved-space" || kind === "tab" || kind === "zero-width-break" || kind === "soft-hyphen";
      const startGraphemeIndex = i === cursor.segmentIndex ? cursor.graphemeIndex : 0;
      const breakableFitAdvance = breakableFitAdvances[i];
      if (!hasContent) {
        if (startGraphemeIndex > 0 || w > maxWidth && breakableFitAdvance !== null) {
          const fitAdvances = breakableFitAdvance;
          const firstGraphemeWidth = fitAdvances[startGraphemeIndex];
          hasContent = true;
          lineW = firstGraphemeWidth;
          lineEndSegmentIndex = i;
          lineEndGraphemeIndex = startGraphemeIndex + 1;
          for (let g = startGraphemeIndex + 1; g < fitAdvances.length; g++) {
            const gw = fitAdvances[g];
            if (lineW + gw > fitLimit) {
              cursor.segmentIndex = lineEndSegmentIndex;
              cursor.graphemeIndex = lineEndGraphemeIndex;
              return lineW;
            }
            lineW += gw;
            lineEndSegmentIndex = i;
            lineEndGraphemeIndex = g + 1;
          }
          if (lineEndSegmentIndex === i && lineEndGraphemeIndex === fitAdvances.length) {
            lineEndSegmentIndex = i + 1;
            lineEndGraphemeIndex = 0;
          }
        } else {
          hasContent = true;
          lineW = w;
          lineEndSegmentIndex = i + 1;
          lineEndGraphemeIndex = 0;
        }
        if (breakAfter) {
          pendingBreakSegmentIndex = i + 1;
          pendingBreakPaintWidth = lineW - w;
        }
        continue;
      }
      if (lineW + w > fitLimit) {
        if (breakAfter) {
          cursor.segmentIndex = i + 1;
          cursor.graphemeIndex = 0;
          return lineW;
        }
        if (pendingBreakSegmentIndex >= 0) {
          if (lineEndSegmentIndex > pendingBreakSegmentIndex || lineEndSegmentIndex === pendingBreakSegmentIndex && lineEndGraphemeIndex > 0) {
            cursor.segmentIndex = lineEndSegmentIndex;
            cursor.graphemeIndex = lineEndGraphemeIndex;
            return lineW;
          }
          cursor.segmentIndex = pendingBreakSegmentIndex;
          cursor.graphemeIndex = 0;
          return pendingBreakPaintWidth;
        }
        cursor.segmentIndex = lineEndSegmentIndex;
        cursor.graphemeIndex = lineEndGraphemeIndex;
        return lineW;
      }
      lineW += w;
      lineEndSegmentIndex = i + 1;
      lineEndGraphemeIndex = 0;
      if (breakAfter) {
        pendingBreakSegmentIndex = i + 1;
        pendingBreakPaintWidth = lineW - w;
      }
    }
    if (!hasContent) return null;
    cursor.segmentIndex = lineEndSegmentIndex;
    cursor.graphemeIndex = lineEndGraphemeIndex;
    return lineW;
  }
  function layoutNextLineRange(prepared, start, maxWidth) {
    const end = {
      segmentIndex: start.segmentIndex,
      graphemeIndex: start.graphemeIndex
    };
    const chunkIndex = normalizeLineStartChunkIndex(prepared, end);
    if (chunkIndex < 0) return null;
    const lineStartSegmentIndex = end.segmentIndex;
    const lineStartGraphemeIndex = end.graphemeIndex;
    const width = prepared.simpleLineWalkFastPath ? stepPreparedSimpleLineGeometry(prepared, end, maxWidth) : stepPreparedChunkLineGeometry(prepared, end, chunkIndex, maxWidth);
    if (width === null) return null;
    return {
      startSegmentIndex: lineStartSegmentIndex,
      startGraphemeIndex: lineStartGraphemeIndex,
      endSegmentIndex: end.segmentIndex,
      endGraphemeIndex: end.graphemeIndex,
      width
    };
  }
  function stepPreparedLineGeometry(prepared, cursor, maxWidth) {
    const chunkIndex = normalizeLineStartChunkIndex(prepared, cursor);
    if (chunkIndex < 0) return null;
    if (prepared.simpleLineWalkFastPath) {
      return stepPreparedSimpleLineGeometry(prepared, cursor, maxWidth);
    }
    return stepPreparedChunkLineGeometry(prepared, cursor, chunkIndex, maxWidth);
  }
  function measurePreparedLineGeometry(prepared, maxWidth) {
    if (prepared.widths.length === 0) {
      return {
        lineCount: 0,
        maxLineWidth: 0
      };
    }
    const cursor = {
      segmentIndex: 0,
      graphemeIndex: 0
    };
    let lineCount = 0;
    let maxLineWidth = 0;
    if (!prepared.simpleLineWalkFastPath) {
      let chunkIndex = normalizeLineStartChunkIndex(prepared, cursor);
      while (chunkIndex >= 0) {
        const lineWidth = stepPreparedChunkLineGeometry(prepared, cursor, chunkIndex, maxWidth);
        if (lineWidth === null) {
          return {
            lineCount,
            maxLineWidth
          };
        }
        lineCount++;
        if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;
        chunkIndex = normalizeLineStartChunkIndexFromHint(prepared, chunkIndex, cursor);
      }
      return {
        lineCount,
        maxLineWidth
      };
    }
    while (true) {
      const lineWidth = stepPreparedLineGeometry(prepared, cursor, maxWidth);
      if (lineWidth === null) {
        return {
          lineCount,
          maxLineWidth
        };
      }
      lineCount++;
      if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;
    }
  }
  var init_line_break = __esm({
    "src/line-break.ts"() {
      init_measurement();
    }
  });

  // src/layout.ts
  var layout_exports = {};
  __export(layout_exports, {
    clearCache: () => clearCache,
    layout: () => layout,
    layoutNextLine: () => layoutNextLine,
    layoutNextLineRange: () => layoutNextLineRange2,
    layoutWithLines: () => layoutWithLines,
    materializeLineRange: () => materializeLineRange,
    measureLineStats: () => measureLineStats,
    measureNaturalWidth: () => measureNaturalWidth,
    prepare: () => prepare,
    prepareWithSegments: () => prepareWithSegments,
    setLocale: () => setLocale,
    walkLineRanges: () => walkLineRanges
  });
  function getSharedGraphemeSegmenter2() {
    if (sharedGraphemeSegmenter2 === null) {
      sharedGraphemeSegmenter2 = new Intl.Segmenter(void 0, { granularity: "grapheme" });
    }
    return sharedGraphemeSegmenter2;
  }
  function createEmptyPrepared(includeSegments) {
    if (includeSegments) {
      return {
        widths: [],
        lineEndFitAdvances: [],
        lineEndPaintAdvances: [],
        kinds: [],
        simpleLineWalkFastPath: true,
        segLevels: null,
        breakableFitAdvances: [],
        discretionaryHyphenWidth: 0,
        tabStopAdvance: 0,
        chunks: [],
        segments: []
      };
    }
    return {
      widths: [],
      lineEndFitAdvances: [],
      lineEndPaintAdvances: [],
      kinds: [],
      simpleLineWalkFastPath: true,
      segLevels: null,
      breakableFitAdvances: [],
      discretionaryHyphenWidth: 0,
      tabStopAdvance: 0,
      chunks: []
    };
  }
  function buildBaseCjkUnits(segText, engineProfile) {
    const units = [];
    let unitParts = [];
    let unitStart = 0;
    let unitContainsCJK = false;
    let unitEndsWithClosingQuote = false;
    let unitIsSingleKinsokuEnd = false;
    function pushUnit() {
      if (unitParts.length === 0) return;
      units.push({
        text: unitParts.length === 1 ? unitParts[0] : unitParts.join(""),
        start: unitStart
      });
      unitParts = [];
      unitContainsCJK = false;
      unitEndsWithClosingQuote = false;
      unitIsSingleKinsokuEnd = false;
    }
    function startUnit(grapheme, start, graphemeContainsCJK) {
      unitParts = [grapheme];
      unitStart = start;
      unitContainsCJK = graphemeContainsCJK;
      unitEndsWithClosingQuote = endsWithClosingQuote(grapheme);
      unitIsSingleKinsokuEnd = kinsokuEnd.has(grapheme);
    }
    function appendToUnit(grapheme, graphemeContainsCJK) {
      unitParts.push(grapheme);
      unitContainsCJK = unitContainsCJK || graphemeContainsCJK;
      const graphemeEndsWithClosingQuote = endsWithClosingQuote(grapheme);
      if (grapheme.length === 1 && leftStickyPunctuation.has(grapheme)) {
        unitEndsWithClosingQuote = unitEndsWithClosingQuote || graphemeEndsWithClosingQuote;
      } else {
        unitEndsWithClosingQuote = graphemeEndsWithClosingQuote;
      }
      unitIsSingleKinsokuEnd = false;
    }
    for (const gs of getSharedGraphemeSegmenter2().segment(segText)) {
      const grapheme = gs.segment;
      const graphemeContainsCJK = isCJK(grapheme);
      if (unitParts.length === 0) {
        startUnit(grapheme, gs.index, graphemeContainsCJK);
        continue;
      }
      if (unitIsSingleKinsokuEnd || kinsokuStart.has(grapheme) || leftStickyPunctuation.has(grapheme) || engineProfile.carryCJKAfterClosingQuote && graphemeContainsCJK && unitEndsWithClosingQuote) {
        appendToUnit(grapheme, graphemeContainsCJK);
        continue;
      }
      if (!unitContainsCJK && !graphemeContainsCJK) {
        appendToUnit(grapheme, graphemeContainsCJK);
        continue;
      }
      pushUnit();
      startUnit(grapheme, gs.index, graphemeContainsCJK);
    }
    pushUnit();
    return units;
  }
  function mergeKeepAllTextUnits(units) {
    if (units.length <= 1) return units;
    const merged = [];
    let currentTextParts = [units[0].text];
    let currentStart = units[0].start;
    let currentContainsCJK = isCJK(units[0].text);
    let currentCanContinue = canContinueKeepAllTextRun(units[0].text);
    function flushCurrent() {
      merged.push({
        text: currentTextParts.length === 1 ? currentTextParts[0] : currentTextParts.join(""),
        start: currentStart
      });
    }
    for (let i = 1; i < units.length; i++) {
      const next = units[i];
      const nextContainsCJK = isCJK(next.text);
      const nextCanContinue = canContinueKeepAllTextRun(next.text);
      if (currentContainsCJK && currentCanContinue) {
        currentTextParts.push(next.text);
        currentContainsCJK = currentContainsCJK || nextContainsCJK;
        currentCanContinue = nextCanContinue;
        continue;
      }
      flushCurrent();
      currentTextParts = [next.text];
      currentStart = next.start;
      currentContainsCJK = nextContainsCJK;
      currentCanContinue = nextCanContinue;
    }
    flushCurrent();
    return merged;
  }
  function measureAnalysis(analysis, font, includeSegments, wordBreak) {
    const engineProfile = getEngineProfile();
    const { cache, emojiCorrection } = getFontMeasurementState(
      font,
      textMayContainEmoji(analysis.normalized)
    );
    const discretionaryHyphenWidth = getCorrectedSegmentWidth("-", getSegmentMetrics("-", cache), emojiCorrection);
    const spaceWidth = getCorrectedSegmentWidth(" ", getSegmentMetrics(" ", cache), emojiCorrection);
    const tabStopAdvance = spaceWidth * 8;
    if (analysis.len === 0) return createEmptyPrepared(includeSegments);
    const widths = [];
    const lineEndFitAdvances = [];
    const lineEndPaintAdvances = [];
    const kinds = [];
    let simpleLineWalkFastPath = analysis.chunks.length <= 1;
    const segStarts = includeSegments ? [] : null;
    const breakableFitAdvances = [];
    const segments = includeSegments ? [] : null;
    const preparedStartByAnalysisIndex = Array.from({ length: analysis.len });
    function pushMeasuredSegment(text, width, lineEndFitAdvance, lineEndPaintAdvance, kind, start, breakableFitAdvance) {
      if (kind !== "text" && kind !== "space" && kind !== "zero-width-break") {
        simpleLineWalkFastPath = false;
      }
      widths.push(width);
      lineEndFitAdvances.push(lineEndFitAdvance);
      lineEndPaintAdvances.push(lineEndPaintAdvance);
      kinds.push(kind);
      segStarts?.push(start);
      breakableFitAdvances.push(breakableFitAdvance);
      if (segments !== null) segments.push(text);
    }
    function pushMeasuredTextSegment(text, kind, start, wordLike, allowOverflowBreaks) {
      const textMetrics = getSegmentMetrics(text, cache);
      const width = getCorrectedSegmentWidth(text, textMetrics, emojiCorrection);
      const lineEndFitAdvance = kind === "space" || kind === "preserved-space" || kind === "zero-width-break" ? 0 : width;
      const lineEndPaintAdvance = kind === "space" || kind === "zero-width-break" ? 0 : width;
      if (allowOverflowBreaks && wordLike && text.length > 1) {
        let fitMode = "sum-graphemes";
        if (isNumericRunSegment(text)) {
          fitMode = "pair-context";
        } else if (engineProfile.preferPrefixWidthsForBreakableRuns) {
          fitMode = "segment-prefixes";
        }
        const fitAdvances = getSegmentBreakableFitAdvances(
          text,
          textMetrics,
          cache,
          emojiCorrection,
          fitMode
        );
        pushMeasuredSegment(
          text,
          width,
          lineEndFitAdvance,
          lineEndPaintAdvance,
          kind,
          start,
          fitAdvances
        );
        return;
      }
      pushMeasuredSegment(
        text,
        width,
        lineEndFitAdvance,
        lineEndPaintAdvance,
        kind,
        start,
        null
      );
    }
    for (let mi = 0; mi < analysis.len; mi++) {
      preparedStartByAnalysisIndex[mi] = widths.length;
      const segText = analysis.texts[mi];
      const segWordLike = analysis.isWordLike[mi];
      const segKind = analysis.kinds[mi];
      const segStart = analysis.starts[mi];
      if (segKind === "soft-hyphen") {
        pushMeasuredSegment(
          segText,
          0,
          discretionaryHyphenWidth,
          discretionaryHyphenWidth,
          segKind,
          segStart,
          null
        );
        continue;
      }
      if (segKind === "hard-break") {
        pushMeasuredSegment(segText, 0, 0, 0, segKind, segStart, null);
        continue;
      }
      if (segKind === "tab") {
        pushMeasuredSegment(segText, 0, 0, 0, segKind, segStart, null);
        continue;
      }
      const segMetrics = getSegmentMetrics(segText, cache);
      if (segKind === "text" && segMetrics.containsCJK) {
        const baseUnits = buildBaseCjkUnits(segText, engineProfile);
        const measuredUnits = wordBreak === "keep-all" ? mergeKeepAllTextUnits(baseUnits) : baseUnits;
        for (let i = 0; i < measuredUnits.length; i++) {
          const unit = measuredUnits[i];
          pushMeasuredTextSegment(
            unit.text,
            "text",
            segStart + unit.start,
            segWordLike,
            wordBreak === "keep-all" || !isCJK(unit.text)
          );
        }
        continue;
      }
      pushMeasuredTextSegment(segText, segKind, segStart, segWordLike, true);
    }
    const chunks = mapAnalysisChunksToPreparedChunks(analysis.chunks, preparedStartByAnalysisIndex, widths.length);
    const segLevels = segStarts === null ? null : computeSegmentLevels(analysis.normalized, segStarts);
    if (segments !== null) {
      return {
        widths,
        lineEndFitAdvances,
        lineEndPaintAdvances,
        kinds,
        simpleLineWalkFastPath,
        segLevels,
        breakableFitAdvances,
        discretionaryHyphenWidth,
        tabStopAdvance,
        chunks,
        segments
      };
    }
    return {
      widths,
      lineEndFitAdvances,
      lineEndPaintAdvances,
      kinds,
      simpleLineWalkFastPath,
      segLevels,
      breakableFitAdvances,
      discretionaryHyphenWidth,
      tabStopAdvance,
      chunks
    };
  }
  function mapAnalysisChunksToPreparedChunks(chunks, preparedStartByAnalysisIndex, preparedEndSegmentIndex) {
    const preparedChunks = [];
    for (let i = 0; i < chunks.length; i++) {
      const chunk = chunks[i];
      const startSegmentIndex = chunk.startSegmentIndex < preparedStartByAnalysisIndex.length ? preparedStartByAnalysisIndex[chunk.startSegmentIndex] : preparedEndSegmentIndex;
      const endSegmentIndex = chunk.endSegmentIndex < preparedStartByAnalysisIndex.length ? preparedStartByAnalysisIndex[chunk.endSegmentIndex] : preparedEndSegmentIndex;
      const consumedEndSegmentIndex = chunk.consumedEndSegmentIndex < preparedStartByAnalysisIndex.length ? preparedStartByAnalysisIndex[chunk.consumedEndSegmentIndex] : preparedEndSegmentIndex;
      preparedChunks.push({
        startSegmentIndex,
        endSegmentIndex,
        consumedEndSegmentIndex
      });
    }
    return preparedChunks;
  }
  function prepareInternal(text, font, includeSegments, options) {
    const wordBreak = options?.wordBreak ?? "normal";
    const analysis = analyzeText(text, getEngineProfile(), options?.whiteSpace, wordBreak);
    return measureAnalysis(analysis, font, includeSegments, wordBreak);
  }
  function prepare(text, font, options) {
    return prepareInternal(text, font, false, options);
  }
  function prepareWithSegments(text, font, options) {
    return prepareInternal(text, font, true, options);
  }
  function getInternalPrepared(prepared) {
    return prepared;
  }
  function layout(prepared, maxWidth, lineHeight) {
    const lineCount = countPreparedLines(getInternalPrepared(prepared), maxWidth);
    return { lineCount, height: lineCount * lineHeight };
  }
  function getSegmentGraphemes(segmentIndex, segments, cache) {
    let graphemes = cache.get(segmentIndex);
    if (graphemes !== void 0) return graphemes;
    graphemes = [];
    const graphemeSegmenter2 = getSharedGraphemeSegmenter2();
    for (const gs of graphemeSegmenter2.segment(segments[segmentIndex])) {
      graphemes.push(gs.segment);
    }
    cache.set(segmentIndex, graphemes);
    return graphemes;
  }
  function getLineTextCache(prepared) {
    let cache = sharedLineTextCaches.get(prepared);
    if (cache !== void 0) return cache;
    cache = /* @__PURE__ */ new Map();
    sharedLineTextCaches.set(prepared, cache);
    return cache;
  }
  function lineHasDiscretionaryHyphen(kinds, startSegmentIndex, startGraphemeIndex, endSegmentIndex) {
    return endSegmentIndex > 0 && kinds[endSegmentIndex - 1] === "soft-hyphen" && !(startSegmentIndex === endSegmentIndex && startGraphemeIndex > 0);
  }
  function buildLineTextFromRange(segments, kinds, cache, startSegmentIndex, startGraphemeIndex, endSegmentIndex, endGraphemeIndex) {
    let text = "";
    const endsWithDiscretionaryHyphen = lineHasDiscretionaryHyphen(
      kinds,
      startSegmentIndex,
      startGraphemeIndex,
      endSegmentIndex
    );
    for (let i = startSegmentIndex; i < endSegmentIndex; i++) {
      if (kinds[i] === "soft-hyphen" || kinds[i] === "hard-break") continue;
      if (i === startSegmentIndex && startGraphemeIndex > 0) {
        text += getSegmentGraphemes(i, segments, cache).slice(startGraphemeIndex).join("");
      } else {
        text += segments[i];
      }
    }
    if (endGraphemeIndex > 0) {
      if (endsWithDiscretionaryHyphen) text += "-";
      text += getSegmentGraphemes(endSegmentIndex, segments, cache).slice(
        startSegmentIndex === endSegmentIndex ? startGraphemeIndex : 0,
        endGraphemeIndex
      ).join("");
    } else if (endsWithDiscretionaryHyphen) {
      text += "-";
    }
    return text;
  }
  function createLayoutLine(prepared, cache, width, startSegmentIndex, startGraphemeIndex, endSegmentIndex, endGraphemeIndex) {
    return {
      text: buildLineTextFromRange(
        prepared.segments,
        prepared.kinds,
        cache,
        startSegmentIndex,
        startGraphemeIndex,
        endSegmentIndex,
        endGraphemeIndex
      ),
      width,
      start: {
        segmentIndex: startSegmentIndex,
        graphemeIndex: startGraphemeIndex
      },
      end: {
        segmentIndex: endSegmentIndex,
        graphemeIndex: endGraphemeIndex
      }
    };
  }
  function materializeLayoutLine(prepared, cache, line) {
    return createLayoutLine(
      prepared,
      cache,
      line.width,
      line.startSegmentIndex,
      line.startGraphemeIndex,
      line.endSegmentIndex,
      line.endGraphemeIndex
    );
  }
  function toLayoutLineRange(line) {
    return {
      width: line.width,
      start: {
        segmentIndex: line.startSegmentIndex,
        graphemeIndex: line.startGraphemeIndex
      },
      end: {
        segmentIndex: line.endSegmentIndex,
        graphemeIndex: line.endGraphemeIndex
      }
    };
  }
  function materializeLineRange(prepared, line) {
    return createLayoutLine(
      prepared,
      getLineTextCache(prepared),
      line.width,
      line.start.segmentIndex,
      line.start.graphemeIndex,
      line.end.segmentIndex,
      line.end.graphemeIndex
    );
  }
  function walkLineRanges(prepared, maxWidth, onLine) {
    if (prepared.widths.length === 0) return 0;
    return walkPreparedLines(getInternalPrepared(prepared), maxWidth, (line) => {
      onLine(toLayoutLineRange(line));
    });
  }
  function measureLineStats(prepared, maxWidth) {
    return measurePreparedLineGeometry(getInternalPrepared(prepared), maxWidth);
  }
  function measureNaturalWidth(prepared) {
    let maxWidth = 0;
    walkLineRanges(prepared, Number.POSITIVE_INFINITY, (line) => {
      if (line.width > maxWidth) maxWidth = line.width;
    });
    return maxWidth;
  }
  function layoutNextLine(prepared, start, maxWidth) {
    const line = layoutNextLineRange2(prepared, start, maxWidth);
    if (line === null) return null;
    return materializeLineRange(prepared, line);
  }
  function layoutNextLineRange2(prepared, start, maxWidth) {
    const line = layoutNextLineRange(prepared, start, maxWidth);
    if (line === null) return null;
    return toLayoutLineRange(line);
  }
  function layoutWithLines(prepared, maxWidth, lineHeight) {
    const lines = [];
    if (prepared.widths.length === 0) return { lineCount: 0, height: 0, lines };
    const graphemeCache = getLineTextCache(prepared);
    const lineCount = walkPreparedLines(getInternalPrepared(prepared), maxWidth, (line) => {
      lines.push(materializeLayoutLine(prepared, graphemeCache, line));
    });
    return { lineCount, height: lineCount * lineHeight, lines };
  }
  function clearCache() {
    clearAnalysisCaches();
    sharedGraphemeSegmenter2 = null;
    sharedLineTextCaches = /* @__PURE__ */ new WeakMap();
    clearMeasurementCaches();
  }
  function setLocale(locale) {
    setAnalysisLocale(locale);
    clearCache();
  }
  var sharedGraphemeSegmenter2, sharedLineTextCaches;
  var init_layout = __esm({
    "src/layout.ts"() {
      init_bidi();
      init_analysis();
      init_measurement();
      init_line_break();
      sharedGraphemeSegmenter2 = null;
      sharedLineTextCaches = /* @__PURE__ */ new WeakMap();
    }
  });

  // src/rich-inline.ts
  var rich_inline_exports = {};
  __export(rich_inline_exports, {
    layoutNextRichInlineLineRange: () => layoutNextRichInlineLineRange,
    materializeRichInlineLineRange: () => materializeRichInlineLineRange,
    measureRichInlineStats: () => measureRichInlineStats,
    prepareRichInline: () => prepareRichInline,
    walkRichInlineLineRanges: () => walkRichInlineLineRanges
  });
  function getInternalPreparedRichInline(prepared) {
    return prepared;
  }
  function cloneCursor(cursor) {
    return {
      segmentIndex: cursor.segmentIndex,
      graphemeIndex: cursor.graphemeIndex
    };
  }
  function isLineStartCursor(cursor) {
    return cursor.segmentIndex === 0 && cursor.graphemeIndex === 0;
  }
  function getCollapsedSpaceWidth(font, cache) {
    const cached = cache.get(font);
    if (cached !== void 0) return cached;
    const joinedWidth = measureNaturalWidth(prepareWithSegments("A A", font));
    const compactWidth = measureNaturalWidth(prepareWithSegments("AA", font));
    const collapsedWidth = Math.max(0, joinedWidth - compactWidth);
    cache.set(font, collapsedWidth);
    return collapsedWidth;
  }
  function prepareWholeItemLine(prepared) {
    const line = layoutNextLineRange(prepared, EMPTY_LAYOUT_CURSOR, Number.POSITIVE_INFINITY);
    if (line === null) return null;
    return {
      endGraphemeIndex: line.endGraphemeIndex,
      endSegmentIndex: line.endSegmentIndex,
      width: line.width
    };
  }
  function endsInsideFirstSegment(segmentIndex, graphemeIndex) {
    return segmentIndex === 0 && graphemeIndex > 0;
  }
  function prepareRichInline(items) {
    const preparedItems = [];
    const itemsBySourceItemIndex = Array.from({ length: items.length });
    const collapsedSpaceWidthCache = /* @__PURE__ */ new Map();
    let pendingGapWidth = 0;
    for (let index = 0; index < items.length; index++) {
      const item = items[index];
      const hasLeadingWhitespace = LEADING_COLLAPSIBLE_BOUNDARY_RE.test(item.text);
      const hasTrailingWhitespace = TRAILING_COLLAPSIBLE_BOUNDARY_RE.test(item.text);
      const trimmedText = item.text.replace(LEADING_COLLAPSIBLE_BOUNDARY_RE, "").replace(TRAILING_COLLAPSIBLE_BOUNDARY_RE, "");
      if (trimmedText.length === 0) {
        if (COLLAPSIBLE_BOUNDARY_RE.test(item.text) && pendingGapWidth === 0) {
          pendingGapWidth = getCollapsedSpaceWidth(item.font, collapsedSpaceWidthCache);
        }
        continue;
      }
      const gapBefore = pendingGapWidth > 0 ? pendingGapWidth : hasLeadingWhitespace ? getCollapsedSpaceWidth(item.font, collapsedSpaceWidthCache) : 0;
      const prepared = prepareWithSegments(trimmedText, item.font);
      const wholeLine = prepareWholeItemLine(prepared);
      if (wholeLine === null) {
        pendingGapWidth = hasTrailingWhitespace ? getCollapsedSpaceWidth(item.font, collapsedSpaceWidthCache) : 0;
        continue;
      }
      const preparedItem = {
        break: item.break ?? "normal",
        endGraphemeIndex: wholeLine.endGraphemeIndex,
        endSegmentIndex: wholeLine.endSegmentIndex,
        extraWidth: item.extraWidth ?? 0,
        gapBefore,
        naturalWidth: wholeLine.width,
        prepared,
        sourceItemIndex: index
      };
      preparedItems.push(preparedItem);
      itemsBySourceItemIndex[index] = preparedItem;
      pendingGapWidth = hasTrailingWhitespace ? getCollapsedSpaceWidth(item.font, collapsedSpaceWidthCache) : 0;
    }
    return {
      items: preparedItems,
      itemsBySourceItemIndex
    };
  }
  function stepRichInlineLine(flow, maxWidth, cursor, collectFragment) {
    if (flow.items.length === 0 || cursor.itemIndex >= flow.items.length) return null;
    const safeWidth = Math.max(1, maxWidth);
    let lineWidth = 0;
    let remainingWidth = safeWidth;
    let itemIndex = cursor.itemIndex;
    const textCursor = {
      segmentIndex: cursor.segmentIndex,
      graphemeIndex: cursor.graphemeIndex
    };
    lineLoop:
      while (itemIndex < flow.items.length) {
        const item = flow.items[itemIndex];
        if (!isLineStartCursor(textCursor) && textCursor.segmentIndex === item.endSegmentIndex && textCursor.graphemeIndex === item.endGraphemeIndex) {
          itemIndex++;
          textCursor.segmentIndex = 0;
          textCursor.graphemeIndex = 0;
          continue;
        }
        const gapBefore = lineWidth === 0 ? 0 : item.gapBefore;
        const atItemStart = isLineStartCursor(textCursor);
        if (item.break === "never") {
          if (!atItemStart) {
            itemIndex++;
            textCursor.segmentIndex = 0;
            textCursor.graphemeIndex = 0;
            continue;
          }
          const occupiedWidth = item.naturalWidth + item.extraWidth;
          const totalWidth = gapBefore + occupiedWidth;
          if (lineWidth > 0 && totalWidth > remainingWidth) break lineLoop;
          collectFragment?.(
            item,
            gapBefore,
            occupiedWidth,
            cloneCursor(EMPTY_LAYOUT_CURSOR),
            {
              segmentIndex: item.endSegmentIndex,
              graphemeIndex: item.endGraphemeIndex
            }
          );
          lineWidth += totalWidth;
          remainingWidth = Math.max(0, safeWidth - lineWidth);
          itemIndex++;
          textCursor.segmentIndex = 0;
          textCursor.graphemeIndex = 0;
          continue;
        }
        const reservedWidth = gapBefore + item.extraWidth;
        if (lineWidth > 0 && reservedWidth >= remainingWidth) break lineLoop;
        if (atItemStart) {
          const totalWidth = reservedWidth + item.naturalWidth;
          if (totalWidth <= remainingWidth) {
            collectFragment?.(
              item,
              gapBefore,
              item.naturalWidth + item.extraWidth,
              cloneCursor(EMPTY_LAYOUT_CURSOR),
              {
                segmentIndex: item.endSegmentIndex,
                graphemeIndex: item.endGraphemeIndex
              }
            );
            lineWidth += totalWidth;
            remainingWidth = Math.max(0, safeWidth - lineWidth);
            itemIndex++;
            textCursor.segmentIndex = 0;
            textCursor.graphemeIndex = 0;
            continue;
          }
        }
        const availableWidth = Math.max(1, remainingWidth - reservedWidth);
        const line = layoutNextLineRange(item.prepared, textCursor, availableWidth);
        if (line === null) {
          itemIndex++;
          textCursor.segmentIndex = 0;
          textCursor.graphemeIndex = 0;
          continue;
        }
        if (textCursor.segmentIndex === line.endSegmentIndex && textCursor.graphemeIndex === line.endGraphemeIndex) {
          itemIndex++;
          textCursor.segmentIndex = 0;
          textCursor.graphemeIndex = 0;
          continue;
        }
        if (lineWidth > 0 && atItemStart && gapBefore > 0 && endsInsideFirstSegment(line.endSegmentIndex, line.endGraphemeIndex)) {
          const freshLine = layoutNextLineRange(
            item.prepared,
            EMPTY_LAYOUT_CURSOR,
            Math.max(1, safeWidth - item.extraWidth)
          );
          if (freshLine !== null && (freshLine.endSegmentIndex > line.endSegmentIndex || freshLine.endSegmentIndex === line.endSegmentIndex && freshLine.endGraphemeIndex > line.endGraphemeIndex)) {
            break lineLoop;
          }
        }
        collectFragment?.(
          item,
          gapBefore,
          line.width + item.extraWidth,
          cloneCursor(textCursor),
          {
            segmentIndex: line.endSegmentIndex,
            graphemeIndex: line.endGraphemeIndex
          }
        );
        lineWidth += gapBefore + line.width + item.extraWidth;
        remainingWidth = Math.max(0, safeWidth - lineWidth);
        if (line.endSegmentIndex === item.endSegmentIndex && line.endGraphemeIndex === item.endGraphemeIndex) {
          itemIndex++;
          textCursor.segmentIndex = 0;
          textCursor.graphemeIndex = 0;
          continue;
        }
        textCursor.segmentIndex = line.endSegmentIndex;
        textCursor.graphemeIndex = line.endGraphemeIndex;
        break;
      }
    if (lineWidth === 0) return null;
    cursor.itemIndex = itemIndex;
    cursor.segmentIndex = textCursor.segmentIndex;
    cursor.graphemeIndex = textCursor.graphemeIndex;
    return lineWidth;
  }
  function stepRichInlineLineStats(flow, maxWidth, cursor) {
    if (flow.items.length === 0 || cursor.itemIndex >= flow.items.length) return null;
    const safeWidth = Math.max(1, maxWidth);
    let lineWidth = 0;
    let remainingWidth = safeWidth;
    let itemIndex = cursor.itemIndex;
    lineLoop:
      while (itemIndex < flow.items.length) {
        const item = flow.items[itemIndex];
        if (!isLineStartCursor(cursor) && cursor.segmentIndex === item.endSegmentIndex && cursor.graphemeIndex === item.endGraphemeIndex) {
          itemIndex++;
          cursor.segmentIndex = 0;
          cursor.graphemeIndex = 0;
          continue;
        }
        const gapBefore = lineWidth === 0 ? 0 : item.gapBefore;
        const atItemStart = isLineStartCursor(cursor);
        if (item.break === "never") {
          if (!atItemStart) {
            itemIndex++;
            cursor.segmentIndex = 0;
            cursor.graphemeIndex = 0;
            continue;
          }
          const occupiedWidth = item.naturalWidth + item.extraWidth;
          const totalWidth = gapBefore + occupiedWidth;
          if (lineWidth > 0 && totalWidth > remainingWidth) break lineLoop;
          lineWidth += totalWidth;
          remainingWidth = Math.max(0, safeWidth - lineWidth);
          itemIndex++;
          cursor.segmentIndex = 0;
          cursor.graphemeIndex = 0;
          continue;
        }
        const reservedWidth = gapBefore + item.extraWidth;
        if (lineWidth > 0 && reservedWidth >= remainingWidth) break lineLoop;
        if (atItemStart) {
          const totalWidth = reservedWidth + item.naturalWidth;
          if (totalWidth <= remainingWidth) {
            lineWidth += totalWidth;
            remainingWidth = Math.max(0, safeWidth - lineWidth);
            itemIndex++;
            cursor.segmentIndex = 0;
            cursor.graphemeIndex = 0;
            continue;
          }
        }
        const availableWidth = Math.max(1, remainingWidth - reservedWidth);
        const lineEnd = {
          segmentIndex: cursor.segmentIndex,
          graphemeIndex: cursor.graphemeIndex
        };
        const lineWidthForItem = stepPreparedLineGeometry(item.prepared, lineEnd, availableWidth);
        if (lineWidthForItem === null) {
          itemIndex++;
          cursor.segmentIndex = 0;
          cursor.graphemeIndex = 0;
          continue;
        }
        if (cursor.segmentIndex === lineEnd.segmentIndex && cursor.graphemeIndex === lineEnd.graphemeIndex) {
          itemIndex++;
          cursor.segmentIndex = 0;
          cursor.graphemeIndex = 0;
          continue;
        }
        if (lineWidth > 0 && atItemStart && gapBefore > 0 && endsInsideFirstSegment(lineEnd.segmentIndex, lineEnd.graphemeIndex)) {
          const freshLineEnd = {
            segmentIndex: 0,
            graphemeIndex: 0
          };
          const freshLineWidth = stepPreparedLineGeometry(
            item.prepared,
            freshLineEnd,
            Math.max(1, safeWidth - item.extraWidth)
          );
          if (freshLineWidth !== null && (freshLineEnd.segmentIndex > lineEnd.segmentIndex || freshLineEnd.segmentIndex === lineEnd.segmentIndex && freshLineEnd.graphemeIndex > lineEnd.graphemeIndex)) {
            break lineLoop;
          }
        }
        lineWidth += gapBefore + lineWidthForItem + item.extraWidth;
        remainingWidth = Math.max(0, safeWidth - lineWidth);
        if (lineEnd.segmentIndex === item.endSegmentIndex && lineEnd.graphemeIndex === item.endGraphemeIndex) {
          itemIndex++;
          cursor.segmentIndex = 0;
          cursor.graphemeIndex = 0;
          continue;
        }
        cursor.segmentIndex = lineEnd.segmentIndex;
        cursor.graphemeIndex = lineEnd.graphemeIndex;
        break;
      }
    if (lineWidth === 0) return null;
    cursor.itemIndex = itemIndex;
    return lineWidth;
  }
  function layoutNextRichInlineLineRange(prepared, maxWidth, start = RICH_INLINE_START_CURSOR) {
    const flow = getInternalPreparedRichInline(prepared);
    const end = {
      itemIndex: start.itemIndex,
      segmentIndex: start.segmentIndex,
      graphemeIndex: start.graphemeIndex
    };
    const fragments = [];
    const width = stepRichInlineLine(flow, maxWidth, end, (item, gapBefore, occupiedWidth, fragmentStart, fragmentEnd) => {
      fragments.push({
        itemIndex: item.sourceItemIndex,
        gapBefore,
        occupiedWidth,
        start: fragmentStart,
        end: fragmentEnd
      });
    });
    if (width === null) return null;
    return {
      fragments,
      width,
      end
    };
  }
  function materializeFragmentText(item, fragment) {
    const line = materializeLineRange(item.prepared, {
      width: fragment.occupiedWidth - item.extraWidth,
      start: fragment.start,
      end: fragment.end
    });
    return line.text;
  }
  function materializeRichInlineLineRange(prepared, line) {
    const flow = getInternalPreparedRichInline(prepared);
    return {
      fragments: line.fragments.map((fragment) => {
        const item = flow.itemsBySourceItemIndex[fragment.itemIndex];
        if (item === void 0) throw new Error("Missing rich-text inline item for fragment");
        return {
          ...fragment,
          text: materializeFragmentText(item, fragment)
        };
      }),
      width: line.width,
      end: line.end
    };
  }
  function walkRichInlineLineRanges(prepared, maxWidth, onLine) {
    let lineCount = 0;
    let cursor = RICH_INLINE_START_CURSOR;
    while (true) {
      const line = layoutNextRichInlineLineRange(prepared, maxWidth, cursor);
      if (line === null) return lineCount;
      onLine(line);
      lineCount++;
      cursor = line.end;
    }
  }
  function measureRichInlineStats(prepared, maxWidth) {
    const flow = getInternalPreparedRichInline(prepared);
    let lineCount = 0;
    let maxLineWidth = 0;
    const cursor = {
      itemIndex: 0,
      segmentIndex: 0,
      graphemeIndex: 0
    };
    while (true) {
      const lineWidth = stepRichInlineLineStats(flow, maxWidth, cursor);
      if (lineWidth === null) {
        return {
          lineCount,
          maxLineWidth
        };
      }
      lineCount++;
      if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;
    }
  }
  var COLLAPSIBLE_BOUNDARY_RE, LEADING_COLLAPSIBLE_BOUNDARY_RE, TRAILING_COLLAPSIBLE_BOUNDARY_RE, EMPTY_LAYOUT_CURSOR, RICH_INLINE_START_CURSOR;
  var init_rich_inline = __esm({
    "src/rich-inline.ts"() {
      init_layout();
      init_line_break();
      COLLAPSIBLE_BOUNDARY_RE = /[ \t\n\f\r]+/;
      LEADING_COLLAPSIBLE_BOUNDARY_RE = /^[ \t\n\f\r]+/;
      TRAILING_COLLAPSIBLE_BOUNDARY_RE = /[ \t\n\f\r]+$/;
      EMPTY_LAYOUT_CURSOR = { segmentIndex: 0, graphemeIndex: 0 };
      RICH_INLINE_START_CURSOR = {
        itemIndex: 0,
        segmentIndex: 0,
        graphemeIndex: 0
      };
    }
  });

  // src/layout.test.ts
  var import_bun_test = __require("bun:test");
  var FONT = "16px Test Sans";
  var LINE_HEIGHT = 19;
  var prepare2;
  var prepareWithSegments2;
  var layout2;
  var layoutWithLines2;
  var layoutNextLine2;
  var layoutNextLineRange3;
  var measureLineStats2;
  var walkLineRanges2;
  var clearCache2;
  var setLocale2;
  var countPreparedLines2;
  var measurePreparedLineGeometry2;
  var stepPreparedLineGeometry2;
  var walkPreparedLines2;
  var prepareRichInline2;
  var materializeRichInlineLineRange2;
  var measureRichInlineStats2;
  var walkRichInlineLineRanges2;
  var isCJK2;
  // --- Inline init (workaround for Promise.resolve().then closure bug) ---
  Reflect.set(globalThis, "OffscreenCanvas", TestOffscreenCanvas);
  init_analysis();
  init_layout();
  init_line_break();
  init_rich_inline();
  isCJK2 = analysis_exports.isCJK;
  prepare2 = layout_exports.prepare;
  prepareWithSegments2 = layout_exports.prepareWithSegments;
  layout2 = layout_exports.layout;
  layoutWithLines2 = layout_exports.layoutWithLines;
  layoutNextLine2 = layout_exports.layoutNextLine;
  layoutNextLineRange3 = layout_exports.layoutNextLineRange;
  measureLineStats2 = layout_exports.measureLineStats;
  walkLineRanges2 = layout_exports.walkLineRanges;
  clearCache2 = layout_exports.clearCache;
  setLocale2 = layout_exports.setLocale;
  countPreparedLines2 = line_break_exports.countPreparedLines;
  measurePreparedLineGeometry2 = line_break_exports.measurePreparedLineGeometry;
  stepPreparedLineGeometry2 = line_break_exports.stepPreparedLineGeometry;
  walkPreparedLines2 = line_break_exports.walkPreparedLines;
  prepareRichInline2 = rich_inline_exports.prepareRichInline;
  materializeRichInlineLineRange2 = rich_inline_exports.materializeRichInlineLineRange;
  measureRichInlineStats2 = rich_inline_exports.measureRichInlineStats;
  walkRichInlineLineRanges2 = rich_inline_exports.walkRichInlineLineRanges;
  // --- End inline init ---
  var emojiPresentationRe2 = /\p{Emoji_Presentation}/u;
  var punctuationRe = /[.,!?;:%)\]}'"”’»›…—-]/u;
  var decimalDigitRe2 = /\p{Nd}/u;
  var graphemeSegmenter = new Intl.Segmenter(void 0, { granularity: "grapheme" });
  function parseFontSize2(font) {
    const match = font.match(/(\d+(?:\.\d+)?)\s*px/);
    return match ? Number.parseFloat(match[1]) : 16;
  }
  function isWideCharacter(ch) {
    const code = ch.codePointAt(0);
    return code >= 19968 && code <= 40959 || code >= 13312 && code <= 19903 || code >= 63744 && code <= 64255 || code >= 194560 && code <= 195103 || code >= 131072 && code <= 173791 || code >= 173824 && code <= 177983 || code >= 177984 && code <= 178207 || code >= 178208 && code <= 183983 || code >= 183984 && code <= 191471 || code >= 191472 && code <= 192093 || code >= 196608 && code <= 201551 || code >= 201552 && code <= 205743 || code >= 205744 && code <= 210041 || code >= 12288 && code <= 12351 || code >= 12352 && code <= 12447 || code >= 12448 && code <= 12543 || code >= 12592 && code <= 12687 || code >= 44032 && code <= 55215 || code >= 65280 && code <= 65519;
  }
  function measureWidth(text, font) {
    const fontSize = parseFontSize2(font);
    let width = 0;
    let previousWasDecimalDigit = false;
    for (const ch of text) {
      if (ch === " ") {
        width += fontSize * 0.33;
        previousWasDecimalDigit = false;
      } else if (ch === "	") {
        width += fontSize * 1.32;
        previousWasDecimalDigit = false;
      } else if (emojiPresentationRe2.test(ch) || ch === "\uFE0F") {
        width += fontSize;
        previousWasDecimalDigit = false;
      } else if (decimalDigitRe2.test(ch)) {
        width += fontSize * (previousWasDecimalDigit ? 0.48 : 0.52);
        previousWasDecimalDigit = true;
      } else if (isWideCharacter(ch)) {
        width += fontSize;
        previousWasDecimalDigit = false;
      } else if (punctuationRe.test(ch)) {
        width += fontSize * 0.4;
        previousWasDecimalDigit = false;
      } else {
        width += fontSize * 0.6;
        previousWasDecimalDigit = false;
      }
    }
    return width;
  }
  function nextTabAdvance(lineWidth, spaceWidth, tabSize = 8) {
    const tabStopAdvance = spaceWidth * tabSize;
    const remainder = lineWidth % tabStopAdvance;
    return remainder === 0 ? tabStopAdvance : tabStopAdvance - remainder;
  }
  function getSegmentGraphemes2(text) {
    return Array.from(graphemeSegmenter.segment(text), (segment) => segment.segment);
  }
  function slicePreparedText(prepared, start, end) {
    if (start.segmentIndex === end.segmentIndex) {
      const segment = prepared.segments[start.segmentIndex];
      if (segment === void 0) return "";
      return getSegmentGraphemes2(segment).slice(start.graphemeIndex, end.graphemeIndex).join("");
    }
    let result = "";
    for (let segmentIndex = start.segmentIndex; segmentIndex < end.segmentIndex; segmentIndex++) {
      const segment = prepared.segments[segmentIndex];
      if (segment === void 0) break;
      if (segmentIndex === start.segmentIndex && start.graphemeIndex > 0) {
        result += getSegmentGraphemes2(segment).slice(start.graphemeIndex).join("");
      } else {
        result += segment;
      }
    }
    if (end.graphemeIndex > 0) {
      const segment = prepared.segments[end.segmentIndex];
      if (segment !== void 0) {
        result += getSegmentGraphemes2(segment).slice(0, end.graphemeIndex).join("");
      }
    }
    return result;
  }
  function reconstructFromLineBoundaries(prepared, lines) {
    return lines.map((line) => slicePreparedText(prepared, line.start, line.end)).join("");
  }
  function collectStreamedLines(prepared, width, start = { segmentIndex: 0, graphemeIndex: 0 }) {
    const lines = [];
    let cursor = { ...start };
    while (true) {
      const line = layoutNextLine2(prepared, cursor, width);
      if (line === null) break;
      lines.push(line);
      cursor = line.end;
    }
    return lines;
  }
  function collectStreamedLinesWithWidths(prepared, widths, start = { segmentIndex: 0, graphemeIndex: 0 }) {
    const lines = [];
    let cursor = { ...start };
    let widthIndex = 0;
    while (true) {
      const width = widths[widthIndex];
      if (width === void 0) {
        throw new Error("collectStreamedLinesWithWidths requires enough widths to finish the paragraph");
      }
      const line = layoutNextLine2(prepared, cursor, width);
      if (line === null) break;
      lines.push(line);
      cursor = line.end;
      widthIndex++;
    }
    return lines;
  }
  function reconstructFromWalkedRanges(prepared, width) {
    const slices = [];
    walkLineRanges2(prepared, width, (line) => {
      slices.push(slicePreparedText(prepared, line.start, line.end));
    });
    return slices.join("");
  }
  function compareCursors(a, b) {
    if (a.segmentIndex !== b.segmentIndex) return a.segmentIndex - b.segmentIndex;
    return a.graphemeIndex - b.graphemeIndex;
  }
  function terminalCursor(prepared) {
    return { segmentIndex: prepared.segments.length, graphemeIndex: 0 };
  }
  function getNonSpaceSegmentLevels(prepared) {
    if (prepared.segLevels === null || prepared.segLevels === void 0) return [];
    const levels = [];
    for (let i = 0; i < prepared.segments.length; i++) {
      const text = prepared.segments[i];
      if (text.trim().length === 0) continue;
      levels.push({ level: prepared.segLevels[i], text });
    }
    return levels;
  }
  var TestCanvasRenderingContext2D = class {
    constructor() {
      __publicField(this, "font", "");
    }
    measureText(text) {
      return { width: measureWidth(text, this.font) };
    }
  };
  var TestOffscreenCanvas = class {
    constructor(_width, _height) {
    }
    getContext(_kind) {
      return new TestCanvasRenderingContext2D();
    }
  };
  (0, import_bun_test.beforeAll)(async () => {
    Reflect.set(globalThis, "OffscreenCanvas", TestOffscreenCanvas);
    const [analysisMod, mod, lineBreakMod, richInlineMod] = await Promise.all([
      Promise.resolve().then(() => (init_analysis(), analysis_exports)),
      Promise.resolve().then(() => (init_layout(), layout_exports)),
      Promise.resolve().then(() => (init_line_break(), line_break_exports)),
      Promise.resolve().then(() => (init_rich_inline(), rich_inline_exports))
    ]);
    ({ isCJK: isCJK2 } = analysisMod);
    ({
      prepare: prepare2,
      prepareWithSegments: prepareWithSegments2,
      layout: layout2,
      layoutWithLines: layoutWithLines2,
      layoutNextLine: layoutNextLine2,
      layoutNextLineRange: layoutNextLineRange3,
      measureLineStats: measureLineStats2,
      walkLineRanges: walkLineRanges2,
      clearCache: clearCache2,
      setLocale: setLocale2
    } = mod);
    ({ countPreparedLines: countPreparedLines2, measurePreparedLineGeometry: measurePreparedLineGeometry2, stepPreparedLineGeometry: stepPreparedLineGeometry2, walkPreparedLines: walkPreparedLines2 } = lineBreakMod);
    ({ prepareRichInline: prepareRichInline2, materializeRichInlineLineRange: materializeRichInlineLineRange2, measureRichInlineStats: measureRichInlineStats2, walkRichInlineLineRanges: walkRichInlineLineRanges2 } = richInlineMod);
  });
  (0, import_bun_test.beforeEach)(() => {
    setLocale2(void 0);
    clearCache2();
  });
  (0, import_bun_test.describe)("prepare invariants", () => {
    (0, import_bun_test.test)("whitespace-only input stays empty", () => {
      const prepared = prepare2("  	\n  ", FONT);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 0, height: 0 });
    });
    (0, import_bun_test.test)("collapses ordinary whitespace runs and trims the edges", () => {
      const prepared = prepareWithSegments2("  Hello	 \n  World  ", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["Hello", " ", "World"]);
    });
    (0, import_bun_test.test)("pre-wrap mode keeps ordinary spaces instead of collapsing them", () => {
      const prepared = prepareWithSegments2("  Hello   World  ", FONT, { whiteSpace: "pre-wrap" });
      (0, import_bun_test.expect)(prepared.segments).toEqual(["  ", "Hello", "   ", "World", "  "]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["preserved-space", "text", "preserved-space", "text", "preserved-space"]);
    });
    (0, import_bun_test.test)("pre-wrap mode keeps hard breaks as explicit segments", () => {
      const prepared = prepareWithSegments2("Hello\nWorld", FONT, { whiteSpace: "pre-wrap" });
      (0, import_bun_test.expect)(prepared.segments).toEqual(["Hello", "\n", "World"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text", "hard-break", "text"]);
    });
    (0, import_bun_test.test)("pre-wrap mode normalizes CRLF into a single hard break", () => {
      const prepared = prepareWithSegments2("Hello\r\nWorld", FONT, { whiteSpace: "pre-wrap" });
      (0, import_bun_test.expect)(prepared.segments).toEqual(["Hello", "\n", "World"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text", "hard-break", "text"]);
    });
    (0, import_bun_test.test)("pre-wrap mode keeps tabs as explicit segments", () => {
      const prepared = prepareWithSegments2("Hello	World", FONT, { whiteSpace: "pre-wrap" });
      (0, import_bun_test.expect)(prepared.segments).toEqual(["Hello", "	", "World"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text", "tab", "text"]);
    });
    (0, import_bun_test.test)("keeps non-breaking spaces as glue instead of collapsing them away", () => {
      const prepared = prepareWithSegments2("Hello\xA0world", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["Hello\xA0world"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text"]);
    });
    (0, import_bun_test.test)("keeps standalone non-breaking spaces as visible glue content", () => {
      const prepared = prepareWithSegments2("\xA0", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\xA0"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 1, height: LINE_HEIGHT });
    });
    (0, import_bun_test.test)("pre-wrap mode keeps whitespace-only input visible", () => {
      const prepared = prepare2("   ", FONT, { whiteSpace: "pre-wrap" });
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 1, height: LINE_HEIGHT });
    });
    (0, import_bun_test.test)("keeps narrow no-break spaces as glue content", () => {
      const prepared = prepareWithSegments2("10\u202F000", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["10\u202F000"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text"]);
    });
    (0, import_bun_test.test)("keeps word joiners as glue content", () => {
      const prepared = prepareWithSegments2("foo\u2060bar", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["foo\u2060bar"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text"]);
    });
    (0, import_bun_test.test)("treats zero-width spaces as explicit break opportunities", () => {
      const prepared = prepareWithSegments2("alpha\u200Bbeta", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["alpha", "\u200B", "beta"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text", "zero-width-break", "text"]);
      const alphaWidth = prepared.widths[0];
      (0, import_bun_test.expect)(layout2(prepared, alphaWidth + 0.1, LINE_HEIGHT).lineCount).toBe(2);
    });
    (0, import_bun_test.test)("treats soft hyphens as discretionary break points", () => {
      const prepared = prepareWithSegments2("trans\xADatlantic", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["trans", "\xAD", "atlantic"]);
      (0, import_bun_test.expect)(prepared.kinds).toEqual(["text", "soft-hyphen", "text"]);
      const wide = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(wide.lineCount).toBe(1);
      (0, import_bun_test.expect)(wide.lines.map((line) => line.text)).toEqual(["transatlantic"]);
      const prefixed = prepareWithSegments2("foo trans\xADatlantic", FONT);
      const softBreakWidth = Math.max(
        prefixed.widths[0] + prefixed.widths[1] + prefixed.widths[2] + prefixed.discretionaryHyphenWidth,
        prefixed.widths[4]
      ) + 0.1;
      const narrow = layoutWithLines2(prefixed, softBreakWidth, LINE_HEIGHT);
      (0, import_bun_test.expect)(narrow.lineCount).toBe(2);
      (0, import_bun_test.expect)(narrow.lines.map((line) => line.text)).toEqual(["foo trans-", "atlantic"]);
      (0, import_bun_test.expect)(layout2(prefixed, softBreakWidth, LINE_HEIGHT).lineCount).toBe(narrow.lineCount);
      const continuedSoftBreakWidth = prefixed.widths[0] + prefixed.widths[1] + prefixed.widths[2] + prefixed.breakableFitAdvances[4][0] + prefixed.discretionaryHyphenWidth + 0.1;
      const continued = layoutWithLines2(prefixed, continuedSoftBreakWidth, LINE_HEIGHT);
      (0, import_bun_test.expect)(continued.lines.map((line) => line.text)).toEqual(["foo trans-a", "tlantic"]);
      (0, import_bun_test.expect)(layout2(prefixed, continuedSoftBreakWidth, LINE_HEIGHT).lineCount).toBe(continued.lineCount);
    });
    (0, import_bun_test.test)("keeps closing punctuation attached to the preceding word", () => {
      const prepared = prepareWithSegments2("hello.", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["hello."]);
    });
    (0, import_bun_test.test)("keeps arabic punctuation attached to the preceding word", () => {
      const prepared = prepareWithSegments2("\u0645\u0631\u062D\u0628\u0627\u060C \u0639\u0627\u0644\u0645\u061F", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0645\u0631\u062D\u0628\u0627\u060C", " ", "\u0639\u0627\u0644\u0645\u061F"]);
    });
    (0, import_bun_test.test)("keeps arabic punctuation-plus-mark clusters attached to the preceding word", () => {
      const prepared = prepareWithSegments2("\u0648\u062D\u0648\u0627\u0631\u0649 \u0628\u0643\u0634\u0621\u060C\u064D \u0645\u0646 \u0642\u0648\u0644\u0647\u0645", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0648\u062D\u0648\u0627\u0631\u0649", " ", "\u0628\u0643\u0634\u0621\u060C\u064D", " ", "\u0645\u0646", " ", "\u0642\u0648\u0644\u0647\u0645"]);
    });
    (0, import_bun_test.test)("keeps arabic no-space punctuation clusters together", () => {
      const prepared = prepareWithSegments2("\u0641\u064A\u0642\u0648\u0644:\u0648\u0639\u0644\u064A\u0643 \u0627\u0644\u0633\u0644\u0627\u0645", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0641\u064A\u0642\u0648\u0644:\u0648\u0639\u0644\u064A\u0643", " ", "\u0627\u0644\u0633\u0644\u0627\u0645"]);
    });
    (0, import_bun_test.test)("keeps arabic comma-followed text together without a space", () => {
      const prepared = prepareWithSegments2("\u0647\u0645\u0632\u0629\u064C\u060C\u0645\u0627 \u0643\u0627\u0646", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0647\u0645\u0632\u0629\u064C\u060C\u0645\u0627", " ", "\u0643\u0627\u0646"]);
    });
    (0, import_bun_test.test)("keeps leading arabic combining marks with the following word", () => {
      const prepared = prepareWithSegments2("\u0643\u0644 \u0650\u0651\u0648\u0627\u062D\u062F\u0629\u064D", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0643\u0644", " ", "\u0650\u0651\u0648\u0627\u062D\u062F\u0629\u064D"]);
    });
    (0, import_bun_test.test)("keeps devanagari danda punctuation attached to the preceding word", () => {
      const prepared = prepareWithSegments2("\u0928\u092E\u0938\u094D\u0924\u0947\u0964 \u0926\u0941\u0928\u093F\u092F\u093E\u0965", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u0928\u092E\u0938\u094D\u0924\u0947\u0964", " ", "\u0926\u0941\u0928\u093F\u092F\u093E\u0965"]);
    });
    (0, import_bun_test.test)("keeps myanmar punctuation attached to the preceding word", () => {
      const prepared = prepareWithSegments2("\u1016\u103C\u1005\u103A\u101E\u100A\u103A\u104B \u1014\u1031\u102C\u1000\u103A\u1010\u1005\u103A\u1001\u102F\u104A \u1000\u102D\u102F\u1000\u103A\u1001\u103B\u102E\u104D \u101A\u102F\u1036\u1000\u103C\u100A\u103A\u1019\u102D\u1000\u103C\u104F\u104B", FONT);
      (0, import_bun_test.expect)(prepared.segments.slice(0, 7)).toEqual(["\u1016\u103C\u1005\u103A\u101E\u100A\u103A\u104B", " ", "\u1014\u1031\u102C\u1000\u103A\u1010\u1005\u103A\u1001\u102F\u104A", " ", "\u1000\u102D\u102F\u1000\u103A", "\u1001\u103B\u102E\u104D", " "]);
      (0, import_bun_test.expect)(prepared.segments.at(-1)).toBe("\u1000\u103C\u104F\u104B");
    });
    (0, import_bun_test.test)("keeps myanmar possessive marker attached to the following word", () => {
      const prepared = prepareWithSegments2("\u1000\u103B\u103D\u1014\u103A\u102F\u1015\u103A\u104F\u101C\u1000\u103A\u1019\u1016\u103C\u1004\u1037\u103A", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u1000\u103B\u103D\u1014\u103A\u102F\u1015\u103A\u104F\u101C\u1000\u103A\u1019", "\u1016\u103C\u1004\u1037\u103A"]);
    });
    (0, import_bun_test.test)("keeps opening quotes attached to the following word", () => {
      const prepared = prepareWithSegments2("\u201CWhenever", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u201CWhenever"]);
    });
    (0, import_bun_test.test)("keeps apostrophe-led elisions attached to the following word", () => {
      const prepared = prepareWithSegments2("\u201CTake \u2019em downstairs", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u201CTake", " ", "\u2019em", " ", "downstairs"]);
    });
    (0, import_bun_test.test)("keeps stacked opening quotes attached to the following word", () => {
      const prepared = prepareWithSegments2("invented, \u201C\u2018George B. Wilson", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["invented,", " ", "\u201C\u2018George", " ", "B.", " ", "Wilson"]);
    });
    (0, import_bun_test.test)("treats ascii quotes as opening and closing glue by context", () => {
      const prepared = prepareWithSegments2('said "hello" there', FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["said", " ", '"hello"', " ", "there"]);
    });
    (0, import_bun_test.test)("treats escaped ascii quote clusters as opening and closing glue by context", () => {
      const text = String.raw`say \"hello\" there`;
      const prepared = prepareWithSegments2(text, FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["say", " ", String.raw`\"hello\"`, " ", "there"]);
    });
    (0, import_bun_test.test)("keeps escaped quote clusters attached through preceding opening punctuation", () => {
      const text = String.raw`((\"\"word`;
      const prepared = prepareWithSegments2(text, FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual([text]);
    });
    (0, import_bun_test.test)("keeps URL-like runs together as one breakable segment", () => {
      const prepared = prepareWithSegments2("see https://example.com/reports/q3?lang=ar&mode=full now", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual([
        "see",
        " ",
        "https://example.com/reports/q3?",
        "lang=ar&mode=full",
        " ",
        "now"
      ]);
    });
    (0, import_bun_test.test)("keeps no-space ascii punctuation chains together as one breakable segment", () => {
      const prepared = prepareWithSegments2("foo;bar foo:bar foo,bar as;lkdfjals;k", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual([
        "foo;bar",
        " ",
        "foo:bar",
        " ",
        "foo,bar",
        " ",
        "as;lkdfjals;k"
      ]);
    });
    (0, import_bun_test.test)("keeps numeric time ranges together", () => {
      const prepared = prepareWithSegments2("window 7:00-9:00 only", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["window", " ", "7:00-", "9:00", " ", "only"]);
    });
    (0, import_bun_test.test)("splits hyphenated numeric identifiers at preferred boundaries", () => {
      const prepared = prepareWithSegments2("SSN 420-69-8008 filed", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["SSN", " ", "420-", "69-", "8008", " ", "filed"]);
    });
    (0, import_bun_test.test)("keeps unicode-digit numeric expressions together", () => {
      const prepared = prepareWithSegments2("\u092F\u0939 \u0968\u096A\xD7\u096D \u0938\u092A\u094B\u0930\u094D\u091F \u0939\u0948", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u092F\u0939", " ", "\u0968\u096A\xD7\u096D", " ", "\u0938\u092A\u094B\u0930\u094D\u091F", " ", "\u0939\u0948"]);
    });
    (0, import_bun_test.test)("does not attach opening punctuation to following whitespace", () => {
      const prepared = prepareWithSegments2("\u201C hello", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u201C", " ", "hello"]);
    });
    (0, import_bun_test.test)("keeps japanese iteration marks attached to the preceding kana", () => {
      const prepared = prepareWithSegments2("\u68C4\u3066\u309D\u884C\u304F", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u68C4", "\u3066\u309D", "\u884C", "\u304F"]);
    });
    (0, import_bun_test.test)("carries trailing cjk opening punctuation forward across segment boundaries", () => {
      const prepared = prepareWithSegments2("\u4F5C\u8005\u306F\u3055\u3064\u304D\u3001\u300C\u4E0B\u4EBA", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u4F5C", "\u8005", "\u306F", "\u3055", "\u3064", "\u304D\u3001", "\u300C\u4E0B", "\u4EBA"]);
    });
    (0, import_bun_test.test)("keeps em dashes breakable", () => {
      const prepared = prepareWithSegments2("universe\u2014so", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["universe", "\u2014", "so"]);
    });
    (0, import_bun_test.test)("coalesces repeated punctuation runs into a single segment", () => {
      const prepared = prepareWithSegments2("=== heading ===", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["===", " ", "heading", " ", "==="]);
    });
    (0, import_bun_test.test)("keeps long repeated punctuation runs coalesced", () => {
      const text = "(".repeat(256);
      const prepared = prepareWithSegments2(text, FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual([text]);
    });
    (0, import_bun_test.test)("keeps repeated punctuation runs attachable to trailing closing punctuation", () => {
      const prepared = prepareWithSegments2("((()", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["((()"]);
    });
    (0, import_bun_test.test)("applies CJK and Hangul punctuation attachment rules", () => {
      (0, import_bun_test.expect)(prepareWithSegments2("\u4E2D\u6587\uFF0C\u6D4B\u8BD5\u3002", FONT).segments).toEqual(["\u4E2D", "\u6587\uFF0C", "\u6D4B", "\u8BD5\u3002"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\uD14C\uC2A4\uD2B8\uC785\uB2C8\uB2E4.", FONT).segments.at(-1)).toBe("\uB2E4.");
    });
    (0, import_bun_test.test)("treats Hangul compatibility jamo as CJK break units", () => {
      const prepared = prepareWithSegments2("\u314B\u314B\u314B \uC9C4\uC9DC", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u314B", "\u314B", "\u314B", " ", "\uC9C4", "\uC9DC"]);
      const width = measureWidth("\u314B\u314B", FONT) + 0.1;
      const lines = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["\u314B\u314B", "\u314B ", "\uC9C4\uC9DC"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT)).toEqual({
        lineCount: 3,
        height: LINE_HEIGHT * 3
      });
    });
    (0, import_bun_test.test)("keeps non-CJK glue-connected runs intact before CJK text", () => {
      const prepared = prepareWithSegments2("foo\xA0\u4E16\u754C", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["foo\xA0", "\u4E16", "\u754C"]);
    });
    (0, import_bun_test.test)("keep-all keeps CJK-leading no-space runs cohesive without swallowing preceding latin runs", () => {
      (0, import_bun_test.expect)(prepareWithSegments2("\u4E2D\u6587\uFF0C\u6D4B\u8BD5\u3002", FONT, { wordBreak: "keep-all" }).segments).toEqual(["\u4E2D\u6587\uFF0C", "\u6D4B\u8BD5\u3002"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\uD55C\uAD6D\uC5B4\uD14C\uC2A4\uD2B8", FONT, { wordBreak: "keep-all" }).segments).toEqual(["\uD55C\uAD6D\uC5B4\uD14C\uC2A4\uD2B8"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\u6F22".repeat(256), FONT, { wordBreak: "keep-all" }).segments).toEqual(["\u6F22".repeat(256)]);
      for (const text of ["\u65E5\u672C\u8A9Efoo-bar", "\u65E5\u672C\u8A9Efoo.bar", "\u65E5\u672C\u8A9Efoo\u2014bar"]) {
        (0, import_bun_test.expect)(prepareWithSegments2(text, FONT, { wordBreak: "keep-all" }).segments).toEqual([text]);
      }
      (0, import_bun_test.expect)(prepareWithSegments2("foo-bar\u65E5\u672C\u8A9E", FONT, { wordBreak: "keep-all" }).segments).toEqual(["foo-", "bar", "\u65E5\u672C\u8A9E"]);
      (0, import_bun_test.expect)(prepareWithSegments2("foo\xA0\u4E16\u754C", FONT, { wordBreak: "keep-all" }).segments).toEqual(["foo\xA0", "\u4E16\u754C"]);
    });
    (0, import_bun_test.test)("adjacent CJK text units stay breakable after visible text, not only after spaces", () => {
      const prepared = prepareWithSegments2("foo \u4E16\u754C bar", FONT);
      (0, import_bun_test.expect)(prepared.segments).toEqual(["foo", " ", "\u4E16", "\u754C", " ", "bar"]);
      const width = prepared.widths[0] + prepared.widths[1] + prepared.widths[2] + 0.1;
      const batched = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(batched.lines.map((line) => line.text)).toEqual(["foo \u4E16", "\u754C bar"]);
      const streamed = [];
      let cursor = { segmentIndex: 0, graphemeIndex: 0 };
      while (true) {
        const line = layoutNextLine2(prepared, cursor, width);
        if (line === null) break;
        streamed.push(line.text);
        cursor = line.end;
      }
      (0, import_bun_test.expect)(streamed).toEqual(["foo \u4E16", "\u754C bar"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT)).toEqual({ lineCount: 2, height: LINE_HEIGHT * 2 });
    });
    (0, import_bun_test.test)("treats astral CJK ideographs as CJK break units", () => {
      const samples = ["\u{20000}", "\u{2EBF0}", "\u{31350}", "\u{323B0}"];
      for (let i = 0; i < samples.length; i++) {
        const sample = samples[i];
        (0, import_bun_test.expect)(prepareWithSegments2(`${sample}${sample}`, FONT).segments).toEqual([sample, sample]);
        (0, import_bun_test.expect)(prepareWithSegments2(`${sample}\u3002`, FONT).segments).toEqual([`${sample}\u3002`]);
      }
    });
    (0, import_bun_test.test)("isCJK covers Hangul compatibility jamo and the newer CJK extension blocks", () => {
      (0, import_bun_test.expect)(isCJK2("\u314B")).toBe(true);
      (0, import_bun_test.expect)(isCJK2("\u{2EBF0}")).toBe(true);
      (0, import_bun_test.expect)(isCJK2("\u{31350}")).toBe(true);
      (0, import_bun_test.expect)(isCJK2("\u{323B0}")).toBe(true);
      (0, import_bun_test.expect)(isCJK2("hello")).toBe(false);
    });
    (0, import_bun_test.test)("keeps opening brackets after CJK attached to following annotation text", () => {
      (0, import_bun_test.expect)(prepareWithSegments2("\uC11C\uC6B8(Seoul)\uACFC", FONT).segments).toEqual(["\uC11C", "\uC6B8", "(Seoul)", "\uACFC"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\u6771\u4EAC(Tokyo)\u3068", FONT).segments).toEqual(["\u6771", "\u4EAC", "(Tokyo)", "\u3068"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\u5317\u4EAC(Beijing)\u548C", FONT).segments).toEqual(["\u5317", "\u4EAC", "(Beijing)", "\u548C"]);
      (0, import_bun_test.expect)(prepareWithSegments2("\uCC38\uC870[1]\uC640", FONT).segments).toEqual(["\uCC38", "\uC870", "[1]", "\uC640"]);
      (0, import_bun_test.expect)(prepareWithSegments2("AB(CD)", FONT).segments).toEqual(["AB(", "CD)"]);
    });
    (0, import_bun_test.test)("prepare and prepareWithSegments agree on layout behavior", () => {
      const plain = prepare2("Alpha beta gamma", FONT);
      const rich = prepareWithSegments2("Alpha beta gamma", FONT);
      for (const width of [40, 80, 200]) {
        (0, import_bun_test.expect)(layout2(plain, width, LINE_HEIGHT)).toEqual(layout2(rich, width, LINE_HEIGHT));
      }
    });
    (0, import_bun_test.test)("locale can be reset without disturbing later prepares", () => {
      setLocale2("th");
      const thai = prepare2("\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22", FONT);
      (0, import_bun_test.expect)(layout2(thai, 80, LINE_HEIGHT).lineCount).toBeGreaterThan(0);
      setLocale2(void 0);
      const latin = prepare2("hello world", FONT);
      (0, import_bun_test.expect)(layout2(latin, 200, LINE_HEIGHT)).toEqual({ lineCount: 1, height: LINE_HEIGHT });
    });
    (0, import_bun_test.test)("pure LTR text skips rich bidi metadata", () => {
      (0, import_bun_test.expect)(prepareWithSegments2("hello world", FONT).segLevels).toBeNull();
    });
    (0, import_bun_test.test)("rich bidi metadata uses the first strong character for paragraph direction", () => {
      const ltrFirst = prepareWithSegments2("one \u0627\u062B\u0646\u0627\u0646 three", FONT);
      (0, import_bun_test.expect)(ltrFirst.segLevels).not.toBeNull();
      (0, import_bun_test.expect)(ltrFirst.segLevels).toHaveLength(ltrFirst.segments.length);
      (0, import_bun_test.expect)(getNonSpaceSegmentLevels(ltrFirst)).toEqual([
        { text: "one", level: 0 },
        { text: "\u0627\u062B\u0646\u0627\u0646", level: 1 },
        { text: "three", level: 0 }
      ]);
      const rtlFirst = prepareWithSegments2("123 \u0648\u0627\u062D\u062F three", FONT);
      (0, import_bun_test.expect)(rtlFirst.segLevels).not.toBeNull();
      (0, import_bun_test.expect)(rtlFirst.segLevels).toHaveLength(rtlFirst.segments.length);
      (0, import_bun_test.expect)(getNonSpaceSegmentLevels(rtlFirst)).toEqual([
        { text: "123", level: 2 },
        { text: "\u0648\u0627\u062D\u062F", level: 1 },
        { text: "three", level: 2 }
      ]);
      const astralRtlFirst = prepareWithSegments2("\u{1E900}\u{1E901} abc", FONT);
      (0, import_bun_test.expect)(astralRtlFirst.segLevels).not.toBeNull();
      (0, import_bun_test.expect)(astralRtlFirst.segLevels).toHaveLength(astralRtlFirst.segments.length);
      (0, import_bun_test.expect)(getNonSpaceSegmentLevels(astralRtlFirst)).toEqual([
        { text: "\u{1E900}\u{1E901}", level: 1 },
        { text: "abc", level: 2 }
      ]);
    });
  });
  (0, import_bun_test.describe)("rich-inline invariants", () => {
    (0, import_bun_test.test)("non-materializing range walker matches range materialization", () => {
      const prepared = prepareRichInline2([
        { text: "Ship ", font: FONT },
        { text: "@maya", font: "700 12px Test Sans", break: "never", extraWidth: 18 },
        { text: "'s rich note wraps cleanly", font: FONT }
      ]);
      const rangedLines = [];
      const materializedLines = [];
      const rangeLineCount = walkRichInlineLineRanges2(prepared, 120, (line) => {
        rangedLines.push({
          end: line.end,
          fragments: line.fragments.map((fragment) => ({
            end: fragment.end,
            gapBefore: fragment.gapBefore,
            itemIndex: fragment.itemIndex,
            occupiedWidth: fragment.occupiedWidth,
            start: fragment.start
          })),
          width: line.width
        });
      });
      const materializedLineCount = walkRichInlineLineRanges2(prepared, 120, (range) => {
        const line = materializeRichInlineLineRange2(prepared, range);
        materializedLines.push({
          end: line.end,
          fragments: line.fragments.map((fragment) => ({
            end: fragment.end,
            gapBefore: fragment.gapBefore,
            itemIndex: fragment.itemIndex,
            occupiedWidth: fragment.occupiedWidth,
            start: fragment.start,
            text: fragment.text
          })),
          width: line.width
        });
      });
      (0, import_bun_test.expect)(rangeLineCount).toBe(materializedLineCount);
      (0, import_bun_test.expect)(measureRichInlineStats2(prepared, 120)).toEqual({
        lineCount: rangeLineCount,
        maxLineWidth: Math.max(...rangedLines.map((line) => line.width))
      });
      (0, import_bun_test.expect)(rangedLines).toHaveLength(materializedLines.length);
      for (let index = 0; index < rangedLines.length; index++) {
        const rangeLine = rangedLines[index];
        const materializedLine = materializedLines[index];
        (0, import_bun_test.expect)(rangeLine.width).toBe(materializedLine.width);
        (0, import_bun_test.expect)(rangeLine.end).toEqual(materializedLine.end);
        (0, import_bun_test.expect)(rangeLine.fragments).toEqual(
          materializedLine.fragments.map(({ text: _text, ...fragment }) => fragment)
        );
      }
    });
  });
  (0, import_bun_test.describe)("layout invariants", () => {
    (0, import_bun_test.test)("line count grows monotonically as width shrinks", () => {
      const prepared = prepare2("The quick brown fox jumps over the lazy dog", FONT);
      let previous = 0;
      for (const width of [320, 200, 140, 90]) {
        const { lineCount } = layout2(prepared, width, LINE_HEIGHT);
        (0, import_bun_test.expect)(lineCount).toBeGreaterThanOrEqual(previous);
        previous = lineCount;
      }
    });
    (0, import_bun_test.test)("trailing whitespace hangs past the line edge", () => {
      const prepared = prepareWithSegments2("Hello ", FONT);
      const widthOfHello = prepared.widths[0];
      (0, import_bun_test.expect)(layout2(prepared, widthOfHello, LINE_HEIGHT).lineCount).toBe(1);
      const withLines = layoutWithLines2(prepared, widthOfHello, LINE_HEIGHT);
      (0, import_bun_test.expect)(withLines.lineCount).toBe(1);
      (0, import_bun_test.expect)(withLines.lines).toEqual([{
        text: "Hello",
        width: widthOfHello,
        start: { segmentIndex: 0, graphemeIndex: 0 },
        end: { segmentIndex: 1, graphemeIndex: 0 }
      }]);
    });
    (0, import_bun_test.test)("breaks long words at grapheme boundaries and keeps both layout APIs aligned", () => {
      const prepared = prepareWithSegments2("Superlongword", FONT);
      const graphemeWidths = prepared.breakableFitAdvances[0];
      const maxWidth = graphemeWidths[0] + graphemeWidths[1] + graphemeWidths[2] + 0.1;
      const plain = layout2(prepared, maxWidth, LINE_HEIGHT);
      const rich = layoutWithLines2(prepared, maxWidth, LINE_HEIGHT);
      (0, import_bun_test.expect)(plain.lineCount).toBeGreaterThan(1);
      (0, import_bun_test.expect)(rich.lineCount).toBe(plain.lineCount);
      (0, import_bun_test.expect)(rich.height).toBe(plain.height);
      (0, import_bun_test.expect)(rich.lines.map((line) => line.text).join("")).toBe("Superlongword");
      (0, import_bun_test.expect)(rich.lines[0].start).toEqual({ segmentIndex: 0, graphemeIndex: 0 });
      (0, import_bun_test.expect)(rich.lines.at(-1).end).toEqual({ segmentIndex: 1, graphemeIndex: 0 });
    });
    (0, import_bun_test.test)("mixed-direction text is a stable smoke test", () => {
      const prepared = prepareWithSegments2("According to \u0645\u062D\u0645\u062F \u0627\u0644\u0623\u062D\u0645\u062F, the results improved.", FONT);
      const result = layoutWithLines2(prepared, 120, LINE_HEIGHT);
      (0, import_bun_test.expect)(result.lineCount).toBeGreaterThanOrEqual(1);
      (0, import_bun_test.expect)(result.height).toBe(result.lineCount * LINE_HEIGHT);
      (0, import_bun_test.expect)(result.lines.map((line) => line.text).join("")).toBe("According to \u0645\u062D\u0645\u062F \u0627\u0644\u0623\u062D\u0645\u062F, the results improved.");
    });
    (0, import_bun_test.test)("layoutNextLine reproduces layoutWithLines exactly", () => {
      const prepared = prepareWithSegments2('foo trans\xADatlantic said "hello" to \u4E16\u754C and waved.', FONT);
      const width = prepared.widths[0] + prepared.widths[1] + prepared.widths[2] + prepared.breakableFitAdvances[4][0] + prepared.discretionaryHyphenWidth + 0.1;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      const actual = [];
      let cursor = { segmentIndex: 0, graphemeIndex: 0 };
      while (true) {
        const line = layoutNextLine2(prepared, cursor, width);
        if (line === null) break;
        actual.push(line);
        cursor = line.end;
      }
      (0, import_bun_test.expect)(actual).toEqual(expected.lines);
    });
    (0, import_bun_test.test)("mixed-script canary keeps layoutWithLines and layoutNextLine aligned across CJK, RTL, and emoji", () => {
      const prepared = prepareWithSegments2("Hello \u4E16\u754C \u0645\u0631\u062D\u0628\u0627 \u{1F30D} test", FONT);
      const width = 80;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(expected.lines.map((line) => line.text)).toEqual(["Hello \u4E16", "\u754C \u0645\u0631\u062D\u0628\u0627 ", "\u{1F30D} test"]);
      const actual = collectStreamedLines(prepared, width);
      (0, import_bun_test.expect)(actual).toEqual(expected.lines);
    });
    (0, import_bun_test.test)("layout and layoutWithLines stay aligned when ZWSP triggers narrow grapheme breaking", () => {
      const cases = [
        "alpha\u200Bbeta",
        "alpha\u200Bbeta\u200Cgamma"
      ];
      for (const text of cases) {
        const plain = prepare2(text, FONT);
        const rich = prepareWithSegments2(text, FONT);
        const width = 10;
        (0, import_bun_test.expect)(layout2(plain, width, LINE_HEIGHT).lineCount).toBe(layoutWithLines2(rich, width, LINE_HEIGHT).lineCount);
      }
    });
    (0, import_bun_test.test)("layoutWithLines strips leading collapsible space after a ZWSP break the same way as layoutNextLine", () => {
      const prepared = prepareWithSegments2("\u751F\u6D3B\u5C31\u50CF\u6D77\u6D0B\u200B \u53EA\u6709\u610F\u5FD7\u575A\u5B9A\u7684\u4EBA\u624D\u80FD\u5230\u8FBE\u5F7C\u5CB8", FONT);
      const width = prepared.widths[0] - 1;
      (0, import_bun_test.expect)(layoutWithLines2(prepared, width, LINE_HEIGHT).lines).toEqual(collectStreamedLines(prepared, width));
    });
    (0, import_bun_test.test)("chunked batch line walking normalizes spaces after zero-width breaks like streaming", () => {
      const prepared = prepareWithSegments2("x\xAD A\u200B B", FONT);
      const width = measureWidth("x A", FONT) + 0.1;
      const batched = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(batched.lines.map((line) => line.text)).toEqual(["x A\u200B", "B"]);
      (0, import_bun_test.expect)(collectStreamedLines(prepared, width)).toEqual(batched.lines);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(batched.lineCount);
    });
    (0, import_bun_test.test)("layoutNextLine can resume from any fixed-width line start without hidden state", () => {
      const prepared = prepareWithSegments2('foo trans\xADatlantic said "hello" to \u4E16\u754C and waved. alpha\u200Bbeta \u{1F680}', FONT);
      const width = 90;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(expected.lines.length).toBeGreaterThan(2);
      for (let i = 0; i < expected.lines.length; i++) {
        const suffix = collectStreamedLines(prepared, width, expected.lines[i].start);
        (0, import_bun_test.expect)(suffix).toEqual(expected.lines.slice(i));
      }
      (0, import_bun_test.expect)(layoutNextLine2(prepared, terminalCursor(prepared), width)).toBeNull();
    });
    (0, import_bun_test.test)("rich line boundary cursors reconstruct normalized source text exactly", () => {
      const cases = [
        "a b c",
        "  Hello	 \n  World  ",
        'foo trans\xADatlantic said "hello" to \u4E16\u754C and waved.',
        "According to \u0645\u062D\u0645\u062F \u0627\u0644\u0623\u062D\u0645\u062F, the results improved.",
        "see https://example.com/reports/q3?lang=ar&mode=full now",
        "alpha\u200Bbeta gamma"
      ];
      const widths = [40, 80, 120, 200];
      for (const text of cases) {
        const prepared = prepareWithSegments2(text, FONT);
        const expected = prepared.segments.join("");
        for (const width of widths) {
          const batched = layoutWithLines2(prepared, width, LINE_HEIGHT);
          const streamed = collectStreamedLines(prepared, width);
          (0, import_bun_test.expect)(reconstructFromLineBoundaries(prepared, batched.lines)).toBe(expected);
          (0, import_bun_test.expect)(reconstructFromLineBoundaries(prepared, streamed)).toBe(expected);
          (0, import_bun_test.expect)(reconstructFromWalkedRanges(prepared, width)).toBe(expected);
        }
      }
    });
    (0, import_bun_test.test)("soft-hyphen round-trip uses source slices instead of rendered line text", () => {
      const prepared = prepareWithSegments2("foo trans\xADatlantic", FONT);
      const width = prepared.widths[0] + prepared.widths[1] + prepared.widths[2] + prepared.breakableFitAdvances[4][0] + prepared.discretionaryHyphenWidth + 0.1;
      const result = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(result.lines.map((line) => line.text).join("")).toBe("foo trans-atlantic");
      (0, import_bun_test.expect)(reconstructFromLineBoundaries(prepared, result.lines)).toBe("foo trans\xADatlantic");
    });
    (0, import_bun_test.test)("soft-hyphen fallback does not crash when overflow happens on a later space", () => {
      const prepared = prepareWithSegments2("foo trans\xADatlantic labels", FONT);
      const width = measureWidth("foo transatlantic", FONT) + 0.1;
      const result = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(result.lines.map((line) => line.text)).toEqual(["foo transatlantic ", "labels"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(result.lineCount);
    });
    (0, import_bun_test.test)("layoutNextLine variable-width streaming stays contiguous and reconstructs normalized text", () => {
      const prepared = prepareWithSegments2(
        'foo trans\xADatlantic said "hello" to \u4E16\u754C and waved. According to \u0645\u062D\u0645\u062F \u0627\u0644\u0623\u062D\u0645\u062F, alpha\u200Bbeta \u{1F680}',
        FONT
      );
      const widths = [140, 72, 108, 64, 160, 84, 116, 70, 180, 92, 128, 76];
      const lines = collectStreamedLinesWithWidths(prepared, widths);
      const expected = prepared.segments.join("");
      (0, import_bun_test.expect)(lines.length).toBeGreaterThan(2);
      (0, import_bun_test.expect)(lines[0].start).toEqual({ segmentIndex: 0, graphemeIndex: 0 });
      for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        (0, import_bun_test.expect)(compareCursors(line.end, line.start)).toBeGreaterThan(0);
        if (i > 0) {
          (0, import_bun_test.expect)(line.start).toEqual(lines[i - 1].end);
        }
      }
      (0, import_bun_test.expect)(lines.at(-1).end).toEqual(terminalCursor(prepared));
      (0, import_bun_test.expect)(reconstructFromLineBoundaries(prepared, lines)).toBe(expected);
      (0, import_bun_test.expect)(layoutNextLine2(prepared, terminalCursor(prepared), widths.at(-1))).toBeNull();
    });
    (0, import_bun_test.test)("layoutNextLine variable-width streaming stays contiguous in pre-wrap mode", () => {
      const prepared = prepareWithSegments2("foo\n  bar baz\n	quux quuz", FONT, { whiteSpace: "pre-wrap" });
      const widths = [200, 62, 80, 200, 72, 200];
      const lines = collectStreamedLinesWithWidths(prepared, widths);
      const expected = prepared.segments.join("");
      (0, import_bun_test.expect)(lines.length).toBeGreaterThanOrEqual(4);
      (0, import_bun_test.expect)(lines[0].start).toEqual({ segmentIndex: 0, graphemeIndex: 0 });
      for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        (0, import_bun_test.expect)(compareCursors(line.end, line.start)).toBeGreaterThan(0);
        if (i > 0) {
          (0, import_bun_test.expect)(line.start).toEqual(lines[i - 1].end);
        }
      }
      (0, import_bun_test.expect)(lines.at(-1).end).toEqual(terminalCursor(prepared));
      (0, import_bun_test.expect)(reconstructFromLineBoundaries(prepared, lines)).toBe(expected);
      (0, import_bun_test.expect)(layoutNextLine2(prepared, terminalCursor(prepared), widths.at(-1))).toBeNull();
    });
    (0, import_bun_test.test)("pre-wrap mode keeps hanging spaces visible at line end", () => {
      const prepared = prepareWithSegments2("foo   bar", FONT, { whiteSpace: "pre-wrap" });
      const width = measureWidth("foo", FONT) + 0.1;
      const lines = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lineCount).toBe(2);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["foo   ", "bar"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(2);
    });
    (0, import_bun_test.test)("pre-wrap mode treats hard breaks as forced line boundaries", () => {
      const prepared = prepareWithSegments2("a\nb", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["a", "b"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT).lineCount).toBe(2);
    });
    (0, import_bun_test.test)("pre-wrap mode treats tabs as hanging whitespace aligned to tab stops", () => {
      const prepared = prepareWithSegments2("a	b", FONT, { whiteSpace: "pre-wrap" });
      const spaceWidth = measureWidth(" ", FONT);
      const prefixWidth = measureWidth("a", FONT);
      const tabAdvance = nextTabAdvance(prefixWidth, spaceWidth, 8);
      const textWidth = prefixWidth + tabAdvance + measureWidth("b", FONT);
      const width = textWidth - 0.1;
      const lines = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["a	", "b"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(2);
    });
    (0, import_bun_test.test)("pre-wrap mode treats consecutive tabs as distinct tab stops", () => {
      const prepared = prepareWithSegments2("a		b", FONT, { whiteSpace: "pre-wrap" });
      const spaceWidth = measureWidth(" ", FONT);
      const prefixWidth = measureWidth("a", FONT);
      const firstTabAdvance = nextTabAdvance(prefixWidth, spaceWidth, 8);
      const afterFirstTab = prefixWidth + firstTabAdvance;
      const secondTabAdvance = nextTabAdvance(afterFirstTab, spaceWidth, 8);
      const width = prefixWidth + firstTabAdvance + secondTabAdvance - 0.1;
      const lines = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["a		", "b"]);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(2);
    });
    (0, import_bun_test.test)("pre-wrap mode keeps whitespace-only middle lines visible", () => {
      const prepared = prepareWithSegments2("foo\n  \nbar", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["foo", "  ", "bar"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 3, height: LINE_HEIGHT * 3 });
    });
    (0, import_bun_test.test)("pre-wrap mode keeps trailing spaces before a hard break on the current line", () => {
      const prepared = prepareWithSegments2("foo  \nbar", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["foo  ", "bar"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 2, height: LINE_HEIGHT * 2 });
    });
    (0, import_bun_test.test)("pre-wrap mode keeps trailing tabs before a hard break on the current line", () => {
      const prepared = prepareWithSegments2("foo	\nbar", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["foo	", "bar"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 2, height: LINE_HEIGHT * 2 });
    });
    (0, import_bun_test.test)("pre-wrap mode restarts tab stops after a hard break", () => {
      const prepared = prepareWithSegments2("foo\n	bar", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      const spaceWidth = measureWidth(" ", FONT);
      const expectedSecondLineWidth = nextTabAdvance(0, spaceWidth, 8) + measureWidth("bar", FONT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["foo", "	bar"]);
      (0, import_bun_test.expect)(lines.lines[1].width).toBeCloseTo(expectedSecondLineWidth, 5);
    });
    (0, import_bun_test.test)("layoutNextLine stays aligned with layoutWithLines in pre-wrap mode", () => {
      const prepared = prepareWithSegments2("foo\n  bar baz\nquux", FONT, { whiteSpace: "pre-wrap" });
      const width = measureWidth("  bar", FONT) + 0.1;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      const actual = [];
      let cursor = { segmentIndex: 0, graphemeIndex: 0 };
      while (true) {
        const line = layoutNextLine2(prepared, cursor, width);
        if (line === null) break;
        actual.push(line);
        cursor = line.end;
      }
      (0, import_bun_test.expect)(actual).toEqual(expected.lines);
    });
    (0, import_bun_test.test)("pre-wrap soft hyphen does not preempt a closer preserved-space break", () => {
      const prepared = prepareWithSegments2("A\nb\u0627 \xADb\u060C b", FONT, { whiteSpace: "pre-wrap" });
      const width = measureWidth("b\u0627", FONT) + measureWidth(" ", FONT) + measureWidth("b\u060C", FONT) + measureWidth(" ", FONT) + 0.1;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(expected.lines.map((line) => line.text)).toEqual(["A", "b\u0627 b\u060C ", "b"]);
      (0, import_bun_test.expect)(collectStreamedLines(prepared, width)).toEqual(expected.lines);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(expected.lineCount);
    });
    (0, import_bun_test.test)("pre-wrap mode keeps empty lines from consecutive hard breaks", () => {
      const prepared = prepareWithSegments2("\n\n", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["", ""]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 2, height: LINE_HEIGHT * 2 });
    });
    (0, import_bun_test.test)("pre-wrap mode does not invent an extra trailing empty line", () => {
      const prepared = prepareWithSegments2("a\n", FONT, { whiteSpace: "pre-wrap" });
      const lines = layoutWithLines2(prepared, 200, LINE_HEIGHT);
      (0, import_bun_test.expect)(lines.lines.map((line) => line.text)).toEqual(["a"]);
      (0, import_bun_test.expect)(layout2(prepared, 200, LINE_HEIGHT)).toEqual({ lineCount: 1, height: LINE_HEIGHT });
    });
    (0, import_bun_test.test)("overlong breakable segments wrap onto a fresh line when the current line already has content", () => {
      const prepared = prepareWithSegments2("foo abcdefghijk", FONT);
      const prefixWidth = prepared.widths[0] + prepared.widths[1];
      const wordBreaks = prepared.breakableFitAdvances[2];
      const width = prefixWidth + wordBreaks[0] + wordBreaks[1] + 0.1;
      const batched = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(batched.lines[0]?.text).toBe("foo ");
      (0, import_bun_test.expect)(batched.lines[1]?.text.startsWith("ab")).toBe(true);
      const streamed = layoutNextLine2(prepared, { segmentIndex: 0, graphemeIndex: 0 }, width);
      (0, import_bun_test.expect)(streamed?.text).toBe("foo ");
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT).lineCount).toBe(batched.lineCount);
    });
    (0, import_bun_test.test)("mixed CJK-plus-numeric runs use cumulative widths when breaking the numeric suffix", () => {
      const prepared = prepareWithSegments2("\u4E2D\u658711111111111111111", FONT);
      const width = measureWidth("11111", FONT) + 0.1;
      (0, import_bun_test.expect)(prepared.segments).toEqual(["\u4E2D", "\u6587", "11111111111111111"]);
      const batched = layoutWithLines2(prepared, width, LINE_HEIGHT);
      (0, import_bun_test.expect)(batched.lines.map((line) => line.text)).toEqual([
        "\u4E2D\u6587",
        "11111",
        "11111",
        "11111",
        "11"
      ]);
      const streamed = collectStreamedLines(prepared, width);
      (0, import_bun_test.expect)(streamed).toEqual(batched.lines);
      (0, import_bun_test.expect)(layout2(prepared, width, LINE_HEIGHT)).toEqual({ lineCount: 5, height: LINE_HEIGHT * 5 });
    });
    (0, import_bun_test.test)("keep-all suppresses ordinary CJK intra-word breaks after existing line content", () => {
      const text = "A \u4E2D\u6587\u6D4B\u8BD5";
      const normal = prepareWithSegments2(text, FONT);
      const keepAll = prepareWithSegments2(text, FONT, { wordBreak: "keep-all" });
      const width = measureWidth("A \u4E2D", FONT) + 0.1;
      (0, import_bun_test.expect)(layoutWithLines2(normal, width, LINE_HEIGHT).lines[0]?.text).toBe("A \u4E2D");
      (0, import_bun_test.expect)(layoutWithLines2(keepAll, width, LINE_HEIGHT).lines[0]?.text).toBe("A ");
      (0, import_bun_test.expect)(layout2(keepAll, width, LINE_HEIGHT).lineCount).toBeGreaterThan(layout2(normal, width, LINE_HEIGHT).lineCount);
    });
    (0, import_bun_test.test)("keep-all lets mixed no-space CJK runs break through the script boundary", () => {
      const text = "\u65E5\u672C\u8A9Efoo-bar";
      const normal = prepareWithSegments2(text, FONT);
      const keepAll = prepareWithSegments2(text, FONT, { wordBreak: "keep-all" });
      const width = measureWidth("\u65E5\u672C\u8A9Ef", FONT) + 0.1;
      (0, import_bun_test.expect)(layoutWithLines2(normal, width, LINE_HEIGHT).lines[0]?.text).toBe("\u65E5\u672C\u8A9E");
      (0, import_bun_test.expect)(layoutWithLines2(keepAll, width, LINE_HEIGHT).lines[0]?.text).toBe("\u65E5\u672C\u8A9Ef");
    });
    (0, import_bun_test.test)("walkLineRanges reproduces layoutWithLines geometry without materializing text", () => {
      const prepared = prepareWithSegments2('foo trans\xADatlantic said "hello" to \u4E16\u754C and waved.', FONT);
      const width = prepared.widths[0] + prepared.widths[1] + prepared.widths[2] + prepared.breakableFitAdvances[4][0] + prepared.discretionaryHyphenWidth + 0.1;
      const expected = layoutWithLines2(prepared, width, LINE_HEIGHT);
      const actual = [];
      const lineCount = walkLineRanges2(prepared, width, (line) => {
        actual.push({
          width: line.width,
          start: { ...line.start },
          end: { ...line.end }
        });
      });
      (0, import_bun_test.expect)(lineCount).toBe(expected.lineCount);
      (0, import_bun_test.expect)(actual).toEqual(expected.lines.map((line) => ({
        width: line.width,
        start: line.start,
        end: line.end
      })));
    });
    (0, import_bun_test.test)("measureLineStats matches walked line count and widest line", () => {
      const prepared = prepareWithSegments2('foo trans\xADatlantic said "hello" to \u4E16\u754C and waved.', FONT);
      const width = prepared.widths[0] + prepared.widths[1] + prepared.widths[2] + prepared.breakableFitAdvances[4][0] + prepared.discretionaryHyphenWidth + 0.1;
      let walkedLineCount = 0;
      let walkedMaxLineWidth = 0;
      walkLineRanges2(prepared, width, (line) => {
        walkedLineCount++;
        walkedMaxLineWidth = Math.max(walkedMaxLineWidth, line.width);
      });
      (0, import_bun_test.expect)(measureLineStats2(prepared, width)).toEqual({
        lineCount: walkedLineCount,
        maxLineWidth: walkedMaxLineWidth
      });
    });
    (0, import_bun_test.test)("line-break geometry helpers stay aligned with streamed line ranges", () => {
      const prepared = prepareWithSegments2('foo trans\xADatlantic said "hello" to \u4E16\u754C and waved.', FONT);
      const widths = [48, 72, 120];
      for (let index = 0; index < widths.length; index++) {
        const width = widths[index];
        const cursor = { segmentIndex: 0, graphemeIndex: 0 };
        const streamedWidths = [];
        while (true) {
          const line = layoutNextLineRange3(prepared, cursor, width);
          const geometryCursor = { ...cursor };
          const geometryWidth = stepPreparedLineGeometry2(prepared, geometryCursor, width);
          (0, import_bun_test.expect)(geometryWidth).toBe(line?.width ?? null);
          if (line === null) break;
          (0, import_bun_test.expect)(geometryCursor).toEqual(line.end);
          streamedWidths.push(line.width);
          cursor.segmentIndex = line.end.segmentIndex;
          cursor.graphemeIndex = line.end.graphemeIndex;
        }
        (0, import_bun_test.expect)(measurePreparedLineGeometry2(prepared, width)).toEqual({
          lineCount: streamedWidths.length,
          maxLineWidth: Math.max(0, ...streamedWidths)
        });
      }
    });
    (0, import_bun_test.test)("countPreparedLines stays aligned with the walked line counter", () => {
      const texts = [
        "The quick brown fox jumps over the lazy dog.",
        'said "hello" to \u4E16\u754C and waved.',
        "\u0645\u0631\u062D\u0628\u0627\u060C \u0639\u0627\u0644\u0645\u061F",
        "author 7:00-9:00 only",
        "alpha\u200Bbeta gamma"
      ];
      const widths = [40, 80, 120, 200];
      for (let textIndex = 0; textIndex < texts.length; textIndex++) {
        const prepared = prepareWithSegments2(texts[textIndex], FONT);
        for (let widthIndex = 0; widthIndex < widths.length; widthIndex++) {
          const width = widths[widthIndex];
          const counted = countPreparedLines2(prepared, width);
          const walked = walkPreparedLines2(prepared, width);
          (0, import_bun_test.expect)(counted).toBe(walked);
        }
      }
    });
  });
})();
// Test runner epilogue — synchronous execution
// Run beforeAll
for (var __ba_i = 0; __ba_i < __test_beforeAll_fns.length; __ba_i++) {
  __test_beforeAll_fns[__ba_i]();
}

var __run_total = 0;
var __run_passed = 0;
var __run_failed = 0;
var __run_errors = [];

for (var __d = 0; __d < __test_describes.length; __d++) {
  var __desc = __test_describes[__d];
  for (var __t = 0; __t < __desc.tests.length; __t++) {
    var __tc = __desc.tests[__t];
    __run_total++;
    // Run beforeEach
    for (var __b = 0; __b < __test_beforeEach_fns.length; __b++) {
      __test_beforeEach_fns[__b]();
    }
    try {
      __tc.fn();
      __run_passed++;
    } catch(__e) {
      __run_failed++;
      __run_errors.push(__desc.name + " > " + __tc.name + ": " + (__e.message || __e));
    }
  }
}

// Only print summary
console.log("Pretext upstream tests: " + __run_passed + " passed, " + __run_failed + " failed out of " + __run_total);
