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
      $.accent,
      $.big_operator,
      $.text_command,
      $.style_command,
      $.space_command,
      $.command,  // Generic fallback for unknown commands
    ),

    // Single letter variable (a-z, A-Z, Greek via commands)
    symbol: $ => /[a-zA-Z]/,

    // Numeric literal
    number: $ => /[0-9]+\.?[0-9]*/,

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
      '\\approx', '\\cong', '\\propto',
      '\\subset', '\\supset', '\\subseteq', '\\supseteq',
      '\\in', '\\ni', '\\notin',
      '\\ll', '\\gg', '\\prec', '\\succ',
      '\\perp', '\\parallel', '\\mid',
      '\\vdash', '\\dashv', '\\models',
    ),

    // Punctuation
    punctuation: $ => choice(',', ';', ':', '.', '?'),

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
    
    fraction: $ => seq(
      field('cmd', choice(
        '\\frac',     // Standard fraction
        '\\dfrac',    // Display style fraction
        '\\tfrac',    // Text style fraction
        '\\cfrac',    // Continued fraction
      )),
      field('numer', $.group),
      field('denom', $.group),
    ),

    // Binomial coefficients
    binomial: $ => seq(
      field('cmd', choice('\\binom', '\\dbinom', '\\tbinom', '\\choose')),
      field('top', $.group),
      field('bottom', $.group),
    ),

    // ========================================================================
    // Radicals (Square roots, etc.)
    // ========================================================================
    
    radical: $ => seq(
      '\\sqrt',
      optional(field('index', $.brack_group)),  // Optional root index
      field('radicand', $.group),
    ),

    // ========================================================================
    // Delimiters: \left( ... \right)
    // ========================================================================
    
    delimiter_group: $ => seq(
      '\\left', field('left_delim', $.delimiter),
      repeat($._expression),
      '\\right', field('right_delim', $.delimiter),
    ),

    delimiter: $ => choice(
      '(', ')', '[', ']',
      '\\{', '\\}',
      '|', '\\|',
      '\\langle', '\\rangle',
      '\\lfloor', '\\rfloor',
      '\\lceil', '\\rceil',
      '\\lvert', '\\rvert',
      '\\lVert', '\\rVert',
      '.',  // Null delimiter
    ),

    // ========================================================================
    // Accents
    // ========================================================================
    
    accent: $ => seq(
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
      field('base', choice($.group, $.symbol)),
    ),

    // ========================================================================
    // Big Operators (with limits)
    // ========================================================================
    
    big_operator: $ => prec.right(seq(
      field('op', choice(
        '\\sum', '\\prod', '\\coprod',
        '\\int', '\\iint', '\\iiint', '\\oint',
        '\\bigcup', '\\bigcap', '\\bigsqcup',
        '\\bigvee', '\\bigwedge', '\\bigoplus', '\\bigotimes',
        '\\lim', '\\limsup', '\\liminf',
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
    // Spacing commands
    // ========================================================================
    
    space_command: $ => choice(
      '\\,', '\\:', '\\;', '\\!',  // Thin, medium, thick, negative thin
      '\\quad', '\\qquad',
      '\\hspace', '\\hspace*',
      '\\phantom', '\\hphantom', '\\vphantom',
    ),

    // ========================================================================
    // Generic command (fallback for Greek letters, symbols, etc.)
    // ========================================================================
    
    command: $ => prec.right(-1, seq(
      field('name', $.command_name),
      repeat(field('arg', choice($.group, $.brack_group))),
    )),

    // Command name: backslash followed by letters
    command_name: $ => /\\[a-zA-Z@]+\*?/,
  },
});
