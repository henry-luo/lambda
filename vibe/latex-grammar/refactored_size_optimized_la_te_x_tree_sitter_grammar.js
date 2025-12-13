/// <reference types="tree-sitter-cli/dsl" />
// Size-optimized LaTeX grammar (structure-first)

module.exports = grammar({
  name: 'latex',

  extras: $ => [$.space, $.line_comment],

  word: $ => $.command_name,

  rules: {
    source_file: $ => repeat($._block_item),

    // ------------------------------------------------------------------
    // Blocks
    // ------------------------------------------------------------------

    _block_item: $ => choice(
      $.paragraph_break,
      $.environment,
      $.section_command,
      $.paragraph,
    ),

    paragraph: $ => repeat1($._inline_item),

    paragraph_break: $ => /\n[ \t]*\n/,

    // ------------------------------------------------------------------
    // Inline content
    // ------------------------------------------------------------------

    _inline_item: $ => choice(
      $.text_chunk,
      $.space,
      $.command,
      $.curly_group,
      $.brack_group,
      $.inline_math,
    ),

    text_chunk: $ => /[^\\{}$%\[\]\n]+/,

    space: $ => /[ \t\n]+/,

    line_comment: $ => /%[^\n]*/,

    // ------------------------------------------------------------------
    // Commands (fully generic)
    // ------------------------------------------------------------------

    command: $ => prec.right(seq(
      field('name', $.command_name),
      repeat(field('arg', choice($.curly_group, $.brack_group))),
    )),

    command_name: $ => /\\[@a-zA-Z]+\*?/,

    // ------------------------------------------------------------------
    // Groups
    // ------------------------------------------------------------------

    curly_group: $ => seq('{', repeat($._inline_item), '}'),

    brack_group: $ => seq('[', repeat($._inline_item), ']'),

    // ------------------------------------------------------------------
    // Math (restricted content)
    // ------------------------------------------------------------------

    inline_math: $ => prec.left(seq(
      choice('$', '\\('),
      repeat($._math_item),
      choice('$', '\\)'),
    )),

    _math_item: $ => choice(
      $.text_chunk,
      $.command,
      $.curly_group,
    ),

    // ------------------------------------------------------------------
    // Environments (unified)
    // ------------------------------------------------------------------

    environment: $ => seq(
      $.begin_env,
      repeat($._block_item),
      $.end_env,
    ),

    begin_env: $ => seq(
      field('command', '\\begin'),
      field('name', $.curly_group),
      optional($.brack_group),
    ),

    end_env: $ => seq(
      field('command', '\\end'),
      field('name', $.curly_group),
    ),

    // ------------------------------------------------------------------
    // Sections (flattened)
    // ------------------------------------------------------------------

    section_command: $ => seq(
      field('name', token(/\\(part|chapter|section|subsection|subsubsection)\*?/)),
      repeat(choice($.curly_group, $.brack_group)),
    ),
  },
});
