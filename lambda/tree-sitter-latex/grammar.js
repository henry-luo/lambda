/// <reference types="tree-sitter-cli/dsl" />
// Hybrid LaTeX Tree-sitter Grammar
// Based on LaTeX.js PEG.js structure, optimized for Tree-sitter size
//
// Design principles:
// 1. Match LaTeX.js structure for compatibility
// 2. Use generic command/macro handling (semantic interpretation at runtime)
// 3. Keep specialized rules only where parsing behavior differs
// 4. Use external scanner for verbatim content

module.exports = grammar({
  name: 'latex',

  // Whitespace and comments are NOT extras - they're significant in LaTeX
  // Comments must be visible in the tree so we can handle them properly (they eat newlines)
  extras: $ => [],

  // External scanner for verbatim content that can't be parsed with regex
  // Also handles \begin{document} and \end{document} to take precedence over command
  externals: $ => [
    $._verbatim_content,      // For verbatim, lstlisting environments
    $._comment_env_content,   // For comment environment
    $._begin_document,        // \begin{document} - higher priority than command
    $._end_document,          // \end{document} - higher priority than command
    $._verb_command,          // \verb<delim>text<delim> - must use external to override command_name token
    $._char_command,          // \char<number> - decimal/hex/octal number token
    $._caret_char,            // ^^XX (2 hex) or ^^^^XXXX (4 hex) - TeX caret notation
  ],

  word: $ => $.command_name,

  conflicts: $ => [
    // Environment can appear in both _block and _inline contexts
    [$._block, $._inline],
    // verb_command and command both start with backslash - need GLR to disambiguate
    [$.verb_command, $.command],
    // char_command and command both start with backslash - need GLR to disambiguate
    [$.char_command, $.command],
    // controlspace_command and control_symbol both start with backslash - need GLR
    [$.controlspace_command, $.control_symbol],
    // '[' can be start of brack_group or standalone bracket in _group_content
    [$.brack_group],
  ],

  rules: {
    // ========================================================================
    // Document structure (matches LaTeX.js: latex, document)
    // ========================================================================

    // Top level: sequence of items, some may be preamble, one may be document
    source_file: $ => repeat($._top_level_item),

    // Each top-level item can be either document or a preamble item
    _top_level_item: $ => choice(
      $.document,       // Full document with \begin{document}...\end{document}
      $.environment,    // Environments can appear at top level (e.g., in fragments)
      $.verb_command,   // \verb can appear at top level
      $.char_command,   // \char can appear at top level
      $.caret_char,     // ^^XX or ^^^^XXXX - TeX caret notation
      $.linebreak_command, // \\ with optional [<length>]
      $.control_symbol, // Escape sequences like \%, \&
      $.command,        // Commands can appear at top level (e.g., \textellipsis)
      $.curly_group,    // Curly braces can appear at top level
      $.brack_group,    // Restored: [ ... ] bracket groups at top level
      $.space,
      $.text,
      $.paragraph_break,  // Blank lines are allowed in preamble
      $.nbsp,           // Non-breaking space ~
    ),

    document: $ => seq(
      $.begin_document,
      repeat($._block),
      $.end_document,
    ),

    // Use external scanner to detect these as single tokens before command_name lexer
    begin_document: $ => $._begin_document,
    end_document: $ => $._end_document,

    // ========================================================================
    // Block-level content (matches LaTeX.js: paragraph, vmode_macro)
    // ========================================================================

    _block: $ => choice(
      $.paragraph_break,
      $.environment,
      $.section,
      $.paragraph,
    ),

    paragraph: $ => prec.right(repeat1($._inline)),

    paragraph_break: $ => token(prec(1, /\n[ \t]*\n/)),

    // ========================================================================
    // Sections (matches LaTeX.js section handling via macros)
    // Sections are NOT containers - they just produce heading output.
    // Content after a section is at the same level (siblings, not children).
    // ========================================================================

    section: $ => seq(
      field('command', $.section_command),
      optional(field('toc', $.brack_group)),     // [short title]
      field('title', $.curly_group),             // {title}
    ),

    section_command: $ => token(/\\(part|chapter|section|subsection|subsubsection|paragraph|subparagraph)\*?/),

    // ========================================================================
    // Inline content (matches LaTeX.js: text, primitive, hmode_macro)
    // ========================================================================

    _inline: $ => choice(
      $.controlspace_command, // \<space>, \<tab>, \<newline> - MUST BE FIRST to catch \<newline>
      $.text,
      $.space,
      $.paragraph_break, // Paragraph break (double newline) - must be checked as inline too
      $.line_comment,   // Comments can appear inline
      $.environment,    // Environments can appear inline (they interrupt paragraphs)
      $.verb_command,   // \verb|text| - must be before command (context-gated)
      $.char_command,   // \char<number> - must be before command (context-gated)
      $.caret_char,     // ^^XX or ^^^^XXXX - TeX caret notation
      $.linebreak_command, // \\ with optional [<length>]
      $.command,
      $.curly_group,
      $.brack_group,    // Restored: [ ... ] is an optional argument group
      $.math,
      $.ligature,
      $.control_symbol,
      $.placeholder,    // #1, #2, etc. in macro definitions
      $.nbsp,           // Non-breaking space ~
      $.alignment_tab,  // & for tabular cell separator
    ),

    // Text is everything that's not a special character
    // NOTE: ^ is excluded for caret notation (^^XX), _ is allowed (only special in math mode)
    text: $ => /[^\\{}$%\[\]\n~&#^]+/,

    // Space: horizontal whitespace and single newlines only
    // Paragraph break (higher precedence) will match \n\n sequences
    space: $ => /[ \t]+|\n/,

    line_comment: $ => /%[^\n]*/,

    // Placeholder for macro arguments (#1, #2, ..., #9)
    placeholder: $ => /#[1-9]/,

    // Ligatures (matches LaTeX.js: ligature)
    ligature: $ => choice('---', '--', '``', "''", '<<', '>>'),

    // Control space command: \<space>, \<tab>, \<newline> handled by external scanner
    // Control space: backslash followed by whitespace
    // LaTeX.js: ctrl_space = escape (&nl &break / nl / sp)
    // This is a PARSER rule, not a lexer token, so it can match sequences
    controlspace_command: $ => prec(3, seq(
      '\\',
      choice(
        ' ',        // Backslash-space
        '\t',       // Backslash-tab
        '\n',       // Backslash-newline (Unix)
        '\r\n',     // Backslash-CRLF (Windows)
        '\r'        // Backslash-CR (old Mac)
      )
    )),

    // Control symbols (matches LaTeX.js: ctrl_sym)
    // High precedence to match before line_comment sees the %
    // Includes: escape chars ($%#&{}_-), spacing (\! \, \; \: \/ \@),
    // punctuation (\. \' \` \^ \" \~ \=), control space (\ )
    // Note: \<tab> and \<newline> are handled by external scanner (controlspace_command)
    // Note: line break (\\) is handled by linebreak_command to capture optional [<length>]
    control_symbol: $ => token(prec(2, seq('\\', /[$%#&{}_\-,\/@ !;:.'`^"~=]/))),

    // ========================================================================
    // Line break command - \\ with optional [<length>] argument
    // ========================================================================

    // Matches: \\, \\*, \\[<length>], \\*[<length>]
    // LaTeX.js: nl = escape escape_char opt_star opt_length
    // Use prec.right to greedily consume optional brack_group
    linebreak_command: $ => prec.right(seq(
      '\\\\',
      optional('*'),
      optional($.brack_group)
    )),

    // ========================================================================
    // Verb command - inline verbatim with arbitrary delimiter
    // ========================================================================

    // External scanner token - required because command_name token would match \verb first
    // The external scanner runs before tokenization and can claim \verb pattern
    verb_command: $ => $._verb_command,

    // ========================================================================
    // Char command - TeX character code with decimal/hex/octal number
    // ========================================================================

    // Context-gated external token (Pattern 2)
    // The external scanner will only emit this token when valid_symbols[CHAR_COMMAND] is true
    // Matches: \char<decimal>, \char"<hex>, \char'<octal>
    char_command: $ => $._char_command,

    // TeX caret notation for character input
    // Matches: ^^XX (2 hex digits) or ^^^^XXXX (4 hex digits) or ^^c (char +/- 64)
    caret_char: $ => $._caret_char,

    // ========================================================================
    // Commands/Macros (matches LaTeX.js: macro, macro_args)
    // ========================================================================

    // Generic command: \name followed by optional arguments
    // LaTeX.js determines argument types dynamically; we parse generically
    command: $ => prec.right(seq(
      field('name', $.command_name),
      repeat(field('arg', choice(
        $.curly_group,
        $.brack_group,
        $.star,
      ))),
    )),

    command_name: $ => /\\[@a-zA-Z]+\*?/,

    star: $ => '*',

    // ========================================================================
    // Groups (matches LaTeX.js: arg_group, opt_group, begin_group/end_group)
    // ========================================================================

    curly_group: $ => seq(
      '{',
      repeat($._group_content),
      '}',
    ),

    brack_group: $ => prec(1, seq(
      '[',
      repeat($._group_content),
      ']',
    )),

    _group_content: $ => choice(
      $.text,
      $.space,
      $.paragraph_break,  // Blank lines inside groups also create paragraph breaks
      $.line_comment,     // Comments can appear inside groups (e.g., [% comment\n text])
      $.linebreak_command, // \\ with optional [<length>] - must be before command
      $.verb_command,   // \verb|text| - must be before command to get correct token
      $.command,
      $.curly_group,
      $.brack_group,
      $.math,
      $.ligature,
      $.control_symbol,
      $.placeholder,  // #1, #2, etc. in macro definitions
      $.nbsp,         // Non-breaking space ~
      ',',   // Common in group content
      '=',   // For key=value
      ']',   // Standalone closing bracket (not part of brack_group)
      prec(-1, '['),   // Standalone opening bracket - lower priority than brack_group
    ),

    // ========================================================================
    // Math (matches LaTeX.js: math, inline_math, display_math)
    // ========================================================================

    math: $ => choice(
      $.display_math,  // Try display_math first (longer match)
      $.inline_math,
    ),

    inline_math: $ => choice(
      seq(token(prec(-1, '$')), repeat($._math_content), '$'),
      seq('\\(', repeat($._math_content), '\\)'),
    ),

    // Display math: use token(prec(1, '$$')) to ensure $$ is tokenized as a single lexeme
    // before $ can be matched individually by inline_math
    display_math: $ => choice(
      seq(token(prec(1, '$$')), repeat($._math_content), token(prec(1, '$$'))),
      seq('\\[', repeat($._math_content), '\\]'),
    ),

    _math_content: $ => choice(
      $.math_text,
      $.command,
      $.control_symbol,   // Spacing commands like \, \; \! etc.
      $.curly_group,
      $.subscript,
      $.superscript,
      /[&|]/,  // alignment and other special chars in math
    ),

    math_text: $ => /[^\\{}$^_&|]+/,

    subscript: $ => seq('_', choice($.curly_group, $.math_single_char)),
    superscript: $ => seq('^', choice($.curly_group, $.math_single_char)),

    // Named rule for single character in sub/superscript (so it appears in tree)
    math_single_char: $ => /[a-zA-Z0-9]/,

    // ========================================================================
    // Environments (matches LaTeX.js: environment, begin_env, end_env)
    // ========================================================================

    environment: $ => choice(
      $.generic_environment,     // All environments are generic for simplicity
      // Math environment handling moved to converter (too complex for grammar)
      // $.math_environment,
      // DISABLED: verbatim/comment external scanning causes GLR issues
      // $.verbatim_environment,
      // $.comment_environment,  // External scanner interferes with line_comment parsing
    ),

    // Generic environment for most cases
    generic_environment: $ => seq(
      field('begin', $.begin_env),
      repeat($._block),
      field('end', $.end_env),
    ),

    begin_env: $ => prec.right(seq(
      '\\begin',
      field('name', $.curly_group),
      optional(field('options', $.brack_group)),
      repeat(field('arg', $.curly_group)),
    )),

    end_env: $ => seq(
      '\\end',
      field('name', $.curly_group),
    ),

    // Verbatim environment - uses external scanner
    // Matches LaTeX.js: special handling needed because content is not parsed
    verbatim_environment: $ => seq(
      field('begin', alias($._verbatim_begin, $.begin_env)),
      field('content', alias($._verbatim_content, $.verbatim)),
      field('end', alias($._verbatim_end, $.end_env)),
    ),

    _verbatim_begin: $ => seq(
      '\\begin',
      '{',
      field('name', alias(/verbatim|lstlisting|minted/, $.env_name)),
      '}',
      optional($.brack_group),
      optional($.curly_group),
    ),

    _verbatim_end: $ => seq(
      '\\end',
      '{',
      /verbatim|lstlisting|minted/,
      '}',
    ),

    // Comment environment - content is completely ignored
    // Matches LaTeX.js: comment_env
    comment_environment: $ => seq(
      '\\begin', '{', 'comment', '}',
      optional($._comment_env_content),
      '\\end', '{', 'comment', '}',
    ),

    // Math environment (equation, align, etc.)
    // These contain math content, not regular text
    math_environment: $ => seq(
      field('begin', $.math_env_begin),
      repeat($._math_content),
      field('end', $.math_env_end),
    ),

    math_env_begin: $ => seq(
      '\\begin',
      '{',
      field('name', alias($._math_env_name, $.env_name)),
      '}',
    ),

    math_env_end: $ => seq(
      '\\end',
      '{',
      $._math_env_name,
      '}',
    ),

    _math_env_name: $ => /equation\*?|align\*?|gather\*?|multline\*?|eqnarray\*?|array|matrix|pmatrix|bmatrix|vmatrix|Vmatrix|cases/,

    // ========================================================================
    // Special tokens
    // ========================================================================

    // Non-breaking space
    nbsp: $ => '~',

    // Alignment tab
    alignment_tab: $ => '&',
  },
});
