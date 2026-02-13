/// <reference types="tree-sitter-cli/dsl" />
// Tree-sitter Grammar for LaTeX Math Mode
//
// This grammar parses LaTeX math content (inside $...$ or $$...$$).
// It produces a detailed syntax tree for math structures like fractions,
// radicals, sub/superscripts, delimiters, and accents.
//
// Design principles:
// 1. Parse structural elements (fractions, radicals, scripts) precisely
// 2. Leave symbol semantic interpretation to runtime (atom types)
// 3. Handle common LaTeX math commands with proper argument binding
// 4. Use regex patterns to consolidate keyword groups and reduce parser size
//    Runtime reads actual text content to determine specific commands.

module.exports = grammar({
  name: 'latex_math',

  // Whitespace is NOT significant in math mode (unlike text mode)
  extras: $ => [/\s+/],

  // The word token - tells tree-sitter that command_name is the keyword token
  // This allows string literals like '\\begin' to take precedence over command_name regex
  word: $ => $.command_name,

  // Conflicts for ambiguous structures
  conflicts: $ => [
    // subsup can attach to any atom
    [$.subsup],
    // command with optional arguments
    [$.command],
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
      $.symbol,
      $.number,
      $.symbol_command,  // Symbol commands that could conflict - must be before operator for \cdots vs \cdot
      $.operator,
      $.relation,
      $.punctuation,
      $.group,              // Braced groups can be atoms (for {}_a style subscripts)
      $.fraction,
      $.binomial,
      $.genfrac,
      $.radical,
      $.delimiter_group,
      $.sized_delimiter,    // \big, \Big, \bigg, \Bigg delimiters
      $.overunder_command,  // \overset, \underset, \stackrel
      $.extensible_arrow,   // \xrightarrow, \xleftarrow
      $.accent,
      $.box_command,        // \bbox, \fbox, \boxed
      $.color_command,      // \textcolor, \color, \colorbox
      $.rule_command,       // \rule with dimensions
      $.phantom_command,    // \phantom, \hphantom, \vphantom, \smash
      $.big_operator,
      $.mathop_command,     // \mathop{...} - custom operator
      $.matrix_command,     // \matrix{...} - plain TeX matrix
      $.environment,
      $.text_command,
      $.style_command,
      $.space_command,
      $.hspace_command,     // \hspace{dim}, \hspace*{dim}
      $.skip_command,       // \hskip, \kern, \mskip with dimensions
      $.command,  // Generic fallback for unknown commands
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

    // Style commands: \mathrm, \mathbb, \displaystyle, etc.
    _style_cmd: $ => token(/\\(scriptscriptstyle|operatorname|displaystyle|mathnormal|scriptstyle|textstyle|mathfrak|mathscr|mathcal|mathrm|mathit|mathbf|mathsf|mathtt|mathbb)/),

    // Extensible arrow commands: \xrightarrow, \xleftarrow, etc.
    _extensible_arrow_cmd: $ => token(/\\(xLeftrightarrow|xleftrightarrow|xhookrightarrow|xhookleftarrow|xRightarrow|xrightarrow|xLeftarrow|xleftarrow|xmapsto)/),

    // Text commands: \text, \textrm, \mbox, etc.
    _text_cmd: $ => token(/\\(textrm|textit|textbf|textsf|texttt|text|mbox|hbox)/),

    // Box commands: \bbox, \fbox, \boxed, \llap, \rlap, etc.
    _box_cmd: $ => token(/\\(mathllap|mathrlap|mathclap|boxed|bbox|fbox|llap|rlap|clap)/),

    // Fraction commands: \frac, \dfrac, \tfrac, \cfrac
    _frac_cmd: $ => token(/\\[dtc]?frac/),

    // Binomial commands: \binom, \dbinom, \tbinom
    _binom_cmd: $ => token(/\\[dt]?binom/),

    // Over/under set commands: \overset, \underset, \stackrel
    _overunder_cmd: $ => token(/\\(overset|underset|stackrel)/),

    // Color commands: \textcolor, \color, \colorbox
    _color_cmd: $ => token(/\\(textcolor|colorbox|color)/),

    // Phantom commands: \phantom, \hphantom, \vphantom, \smash
    _phantom_cmd: $ => token(/\\(hphantom|vphantom|phantom|smash)/),

    // Matrix commands (plain TeX): \matrix, \pmatrix, \bordermatrix
    _matrix_cmd: $ => token(/\\(bordermatrix|pmatrix|matrix)/),

    // Skip/kern commands: \hskip, \kern, \mskip, \mkern
    _skip_cmd: $ => token(/\\(hskip|kern|mskip|mkern)/),

    // Environment names (inside \begin{...} and \end{...})
    // Note: these don't start with \ so they don't conflict with command_name
    _env_name_token: $ => token(/smallmatrix|equation\*?|multline\*?|subarray|gathered|Vmatrix|Bmatrix|bmatrix|vmatrix|pmatrix|aligned|matrix|gather\*?|align\*?|rcases|dcases|split|cases|array/),

    // Vertical arrow commands shared between relation and delimiter rules
    _updown_arrow_cmd: $ => token(/\\(updownarrow|Updownarrow|downarrow|Downarrow|uparrow|Uparrow)/),

    // Infix fraction commands: \over, \atop, \above, \choose, \brace, \brack
    _infix_frac_cmd: $ => token(/\\(choose|above|brack|brace|over|atop)/),

    // Space commands: \, \: \; \! \quad \qquad
    _space_cmd: $ => token(/\\([,:;!]|qquad|quad)/),

    // Limits modifier: \limits, \nolimits
    _limits_mod: $ => token(/\\(no)?limits/),

    // Hspace commands: \hspace, \hspace*
    _hspace_cmd: $ => token(/\\hspace\*?/),

    // ========================================================================
    // Atoms using the consolidated tokens
    // ========================================================================

    // Symbol commands that need higher precedence than operators/relations
    // (e.g., \cdots before \cdot, \infty before \inf, \nexists before \ne)
    symbol_command: $ => $._symbol_cmd,

    // Single letter variable (a-z, A-Z, Greek via commands, @ symbol)
    symbol: $ => /[a-zA-Z@]/,

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

    // Punctuation (including standalone delimiters)
    // Lower precedence than brack_group so optional args parse correctly
    punctuation: $ => prec(-1, choice(
      ',', ';', ':', '.', '?',
      '(', ')',                    // Parentheses
      '[', ']',                    // Square brackets
      '|',                         // Vertical bar (absolute value, divides)
      '\\{', '\\}',                // Escaped braces
      '\'',                        // Prime (for derivatives like f')
    )),

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
      $.symbol,
      $.number,
      $.command,
    ),

    // ========================================================================
    // Fractions (TeXBook Rule 15)
    // ========================================================================

    fraction: $ => seq(
      field('cmd', $._frac_cmd),
      field('numer', $._frac_arg),
      field('denom', $._frac_arg),
    ),

    _frac_arg: $ => choice(
      $.group,
      $.symbol,
      $.digit,
      $.command,
    ),

    // Single digit (for \frac57 style) - higher precedence than number
    digit: $ => token(prec(1, /[0-9]/)),

    // Binomial coefficients
    binomial: $ => seq(
      field('cmd', $._binom_cmd),
      field('top', $._frac_arg),
      field('bottom', $._frac_arg),
    ),

    // \genfrac{left}{right}{thickness}{style}{numer}{denom}
    genfrac: $ => seq(
      '\\genfrac',
      field('left_delim', $.group),
      field('right_delim', $.group),
      field('thickness', $.group),
      field('style', $.group),
      field('numer', $._frac_arg),
      field('denom', $._frac_arg),
    ),

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

    // Delimiter token: ASCII delimiters, special sequences, and command delimiters
    delimiter: $ => choice(
      '(', ')', '[', ']',
      '\\{', '\\}',
      '|', '\\|',
      '.',  // Null delimiter
      $._delimiter_cmd,
      $._updown_arrow_cmd,
    ),

    // ========================================================================
    // Over/Under Set Commands (amsmath)
    // ========================================================================

    overunder_command: $ => seq(
      field('cmd', $._overunder_cmd),
      field('annotation', $.group),
      field('base', $.group),
    ),

    // Extensible arrows with optional annotations
    extensible_arrow: $ => seq(
      field('cmd', $._extensible_arrow_cmd),
      optional(field('below', $.brack_group)),
      field('above', $.group),
    ),

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

    // Column spec for arrays: {ccc} or {|c|c|c|}
    env_columns: $ => seq('{', /[lcr|@{pmb\d.]+/, '}'),

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
    // Text mode in math
    // ========================================================================

    text_command: $ => seq(
      field('cmd', $._text_cmd),
      field('content', $.text_group),
    ),

    text_group: $ => seq('{', optional($.text_content), '}'),
    text_content: $ => /[^{}]+/,

    // ========================================================================
    // Style commands
    // ========================================================================

    style_command: $ => prec.right(seq(
      field('cmd', $._style_cmd),
      optional(field('arg', $.group)),
    )),

    // ========================================================================
    // Mathop command: \mathop{...} - makes content behave like an operator
    // ========================================================================

    mathop_command: $ => seq(
      '\\mathop',
      field('content', $.group),
    ),

    // ========================================================================
    // Box commands: \bbox, \fbox, \boxed
    // ========================================================================

    box_command: $ => seq(
      field('cmd', $._box_cmd),
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
    // Phantom commands: \phantom, \hphantom, \vphantom, \smash
    // ========================================================================

    phantom_command: $ => seq(
      field('cmd', $._phantom_cmd),
      optional(field('options', $.brack_group)),
      field('content', $.group),
    ),

    // ========================================================================
    // Spacing commands
    // ========================================================================

    space_command: $ => $._space_cmd,

    // \hspace{dim} and \hspace*{dim}
    hspace_command: $ => seq(
      field('cmd', $._hspace_cmd),
      '{',
      optional(field('sign', choice('+', '-'))),
      field('value', $.number),
      field('unit', $.dimension_unit),
      '}',
    ),

    // Skip/kern commands with dimensions
    skip_command: $ => seq(
      field('cmd', $._skip_cmd),
      optional(field('sign', choice('+', '-'))),
      field('value', $.number),
      field('unit', $.dimension_unit),
    ),

    // Dimension units for spacing commands
    dimension_unit: $ => choice(
      'pt', 'mm', 'cm', 'in', 'ex', 'em',
      'bp', 'pc', 'dd', 'cc', 'sp', 'mu',
      '\\fill',
    ),

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
