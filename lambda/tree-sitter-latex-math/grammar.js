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
      $.symbol_command,      // \infty, \exists, \cdots, ...
      $.operator,            // +, -, *, /, \pm, \times, ...
      $.relation,            // =, <, >, \leq, \rightarrow, ...
      $.ascii_operator,      // <=, ->, xx, +-, etc. (ASCII math)
      $.punctuation,         // , ; : . ( ) [ ] | ...
      $.quoted_text,         // "quoted text" (ASCII math)
      $.group,               // { ... }
      $.frac_like,           // \frac, \binom, \genfrac     (MERGED)
      $.radical,
      $.delimiter_group,
      $.sized_delimiter,     // \big, \Big, ...
      $.annotated_command,   // \overset, \xrightarrow       (MERGED)
      $.accent,
      $.boxlike_command,     // \fbox, \phantom, \smash       (MERGED)
      $.color_command,       // \color, \textcolor
      $.rule_command,        // \rule
      $.big_operator,        // \sum, \int, \lim, ...
      $.mathop_command,      // \mathop{...}
      $.matrix_command,      // \matrix{...}
      $.environment,
      $.textstyle_command,   // \text, \mathrm, \displaystyle (MERGED)
      $.spacing_command,     // \, \quad \hspace \kern         (MERGED)
      $.command,             // fallback
    ),

    // ========================================================================
    // Regex-based command tokens (consolidated to reduce parser size)
    // Each category uses a single regex token instead of many string literals.
    // The runtime reads the actual text content to determine the specific command.
    // ========================================================================

    // Symbol commands: \infty, \exists, \forall, \cdots, \ldots, etc.
    // prec(1) ensures these match before operator/relation/big_operator prefixes
    _symbol_cmd: $ => token(prec(1, /\\(infinity|varnothing|emptyset|nexists|partial|exists|forall|cdots|ldots|vdots|ddots|infty|imath|jmath|nabla|aleph|dotsb|dotsc|dotsi|dotsm|dotso|dots|hbar|ell|Re|Im)/)),

    // Operator commands: \pm, \mp, \times, \cdot, etc.
    _operator_cmd: $ => token(/\\(pm|mp|times|div|cdot|ast|star|circ|bullet|oplus|ominus|otimes|oslash|odot|cup|cap|sqcup|sqcap|vee|wedge|setminus)/),

    // Relation commands (non-arrow): \leq, \geq, \equiv, \subset, etc.
    _relation_cmd: $ => token(/\\(leq|le|geq|ge|neq|ne|equiv|sim|simeq|approx|cong|propto|asymp|subset|supset|subseteq|supseteq|in|ni|notin|ll|gg|prec|succ|preceq|succeq|perp|parallel|mid|vdash|dashv|models)/),

    // Arrow commands: \leftarrow, \rightarrow, \Rightarrow, \mapsto, etc.
    // Note: \uparrow, \downarrow, \updownarrow and their uppercase variants are
    // kept as string literals because they're shared with the delimiter rule.
    _arrow_cmd: $ => token(/\\(leftrightarrows|leftleftarrows|leftrightsquigarrow|longleftrightarrow|Longleftrightarrow|nleftrightarrow|nLeftrightarrow|leftrightharpoons|rightleftharpoons|leftrightarrow|Leftrightarrow|leftharpoondown|longleftarrow|Longleftarrow|leftharpoonup|hookleftarrow|leftarrow|Leftarrow|curvearrowright|curvearrowleft|rightrightarrows|rightharpoondown|circlearrowright|circlearrowleft|looparrowright|looparrowleft|rightharpoonup|hookrightarrow|longrightarrow|Longrightarrow|nRightarrow|nrightarrow|rightarrow|Rightarrow|longmapsto|nearrow|searrow|nwarrow|swarrow|mapsto|gets|to)/),

    // Delimiter commands: \lbrace, \rbrace, \langle, \rangle, \vert, etc.
    // Note: \uparrow etc. kept as string literals (shared with relation rule)
    _delimiter_cmd: $ => token(/\\(lmoustache|rmoustache|backslash|langle|rangle|lfloor|rfloor|lbrace|rbrace|lbrack|rbrack|lgroup|rgroup|lceil|rceil|lvert|rvert|lVert|rVert|vert|Vert)/),

    // Big operator commands: \sum, \prod, \int, \lim, etc.
    _big_operator_cmd: $ => token(/\\(bigsqcup|bigotimes|bigoplus|bigwedge|bigvee|bigcap|bigcup|coprod|liminf|limsup|iiint|iint|oint|prod|sum|lim|max|min|sup|inf|int|det|gcd|Pr)/),

    // Accent commands: \hat, \bar, \vec, \widehat, \overline, etc.
    _accent_cmd: $ => token(/\\(overleftrightarrow|underleftrightarrow|overrightarrow|underrightarrow|overleftarrow|underleftarrow|widetilde|underbrace|underline|overbrace|overline|widehat|ddddot|dddot|acute|breve|check|grave|tilde|ddot|dot|hat|bar|vec)/),

    // Sized delimiter commands: \big, \Big, \bigl, \Bigl, etc.
    _sized_delim_cmd: $ => token(/\\(biggl|Biggl|biggr|Biggr|biggm|Biggm|bigg|Bigg|bigl|Bigl|bigr|Bigr|bigm|Bigm|big|Big)/),

    // MERGED: style + text commands → textstyle
    _textstyle_cmd: $ => token(/\\(scriptscriptstyle|operatorname|displaystyle|mathnormal|scriptstyle|textstyle|mathfrak|mathscr|mathcal|mathrm|mathit|mathbf|mathsf|mathtt|mathbb|textrm|textit|textbf|textsf|texttt|text|mbox|hbox)/),

    // MERGED: extensible arrow + overunder → annotated
    _annotated_cmd: $ => token(/\\(xLeftrightarrow|xleftrightarrow|xhookrightarrow|xhookleftarrow|xRightarrow|xrightarrow|xLeftarrow|xleftarrow|xmapsto|overset|underset|stackrel)/),

    // MERGED: box + phantom → boxlike
    _boxlike_cmd: $ => token(/\\(mathllap|mathrlap|mathclap|boxed|bbox|fbox|llap|rlap|clap|hphantom|vphantom|phantom|smash)/),

    // MERGED: frac + binom + genfrac → frac_like
    _frac_like_cmd: $ => token(/\\([dtc]?(frac|binom)|genfrac)/),

    // Color commands: \textcolor, \color, \colorbox
    _color_cmd: $ => token(/\\(textcolor|colorbox|color)/),

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

    // Symbol commands that need higher precedence than operators/relations
    symbol_command: $ => $._symbol_cmd,

    // Single letter variable (a-z, A-Z, Greek via commands, @ symbol)
    symbol: $ => /[a-zA-Z@]/,

    // NEW: Multi-letter bare word (ASCII math identifiers like sin, alpha, sqrt)
    word: $ => token(prec(-2, /[a-zA-Z]{2,}/)),

    // Numeric literal - lower precedence so 'digit' can match first in _frac_arg
    number: $ => token(prec(-1, /[0-9]+\.?[0-9]*/)),

    // Binary operators: +, -, *, etc. and command operators via regex
    operator: $ => choice(
      '+', '-', '*', '/',
      $._operator_cmd,
    ),

    // Relations: =, <, >, etc. and command relations/arrows via regex
    relation: $ => choice(
      '=', '<', '>', '!',
      $._relation_cmd,
      $._arrow_cmd,
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
    // Annotated Commands — MERGED: overunder_command + extensible_arrow
    // ========================================================================

    annotated_command: $ => prec.right(seq(
      field('cmd', $._annotated_cmd),
      optional(field('opt_arg', $.brack_group)),
      field('first', $.group),
      optional(field('second', $.group)),
    )),

    // ========================================================================
    // Accents
    // ========================================================================

    accent: $ => prec.right(1, seq(
      field('cmd', $._accent_cmd),
      optional(field('base', choice($.group, $.symbol))),
    )),

    // ========================================================================
    // Big Operators (with limits)
    // ========================================================================

    big_operator: $ => prec.right(seq(
      field('op', $._big_operator_cmd),
      optional(choice(
        seq('_', field('lower', $._script_arg), optional(seq('^', field('upper', $._script_arg)))),
        seq('^', field('upper', $._script_arg), optional(seq('_', field('lower', $._script_arg)))),
      )),
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

    // ========================================================================
    // Mathop command: \mathop{...} - makes content behave like an operator
    // ========================================================================

    mathop_command: $ => seq(
      '\\mathop',
      field('content', $.group),
    ),

    // ========================================================================
    // Box-like Commands — MERGED: box_command + phantom_command
    // ========================================================================

    boxlike_command: $ => seq(
      field('cmd', $._boxlike_cmd),
      optional(field('options', $.brack_group)),
      field('content', $.group),
    ),

    // ========================================================================
    // Color commands: \textcolor, \color, \colorbox
    // ========================================================================

    color_command: $ => prec.right(seq(
      field('cmd', $._color_cmd),
      field('color', $.group),
      optional(field('content', $.group)),
    )),

    // ========================================================================
    // Rule command: \rule[raise]{width}{height}
    // ========================================================================

    rule_command: $ => seq(
      '\\rule',
      optional(field('raise', $.brack_group)),
      field('width', $.group),
      field('height', $.group),
    ),

    // ========================================================================
    // Phantom commands (merged into boxlike_command above)
    // ========================================================================

    // (phantom_command merged into boxlike_command)

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
