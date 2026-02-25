/// <reference types="tree-sitter-cli/dsl" />
// Tree-sitter Grammar for Math Mode (LaTeX + ASCII Math)
//
// Unified grammar that parses both LaTeX math and ASCII math.
// Produces a CST that the MathASTBuilder interprets per-flavor.
//
// Design principles:
// 1. Grammar is close to a tokenizer — classifies tokens and captures structure
// 2. Leave symbol/command semantics to the AST builder (flavor-aware)
// 3. Handle structural elements (fractions, radicals, scripts) precisely
// 4. Use regex patterns to consolidate keyword groups and reduce parser size

module.exports = grammar({
  name: 'latex_math',

  // Whitespace is NOT significant in math mode (unlike text mode)
  extras: $ => [/\s+/],

  // The word token - tells tree-sitter that command_name is the keyword token
  // This allows string literals like '\\begin' to take precedence over command_name regex
  word: $ => $.command_name,

  // Conflicts for ambiguous structures
  conflicts: $ => [
    // textstyle_command arg can be text_group or group (both start with {})
    [$.group, $.text_group],
  ],

  rules: {
    // ========================================================================
    // Entry point
    // ========================================================================

    // Math can be a sequence of expressions OR an infix fraction at top level
    math: $ => choice($.infix_frac, repeat($._expression)),

    // ========================================================================
    // Expressions
    // ========================================================================

    // Note: subsup is tried first because it's more specific (atom + scripts)
    // If there's no script, it falls back to just _atom
    _expression: $ => choice(
      $.subsup,
      $._atom,
    ),

    // ========================================================================
    // Atoms - the basic building blocks
    // ========================================================================

    _atom: $ => choice(
      $.symbol,              // [a-zA-Z@]
      $.word,                // bare multi-letter identifier (ASCII math)
      $.number,              // digits
      $.operator,            // +, -, *, /
      $.relation,            // =, <, >, !, \uparrow, \downarrow
      $.ascii_operator,      // <=, ->, xx, +-, etc. (ASCII math)
      $.punctuation,         // , ; : . ( ) [ ] | ...
      $.quoted_text,         // "quoted text" (ASCII math)
      $.group,               // { ... }
      $.frac_like,           // \frac, \binom, \genfrac     (MERGED)
      $.radical,
      $.delimiter_group,
      $.sized_delimiter,     // \big, \Big, ...
      $.accent,
      $.matrix_command,      // \matrix{...}
      $.environment,
      $.textstyle_command,   // \text, \mathrm, \displaystyle (MERGED)
      $.spacing_command,     // \, \quad \hspace \kern         (MERGED)
      $.command,             // fallback (handles \sum, \overset, \phantom, etc.)
    ),

    // ========================================================================
    // Regex-based command tokens (consolidated to reduce parser size)
    // Each category uses a single regex token instead of many string literals.
    // The runtime reads the actual text content to determine the specific command.
    // ========================================================================

    // NOTE: Symbol commands (\infty, \exists, etc.), operator commands (\pm, \times, etc.),
    // relation commands (\leq, \rightarrow, etc.), and big operators (\sum, \int, etc.)
    // are handled by the generic command_name fallback. The AST builder uses table lookups
    // to determine the correct atom type (ORD, BIN, REL, OP) from the command text.

    // Delimiter commands: \lbrace, \rbrace, \langle, \rangle, \vert, etc.
    // Note: \uparrow etc. kept as string literals (shared with relation rule)
    _delimiter_cmd: $ => token(/\\(lmoustache|rmoustache|backslash|langle|rangle|lfloor|rfloor|lbrace|rbrace|lbrack|rbrack|lgroup|rgroup|lceil|rceil|lvert|rvert|lVert|rVert|vert|Vert)/),

    // Accent commands: \hat, \bar, \vec, \widehat, \overline, etc.
    _accent_cmd: $ => token(/\\(overleftrightarrow|underleftrightarrow|overrightarrow|underrightarrow|overleftarrow|underleftarrow|widetilde|underbrace|underline|overbrace|overline|widehat|ddddot|dddot|acute|breve|check|grave|tilde|ddot|dot|hat|bar|vec)/),

    // Sized delimiter commands: \big, \Big, \bigl, \Bigl, etc.
    _sized_delim_cmd: $ => token(/\\(biggl|Biggl|biggr|Biggr|biggm|Biggm|bigg|Bigg|bigl|Bigl|bigr|Bigr|bigm|Bigm|big|Big)/),

    // MERGED: style + text commands → textstyle
    _textstyle_cmd: $ => token(/\\(scriptscriptstyle|operatorname|displaystyle|mathnormal|scriptstyle|textstyle|mathfrak|mathscr|mathcal|mathrm|mathit|mathbf|mathsf|mathtt|mathbb|textrm|textit|textbf|textsf|texttt|text|mbox|hbox)/),

    // MERGED: frac + binom + genfrac → frac_like
    _frac_like_cmd: $ => token(/\\([dtc]?(frac|binom)|genfrac)/),

    // Matrix commands (plain TeX): \matrix, \pmatrix, \bordermatrix
    _matrix_cmd: $ => token(/\\(bordermatrix|pmatrix|matrix)/),

    // MERGED: spacing commands (space + hspace + skip/kern)
    _spacing_cmd: $ => token(/\\([,:;!]|qquad|quad|hspace\*?|hskip|kern|mskip|mkern)/),

    // Environment names (inside \begin{...} and \end{...})
    // Note: these don't start with \ so they don't conflict with command_name
    _env_name_token: $ => token(/smallmatrix|equation\*?|multline\*?|subarray|gathered|Vmatrix|Bmatrix|bmatrix|vmatrix|pmatrix|aligned|matrix|gather\*?|align\*?|rcases|dcases|split|cases|array/),

    // Vertical arrow commands shared between relation and delimiter rules
    _updown_arrow_cmd: $ => token(/\\(updownarrow|Updownarrow|downarrow|Downarrow|uparrow|Uparrow)/),

    // Infix fraction commands: \over, \atop, \above, \choose, \brace, \brack
    _infix_frac_cmd: $ => token(/\\(choose|above|brack|brace|over|atop)/),

    // Limits modifier: \limits, \nolimits
    _limits_mod: $ => token(/\\(no)?limits/),

    // NEW: Multi-character ASCII operator regex token
    _ascii_multi_op: $ => token(prec(2, /<=|>=|!=|-=|~=|~~|->|<->|<=>|\|->|=>|<-|xx|-:|\.\.|\+-|-\+|\*\*/)),

    // ========================================================================
    // Atoms using the consolidated tokens
    // ========================================================================

    // Single letter variable (a-z, A-Z, Greek via commands, @ symbol)
    symbol: $ => /[a-zA-Z@]/,

    // NEW: Multi-letter bare word (ASCII math identifiers like sin, alpha, sqrt)
    word: $ => token(prec(-2, /[a-zA-Z]{2,}/)),

    // Numeric literal - lower precedence so 'digit' can match first in _frac_arg
    number: $ => token(prec(-1, /[0-9]+\.?[0-9]*/)),

    // Binary operators: +, -, *, / (command operators like \pm handled by generic command)
    operator: $ => choice('+', '-', '*', '/'),

    // Relations: =, <, >, ! and vertical arrows (shared with delimiter rule)
    // Command relations like \leq, \rightarrow handled by generic command
    relation: $ => choice(
      '=', '<', '>', '!',
      $._updown_arrow_cmd,
    ),

    // NEW: ASCII multi-character operators/relations
    ascii_operator: $ => $._ascii_multi_op,

    // Punctuation (including standalone delimiters)
    punctuation: $ => prec(-1, choice(
      ',', ';', ':', '.', '?',
      '(', ')',
      '[', ']',
      '|',
      '\\{', '\\}',
      '\'',
    )),

    // NEW: Quoted text for ASCII math
    quoted_text: $ => seq('"', /[^"]*/, '"'),

    // ========================================================================
    // Groups
    // ========================================================================

    // Curly brace group - may contain infix fraction command
    group: $ => seq('{', choice($.infix_frac, repeat($._expression)), '}'),

    // Bracket group (for optional arguments)
    brack_group: $ => seq('[', repeat($._expression), ']'),

    // Infix fraction commands: x \over y, n \choose k, etc.
    infix_frac: $ => seq(
      field('numer', repeat1($._expression)),
      field('cmd', $._infix_frac_cmd),
      field('denom', repeat($._expression)),
    ),

    // ========================================================================
    // Sub/Superscript (TeXBook Rules 18)
    // ========================================================================

    // Limits modifier: \limits forces above/below, \nolimits forces inline scripts
    limits_modifier: $ => $._limits_mod,

    subsup: $ => prec.right(10, seq(
      field('base', $._atom),
      optional(field('modifier', $.limits_modifier)),
      choice(
        seq('_', field('sub', $._script_arg),
            optional(field('sup_modifier', $.limits_modifier)),
            '^', field('sup', $._script_arg)),
        seq('^', field('sup', $._script_arg),
            optional(field('sub_modifier', $.limits_modifier)),
            '_', field('sub', $._script_arg)),
        seq('_', field('sub', $._script_arg)),
        seq('^', field('sup', $._script_arg)),
      ),
    )),

    _script_arg: $ => choice(
      $.group,
      $.paren_script,   // NEW: (...) for ASCII-style bounds
      $.symbol,
      $.number,
      $.word,            // NEW: bare words in script position
      $.command,
    ),

    // NEW: Parenthesized script argument (for ASCII math: sum_(i=0))
    paren_script: $ => seq('(', repeat($._expression), ')'),

    // ========================================================================
    // Fractions — MERGED: fraction + binomial + genfrac → frac_like
    // ========================================================================

    frac_like: $ => prec.right(seq(
      field('cmd', $._frac_like_cmd),
      repeat1(field('arg', $._frac_arg)),
    )),

    _frac_arg: $ => choice(
      $.group,
      $.symbol,
      $.digit,
      $.command,
    ),

    // Single digit (for \frac57 style) - higher precedence than number
    digit: $ => token(prec(1, /[0-9]/)),

    // ========================================================================
    // Radicals (Square roots, etc.)
    // ========================================================================

    radical: $ => seq(
      '\\sqrt',
      optional(field('index', $.brack_group)),
      field('radicand', $._frac_arg),
    ),

    // ========================================================================
    // Delimiters: \left( ... \right)
    // ========================================================================

    delimiter_group: $ => prec(10, seq(
      '\\left', field('left_delim', $.delimiter),
      repeat(choice($._expression, $.middle_delim)),
      '\\right', field('right_delim', $.delimiter),
    )),

    middle_delim: $ => seq(
      '\\middle',
      field('delim', $.delimiter),
    ),

    // Sized delimiters: \big, \Big, \bigg, \Bigg with optional delimiter
    sized_delimiter: $ => prec.right(1, seq(
      field('size', $._sized_delim_cmd),
      optional(field('delim', $.delimiter)),
    )),

    // Delimiter token: collapsed ASCII delimiters into regex + command tokens
    _delim_char: $ => token(/[()\[\]|.]|\\[{}|]/),

    delimiter: $ => choice(
      $._delim_char,
      $._delimiter_cmd,
      $._updown_arrow_cmd,
    ),

    // ========================================================================
    // Accents
    // ========================================================================

    accent: $ => prec.right(1, seq(
      field('cmd', $._accent_cmd),
      optional(field('base', choice($.group, $.symbol))),
    )),

    // ========================================================================
    // Environments: \begin{...} ... \end{...}
    // ========================================================================

    environment: $ => seq(
      $.begin_cmd, '{', field('name', $.env_name), '}',
      optional(field('columns', $.env_columns)),
      field('body', $.env_body),
      $.end_cmd, '{', field('end_name', $.env_name), '}',
    ),

    begin_cmd: $ => '\\begin',
    end_cmd: $ => '\\end',

    // Environment name - consolidated into single regex token
    env_name: $ => $._env_name_token,

    // Column spec for arrays: {ccc} or {|c|c|c|} - simplified to raw text
    env_columns: $ => seq('{', /[^{}]+/, '}'),

    // Environment body
    env_body: $ => repeat1(choice(
      $._expression,
      $.row_sep,
      $.col_sep,
    )),

    row_sep: $ => prec.right(choice(
      seq('\\\\', optional(field('spacing', $.row_spacing))),
      '\\cr'
    )),
    row_spacing: $ => seq('[', /[^\]]+/, ']'),
    col_sep: $ => '&',

    // Plain TeX \matrix{...} command
    matrix_command: $ => seq(
      field('cmd', $._matrix_cmd),
      '{', optional(field('body', $.matrix_body)), '}',
    ),

    matrix_body: $ => repeat1(choice(
      $._expression,
      $.row_sep,
      $.col_sep,
    )),

    // ========================================================================
    // Text/Style Commands — MERGED: text_command + style_command
    // ========================================================================

    textstyle_command: $ => prec.right(seq(
      field('cmd', $._textstyle_cmd),
      optional(field('arg', choice(prec(1, $.text_group), $.group))),
    )),

    text_group: $ => seq('{', optional($.text_content), '}'),
    text_content: $ => /[^{}]+/,

    // ========================================================================
    // Style commands
    // ========================================================================

    // (merged into textstyle_command above)

    // NOTE: \mathop, \phantom, \fbox, \boxed, \color, \textcolor, \rule, \overset,
    // \xrightarrow, etc. are handled by the generic command fallback.
    // The AST builder dispatches them by checking the command name.

    // ========================================================================
    // Spacing Commands — MERGED: space_command + hspace_command + skip_command
    // ========================================================================

    spacing_command: $ => prec.right(seq(
      field('cmd', $._spacing_cmd),
      optional(field('arg', choice($.group, $._dim_value))),
    )),

    // Single token for bare dimensions: "3.5em", "-10pt", "2\fill"
    _dim_value: $ => token(/[+-]?\d+\.?\d*(\\fill|[a-z]{2})/),

    // ========================================================================
    // Generic command (fallback for Greek letters, symbols, etc.)
    // ========================================================================

    command: $ => prec.right(-1, seq(
      field('name', $.command_name),
      repeat(field('arg', choice($.group, $.brack_group))),
    )),

    // Command name: backslash followed by letters
    command_name: $ => token(prec(-1, /\\[a-zA-Z@]+\*?/)),
  },
});
