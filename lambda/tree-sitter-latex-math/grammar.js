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

    math: $ => repeat($._expression),

    // ========================================================================
    // Expressions
    // ========================================================================

    _expression: $ => choice(
      $._atom,
      $.group,
      $.subsup,
    ),

    // ========================================================================
    // Atoms - the basic building blocks
    // ========================================================================

    _atom: $ => choice(
      $.symbol,
      $.number,
      $.operator,
      $.relation,
      $.punctuation,
      $.fraction,
      $.binomial,
      $.radical,
      $.delimiter_group,
      $.sized_delimiter,    // \big, \Big, \bigg, \Bigg delimiters
      $.overunder_command,  // \overset, \underset, \stackrel
      $.extensible_arrow,   // \xrightarrow, \xleftarrow
      $.accent,
      $.box_command,        // \bbox, \fbox, \boxed
      $.rule_command,       // \rule with dimensions
      $.phantom_command,    // \phantom, \hphantom, \vphantom, \smash
      $.symbol_command,  // Symbol commands like \infty - before big_operator
      $.big_operator,
      $.environment,
      $.text_command,
      $.style_command,
      $.space_command,
      $.command,  // Generic fallback for unknown commands
    ),

    // Symbol commands that could conflict with big operator prefixes
    // Must be listed before big_operator to take precedence
    symbol_command: $ => choice(
      '\\infty',   // Must match before \inf (big_operator)
      '\\infinity',
    ),

    // Single letter variable (a-z, A-Z, Greek via commands)
    symbol: $ => /[a-zA-Z]/,

    // Numeric literal - lower precedence so 'digit' can match first in _frac_arg
    number: $ => token(prec(-1, /[0-9]+\.?[0-9]*/)),

    // Binary operators: +, -, *, etc.
    operator: $ => choice(
      '+', '-', '*', '/',
      '\\pm', '\\mp', '\\times', '\\div', '\\cdot',
      '\\ast', '\\star', '\\circ', '\\bullet',
      '\\oplus', '\\ominus', '\\otimes', '\\oslash', '\\odot',
      '\\cup', '\\cap', '\\sqcup', '\\sqcap',
      '\\vee', '\\wedge', '\\setminus',
    ),

    // Relations: =, <, >, etc.
    relation: $ => choice(
      '=', '<', '>', '!',
      '\\leq', '\\le', '\\geq', '\\ge',
      '\\neq', '\\ne', '\\equiv', '\\sim', '\\simeq',
      '\\approx', '\\cong', '\\propto', '\\asymp',
      '\\subset', '\\supset', '\\subseteq', '\\supseteq',
      '\\in', '\\ni', '\\notin',
      '\\ll', '\\gg', '\\prec', '\\succ', '\\preceq', '\\succeq',
      '\\perp', '\\parallel', '\\mid',
      '\\vdash', '\\dashv', '\\models',
    ),

    // Punctuation (including standalone delimiters)
    // Lower precedence than brack_group so optional args parse correctly
    // Note: \lbrace and \rbrace are NOT included here - they should be treated
    // as delimiters via the command fallback, not punctuation
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

    // Curly brace group
    group: $ => seq('{', repeat($._expression), '}'),

    // Bracket group (for optional arguments)
    brack_group: $ => seq('[', repeat($._expression), ']'),

    // ========================================================================
    // Sub/Superscript (TeXBook Rules 18)
    // ========================================================================

    subsup: $ => prec.right(1, seq(
      field('base', $._atom),
      choice(
        // subscript only: x_1
        seq('_', field('sub', $._script_arg)),
        // superscript only: x^2
        seq('^', field('sup', $._script_arg)),
        // both: x_1^2 or x^2_1
        seq('_', field('sub', $._script_arg), '^', field('sup', $._script_arg)),
        seq('^', field('sup', $._script_arg), '_', field('sub', $._script_arg)),
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

    // Note: arguments can be braced groups {x} or single tokens like \frac12
    fraction: $ => seq(
      field('cmd', choice(
        '\\frac',     // Standard fraction
        '\\dfrac',    // Display style fraction
        '\\tfrac',    // Text style fraction
        '\\cfrac',    // Continued fraction
      )),
      field('numer', $._frac_arg),
      field('denom', $._frac_arg),
    ),

    // Fraction argument: either a braced group or a single token
    // Note: digit is used for \frac12 cases; number would be too greedy
    _frac_arg: $ => choice(
      $.group,
      $.symbol,
      $.digit,  // Single digit for \frac12
      $.command,
    ),

    // Single digit (for \frac57 style) - higher precedence than number
    digit: $ => token(prec(1, /[0-9]/)),

    // Binomial coefficients
    binomial: $ => seq(
      field('cmd', choice('\\binom', '\\dbinom', '\\tbinom', '\\choose')),
      field('top', $._frac_arg),
      field('bottom', $._frac_arg),
    ),

    // ========================================================================
    // Radicals (Square roots, etc.)
    // ========================================================================

    // Note: radicand can be braced group {x} or single token like \sqrt2
    radical: $ => seq(
      '\\sqrt',
      optional(field('index', $.brack_group)),  // Optional root index
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

    // \middle delimiter inside \left...\right
    middle_delim: $ => seq(
      '\\middle',
      field('delim', $.delimiter),
    ),

    // Sized delimiters: \big, \Big, \bigg, \Bigg
    // Note: delimiter is optional - bare \bigl, \bigr, etc. are valid
    // Use prec.right to prefer consuming the delimiter when present
    sized_delimiter: $ => prec.right(1, seq(
      field('size', choice(
        '\\big', '\\Big', '\\bigg', '\\Bigg',
        '\\bigl', '\\Bigl', '\\biggl', '\\Biggl',
        '\\bigr', '\\Bigr', '\\biggr', '\\Biggr',
        '\\bigm', '\\Bigm', '\\biggm', '\\Biggm',
      )),
      optional(field('delim', $.delimiter)),
    )),

    // Delimiter token: command delimiters are keywords via the word setting
    delimiter: $ => choice(
      '(', ')', '[', ']',
      '\\{', '\\}',
      '\\lbrace', '\\rbrace',  // Brace aliases
      '\\lbrack', '\\rbrack',  // Bracket aliases
      '|', '\\|',
      '\\vert', '\\Vert',      // Vertical bar commands
      '\\langle', '\\rangle',
      '\\lfloor', '\\rfloor',
      '\\lceil', '\\rceil',
      '\\lvert', '\\rvert',
      '\\lVert', '\\rVert',
      '\\lgroup', '\\rgroup',
      '\\lmoustache', '\\rmoustache',
      '\\backslash',
      '\\uparrow', '\\downarrow', '\\updownarrow',
      '\\Uparrow', '\\Downarrow', '\\Updownarrow',
      '.',  // Null delimiter
    ),

    // ========================================================================
    // Over/Under Set Commands (amsmath)
    // ========================================================================

    overunder_command: $ => seq(
      field('cmd', choice(
        '\\overset',     // Place first arg over second
        '\\underset',    // Place first arg under second
        '\\stackrel',    // Like overset with relation spacing
      )),
      field('annotation', $.group),  // What goes over/under
      field('base', $.group),        // The base symbol
    ),

    // Extensible arrows with optional annotations
    extensible_arrow: $ => seq(
      field('cmd', choice(
        '\\xrightarrow', '\\xleftarrow',
        '\\xRightarrow', '\\xLeftarrow',
        '\\xleftrightarrow', '\\xLeftrightarrow',
        '\\xhookleftarrow', '\\xhookrightarrow',
        '\\xmapsto',
      )),
      optional(field('below', $.brack_group)),  // Optional subscript annotation
      field('above', $.group),                   // Superscript annotation
    ),

    // ========================================================================
    // Accents
    // ========================================================================

    // Note: base is optional - bare \vec, \hat, etc. are valid (per MathLive)
    accent: $ => prec.right(1, seq(
      field('cmd', choice(
        // Standard accents
        '\\hat', '\\check', '\\tilde', '\\acute', '\\grave',
        '\\dot', '\\ddot', '\\dddot', '\\ddddot',
        '\\breve', '\\bar', '\\vec',
        // Wide accents
        '\\widehat', '\\widetilde',
        // Over/under
        '\\overline', '\\underline',
        '\\overbrace', '\\underbrace',
        '\\overrightarrow', '\\overleftarrow',
        '\\overleftrightarrow',
      )),
      optional(field('base', choice($.group, $.symbol))),
    )),

    // ========================================================================
    // Big Operators (with limits)
    // ========================================================================

    big_operator: $ => prec.right(seq(
      field('op', choice(
        '\\sum', '\\prod', '\\coprod',
        '\\int', '\\iint', '\\iiint', '\\oint',
        '\\bigcup', '\\bigcap', '\\bigsqcup',
        '\\bigvee', '\\bigwedge', '\\bigoplus', '\\bigotimes',
        '\\liminf', '\\limsup', '\\lim',  // liminf/limsup before lim
        '\\max', '\\min', '\\sup', '\\inf',
        '\\det', '\\gcd', '\\Pr',
      )),
      optional(choice(
        // Limits notation: \sum_{i=1}^{n}
        seq('_', field('lower', $._script_arg), optional(seq('^', field('upper', $._script_arg)))),
        seq('^', field('upper', $._script_arg), optional(seq('_', field('lower', $._script_arg)))),
      )),
    )),

    // ========================================================================
    // Environments: \begin{...} ... \end{...}
    // ========================================================================

    environment: $ => seq(
      $.begin_cmd, '{', field('name', $.env_name), '}',
      optional(field('columns', $.env_columns)),  // For array: {ccc}
      field('body', $.env_body),
      $.end_cmd, '{', field('end_name', $.env_name), '}',
    ),

    begin_cmd: $ => '\\begin',
    end_cmd: $ => '\\end',

    // Environment name
    env_name: $ => choice(
      // Matrix environments
      'matrix', 'pmatrix', 'bmatrix', 'vmatrix', 'Vmatrix', 'Bmatrix', 'smallmatrix',
      // Alignment environments
      'aligned', 'align', 'align*', 'gathered', 'gather', 'gather*',
      'split', 'multline', 'multline*',
      // Cases
      'cases', 'rcases', 'dcases',
      // Arrays
      'array', 'subarray',
      // Equation environments
      'equation', 'equation*',
    ),

    // Column spec for arrays: {ccc} or {|c|c|c|}
    env_columns: $ => seq('{', /[lcr|@{}p\d.]+/, '}'),

    // Environment body - content between \begin and \end
    // Can contain expressions, row separators (\\), and column separators (&)
    env_body: $ => repeat1(choice(
      $._expression,
      $.row_sep,
      $.col_sep,
    )),

    row_sep: $ => '\\\\',
    col_sep: $ => '&',

    // ========================================================================
    // Text mode in math
    // ========================================================================

    text_command: $ => seq(
      field('cmd', choice(
        '\\text', '\\textrm', '\\textit', '\\textbf', '\\textsf', '\\texttt',
        '\\mbox', '\\hbox',
      )),
      field('content', $.text_group),
    ),

    // Text inside \text{} - different from math group
    text_group: $ => seq('{', optional($.text_content), '}'),
    text_content: $ => /[^{}]+/,

    // ========================================================================
    // Style commands
    // ========================================================================

    style_command: $ => prec.right(seq(
      field('cmd', choice(
        // Math variants
        '\\mathrm', '\\mathit', '\\mathbf', '\\mathsf', '\\mathtt',
        '\\mathcal', '\\mathfrak', '\\mathbb', '\\mathscr',
        // Sizing
        '\\displaystyle', '\\textstyle', '\\scriptstyle', '\\scriptscriptstyle',
        // Operator name
        '\\operatorname',
      )),
      optional(field('arg', $.group)),
    )),

    // ========================================================================
    // Box commands: \bbox, \fbox, \boxed
    // ========================================================================

    box_command: $ => seq(
      field('cmd', choice(
        '\\bbox',   // AMS box with optional styling
        '\\fbox',   // Framed box
        '\\boxed',  // AMS boxed (like fbox)
        '\\colorbox', // Color background box
      )),
      optional(field('options', $.brack_group)),  // Optional [color] for bbox
      field('content', $.group),
    ),

    // ========================================================================
    // Rule command: \rule[raise]{width}{height}
    // ========================================================================

    rule_command: $ => seq(
      '\\rule',
      optional(field('raise', $.brack_group)),  // Optional raise amount
      field('width', $.group),
      field('height', $.group),
    ),

    // ========================================================================
    // Phantom commands: \phantom, \hphantom, \vphantom, \smash
    // ========================================================================

    phantom_command: $ => seq(
      field('cmd', choice(
        '\\phantom',   // Full phantom
        '\\hphantom',  // Horizontal phantom (width only)
        '\\vphantom',  // Vertical phantom (height/depth only)
        '\\smash',     // Smash height/depth
      )),
      optional(field('options', $.brack_group)),  // [t] or [b] for smash
      field('content', $.group),
    ),

    // ========================================================================
    // Spacing commands
    // ========================================================================

    space_command: $ => choice(
      '\\,', '\\:', '\\;', '\\!',  // Thin, medium, thick, negative thin
      '\\quad', '\\qquad',
      '\\hspace', '\\hspace*',
    ),

    // ========================================================================
    // Generic command (fallback for Greek letters, symbols, etc.)
    // ========================================================================

    command: $ => prec.right(-1, seq(
      field('name', $.command_name),
      repeat(field('arg', choice($.group, $.brack_group))),
    )),

    // Command name: backslash followed by letters
    // Note: This regex should not match command keywords like \left, \right, etc.
    // But tree-sitter handles this through explicit string literals taking precedence
    command_name: $ => token(prec(-1, /\\[a-zA-Z@]+\*?/)),
  },
});
