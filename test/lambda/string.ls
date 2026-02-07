// String handling tests - escape sequences, comments in strings, unicode, etc.

// Test escape sequences
[
  "--- Basic Escape Sequences ---",
  ["newline", "a\nb"],
  ["tab", "a\tb"],
  ["backslash", "a\\b"],
  ["quote", "a\"b"],
  ["slash", "a\/b"],
  
  "--- Comment Characters in Strings ---",
  "/* not a comment */",
  "start /* middle */ end",
  "// also not a comment",
  "*/ end marker",
  "/* start marker",
  
  "--- Unicode Escapes ---",
  ["4-digit", "\u0041\u0042\u0043"],  // ABC
  ["braces", "\u{41}\u{42}\u{43}"],    // ABC
  ["chinese", "\u4E2D\u6587"],         // 中文
  ["emoji", "\u{1F600}"],              // 😀
  
  "--- Mixed ---",
  "path\\to\\file",
  "say \"hello\"",
  "a /* b */ c",
  
  "--- Symbol Escapes ---",
  'sym-quote-\'',
  'sym-backslash-\\',
  
  "--- Data Structures ---",
  {key: "/* ok */"},
  <el attr: "// ok">,
  
  "--- Multi-line ---",
  "Line1
Line2",
  
  "--- Done ---"
]
