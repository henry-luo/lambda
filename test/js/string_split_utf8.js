// RegExp.prototype[@@split] indexes by UTF-16 code unit, whatever the flags
// say — RegExpExec reports match positions and writes lastIndex in that unit.
// This loop used to size and slice itself in UTF-8 bytes unless `u` was set,
// so its cursors and `e` only agreed for ASCII subjects; on any wider
// character it both sliced mid-character and compared incommensurable
// offsets, and "日本".split(/\d*/) came back as replacement characters.
// Every expectation below is the value Node reports for the same call.
const cases = [
    ['zero-width, CJK',        () => "日本".split(/\d*/),        ["日","本"]],
    ['zero-width, mixed',      () => "日a本".split(/\d*/),       ["日","a","本"]],
    ['zero-width, accented',   () => "héllo".split(/\d*/),      ["h","é","l","l","o"]],
    ['zero-width, never hits', () => "日本".split(/x*/),         ["日","本"]],
    ['empty pattern',          () => "日本語".split(/(?:)/),     ["日","本","語"]],
    ['empty pattern, u flag',  () => "日本".split(/(?:)/u),      ["日","本"]],
    ['empty string separator', () => "日本".split(""),           ["日","本"]],
    ['ascii unaffected',       () => "ab".split(/\d*/),         ["a","b"]],
    ['delimiter is CJK',       () => "a日b".split(/日/),         ["a","b"]],
    ['multibyte delimiter',    () => "日1本".split(/\d/),        ["日","本"]],
    ['capture group kept',     () => "日1本".split(/(\d)/),      ["日","1","本"]],
    ['astral delimiter',       () => "a😀b".split(/😀/),         ["a","b"]],
    ['astral, u flag',         () => "😀b".split(/(?:)/u),       ["😀","b"]],
    // Without `u` a split point falls BETWEEN an astral character's surrogate
    // halves — a position this UTF-8 subject has no byte offset for, so
    // @@split stands in for exec there rather than widening the subject.
    ['astral, no u flag',      () => "😀b".split(/(?:)/),        ["\ud83d","\ude00","b"]],
    ['astral, zero-width',     () => "😀b".split(/x*/),          ["\ud83d","\ude00","b"]],
    ['astral, leading text',   () => "a😀b".split(/(?:)/),       ["a","\ud83d","\ude00","b"]],
    ['astral, two in a row',   () => "😀😀".split(/(?:)/),        ["\ud83d","\ude00","\ud83d","\ude00"]],
    ['astral, captures kept',  () => "😀b".split(/()/),          ["\ud83d","","\ude00","","b"]],
    ['astral, empty sep',      () => "😀b".split(""),            ["\ud83d","\ude00","b"]],
    ['astral, no match',       () => "😀b".split(/\d/),          ["😀b"]],
    ['astral, limit',          () => "😀b".split(/(?:)/, 2),     ["\ud83d","\ude00"]],
    ['string separator',       () => "日,本".split(","),         ["日","本"]],
    ['limit honoured',         () => "日本".split(/\d*/, 1),     ["日"]],
    ['vowel class',            () => "ünïcödé".split(/[aeiou]*/), ["ü","n","ï","c","ö","d","é"]],
];
let failed = 0;
for (const [name, f, want] of cases) {
    const got = JSON.stringify(f());
    const exp = JSON.stringify(want);
    if (got !== exp) { console.log("FAIL " + name + ": got " + got + " want " + exp); failed++; }
}
console.log(failed === 0 ? "split utf-8: all " + cases.length + " cases match Node" : failed + " case(s) diverge");
