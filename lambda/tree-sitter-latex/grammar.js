/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const sepBy1 = (rule, sep) => seq(rule, repeat(seq(sep, rule)));

const sepBy = (rule, sep) => optional(sepBy1(rule, sep));

const specialEnvironment = ({ rule, name, content, options }) => {
  const beginRule = `_${rule}_begin`;
  const endRule = `_${rule}_end`;
  const groupRule = `_${rule}_group`;
  const nameRule = `_${rule}_name`;
  return {
    [rule]: $ =>
      seq(
        field('begin', alias($[beginRule], $.begin)),
        content($),
        field('end', alias($[endRule], $.end)),
      ),

    [beginRule]: $ =>
      seq(
        field('command', '\\begin'),
        field('name', alias($[groupRule], $.curly_group_text)),
        options ? options($) : seq(),
      ),

    [endRule]: $ =>
      seq(
        field('command', '\\end'),
        field('name', alias($[groupRule], $.curly_group_text)),
      ),

    [groupRule]: $ => seq('{', field('text', alias($[nameRule], $.text)), '}'),

    [nameRule]: $ => seq(field('word', alias(name, $.word))),
  };
};

module.exports = grammar({
  name: 'latex',
  extras: $ => [$.line_comment],  // whitespace is now significant!
  
  // PHASE 8: Add inline rules to reduce parser states
  // Inline rules are substituted directly into usage sites, eliminating intermediate states
  // Note: Be careful - inlining complex rules can cause conflicts
  inline: $ => [
    $._section_part,
    $._glob_pattern_fragment,
    $._math_delimiter_part,
  ],
  
  conflicts: $ => [
    // Allow paragraph_break to interrupt text
    [$._text_content, $.text],
  ],
  
  externals: $ => [
    $._trivia_raw_fi,
    $._trivia_raw_env_comment,
    $._trivia_raw_env_verbatim,
    $._trivia_raw_env_listing,
    $._trivia_raw_env_minted,
    $._trivia_raw_env_asy,      // Used for asy, asydef
    $._trivia_raw_env_pycode,   // Used for pycode, luacode, luacode*
    $._trivia_raw_env_sagesilent,  // Used for sagesilent, sageblock
  ],
  word: $ => $.command_name,
  rules: {
    source_file: $ => repeat($._root_content),

    //--- Trivia

    // Whitespace is now significant in LaTeX!
    // Following LaTeX.js rules:
    // - Single space/tab/newline becomes a space
    // - Multiple spaces/tabs/newlines collapse to single space
    // - Double newline (blank line) creates paragraph break
    
    // Strategy: Separate horizontal and vertical whitespace completely
    // paragraph_break: blank line (double newline)
    paragraph_break: $ => /\n[ \t]*\n/,
    
    // space: ONLY horizontal whitespace or single newline
    space: $ => choice(
      /[ \t]+/,          // spaces and tabs
      /\n/,              // single newline (will NOT match double newline if paragraph_break has priority)
    ),

    line_comment: $ => /%[^\r\n]*/,

    block_comment: $ =>
      seq(
        field('begin', '\\iffalse'),
        field('comment', optional(alias($._trivia_raw_fi, $.comment))),
        field('end', optional('\\fi')),
      ),

    //--- Content

    _root_content: $ => choice($._section, $._paragraph, $._flat_content),

    _flat_content: $ => prec.right(choice($._text_with_env_content, '[', ']')),

    _text_with_env_content: $ =>
      choice(
        ',',
        '=',
        $.comment_environment,
        $.verbatim_environment,
        $.listing_environment,
        $.minted_environment,
        $.asy_environment,     // Unified: handles asy, asydef
        $.code_environment,    // Unified: handles pycode, luacode, luacode*
        $.sage_environment,    // Unified: handles sagesilent, sageblock
        $.generic_environment,
        $.math_environment,
        $._text_content,
      ),

    _text_content: $ =>
      prec.right(
        choice(
          $.curly_group,
          $.block_comment,
          $._command,
          prec(10, $.paragraph_break),    // highest precedence - matches before text
          prec(1, $.text),                 // lower precedence - text will stop at paragraph breaks
          $._math_content,
          '(',
          ')',
        ),
      ),

    //--- Sections
    // PHASE 9: Flattened section structure
    // Instead of enforcing part → chapter → section hierarchy at parse time,
    // we use a flat structure. The hierarchy is built in AST builder.
    // This dramatically reduces parser states.

    _section: $ =>
      prec.right(repeat1($._any_section)),

    _any_section: $ => choice(
      $.part,
      $.chapter,
      $.section,
      $.subsection,
      $.subsubsection,
    ),

    _paragraph: $ =>
      prec.right(repeat1($._any_paragraph)),

    _any_paragraph: $ => choice(
      $.paragraph,
      $.subparagraph,
      $.enum_item,
    ),

    _section_part: $ =>
      seq(field('toc', optional($.brack_group)), field('text', $.curly_group)),

    // OPTIMIZED: Using regex for part commands
    _part_declaration: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(part|part\*|addpart|addpart\*)/)),
          ),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified part - flat structure, no nested hierarchy
    part: $ =>
      prec.right(-1, seq($._part_declaration, repeat($._flat_content))),

    // OPTIMIZED: Using regex for chapter commands
    _chapter_declaration: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(chapter|chapter\*|addchap|addchap\*)/)),
          ),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified chapter - flat structure
    chapter: $ =>
      prec.right(-1, seq($._chapter_declaration, repeat($._flat_content))),

    // OPTIMIZED: Using regex for section commands
    _section_declaration: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(section|section\*|addsec|addsec\*)/)),
          ),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified section - flat structure
    section: $ =>
      prec.right(-1, seq($._section_declaration, repeat($._flat_content))),

    // OPTIMIZED: Using regex for subsection commands
    _subsection_declaration: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\subsection\*?/))),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified subsection - flat structure
    subsection: $ =>
      prec.right(-1, seq($._subsection_declaration, repeat($._flat_content))),

    // OPTIMIZED: Using regex for subsubsection commands
    _subsubsection_declaration: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\subsubsection\*?/))),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified subsubsection - flat structure
    subsubsection: $ =>
      prec.right(-1, seq($._subsubsection_declaration, repeat($._flat_content))),

    // OPTIMIZED: Using regex for paragraph/subparagraph/item commands
    _paragraph_declaration: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\paragraph\*?/))),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified paragraph
    paragraph: $ =>
      prec.right(-1, seq($._paragraph_declaration, repeat($._flat_content))),

    _subparagraph_declaration: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\subparagraph\*?/))),
          optional($._section_part),
        ),
      ),

    // PHASE 9: Simplified subparagraph
    subparagraph: $ =>
      prec.right(-1, seq($._subparagraph_declaration, repeat($._flat_content))),

    _enum_itemdeclaration: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\item\*?/))),
          field('label', optional($.brack_group_text)),
        ),
      ),

    enum_item: $ =>
      prec.right(
        -1,
        seq(
          $._enum_itemdeclaration,
          repeat($._flat_content),
        ),
      ),

    //--- Group

    curly_group: $ => seq('{', repeat($._root_content), '}'),

    curly_group_text: $ => seq('{', field('text', $.text), '}'),

    curly_group_word: $ => seq('{', field('word', $.word), '}'),

    curly_group_value: $ => seq('{', field('value', $.text), '}'),

    curly_group_spec: $ =>
      seq('{', repeat(choice($._text_content, '=')), '}'),

    curly_group_text_list: $ =>
      seq('{', sepBy(field('text', $.text), ','), '}'),

    curly_group_label: $ => seq('{', field('label', $.label), '}'),

    curly_group_label_list: $ =>
      seq('{', sepBy(field('label', $.label), ','), '}'),

    curly_group_path: $ => seq('{', field('path', $.path), '}'),

    curly_group_path_list: $ =>
      seq('{', sepBy(field('path', $.path), ','), '}'),

    curly_group_uri: $ => seq('{', field('uri', $.uri), '}'),

    curly_group_command_name: $ =>
      seq('{', field('command', $.command_name), '}'),

    curly_group_key_value: $ =>
      seq('{', sepBy(field('pair', $.key_value_pair), ','), '}'),

    curly_group_glob_pattern: $ =>
      seq('{', field('pattern', $.glob_pattern), '}'),

    curly_group_impl: $ =>
      seq('{', repeat(choice($._text_content, '[', ']', ',', '=')), '}'),

    curly_group_author_list: $ =>
      seq(
        '{',
        sepBy(
          alias(repeat1($._text_content), $.author),
          alias('\\and', $.command_name),
        ),
        '}',
      ),

    brack_group: $ =>
      seq('[', repeat(choice($._text_with_env_content, $.brack_group)), ']'),

    brack_group_text: $ => seq('[', field('text', $.text), ']'),

    brack_group_word: $ => seq('[', field('word', $.word), ']'),

    brack_group_argc: $ => seq('[', field('value', $.argc), ']'),

    brack_group_key_value: $ =>
      seq('[', sepBy(field('pair', $.key_value_pair), ','), ']'),

    //--- Text

    text: $ =>
      prec.right(
        repeat1(
          field(
            'word',
            choice(
              $.space,              // whitespace within text (not paragraph breaks)
              $.operator,
              $.word,
              $.placeholder,
              $.delimiter,
              $.block_comment,
              $._command,
              $.superscript,
              $.subscript,
            ),
          ),
        ),
      ),

    word: $ => /[^\s\\%\{\},\$\[\]\(\)=\#&_\^\-\+\/\*]+/,

    placeholder: $ => /#+\d/,

    value_literal: $ => /(\d+\.)?\d+/,

    delimiter: $ => /&/,

    path: $ => /[^\*\"\[\]:;,\|\{\}<>]+/,

    uri: $ => /[^\[\]\{\}]+/,

    label: $ => /[^\\\[\]\{\}\$\(\)=&%\s_\^\#\~,]+/,

    argc: $ => /\d/,

    glob_pattern: $ => repeat1($._glob_pattern_fragment),

    _glob_pattern_fragment: $ =>
      choice(
        seq('{', repeat($._glob_pattern_fragment), '}'),
        /[^\"\[\]:;\|\{\}<>]+/,
      ),

    // OPTIMIZED: Regex pattern for operators
    operator: $ => /[+\-*\/<>!|:']/,

    letter: $ => /[^\\%\{\}\$\#_\^]/,

    subscript: $ =>
      seq(
        '_',
        field('subscript', choice($.curly_group, $.letter, $.command_name)),
      ),

    superscript: $ =>
      seq(
        '^',
        field('superscript', choice($.curly_group, $.letter, $.command_name)),
      ),

    //--- Key / Value

    key_value_pair: $ =>
      seq(field('key', $.text), optional(seq('=', field('value', $.value)))),

    value: $ => repeat1(choice($._text_content, $.brack_group)),

    //--- Math

    _math_content: $ =>
      choice(
        $.displayed_equation,
        $.inline_formula,
        $.math_delimiter,
        $.text_mode,
      ),

    displayed_equation: $ =>
      prec.left(
        seq(choice('$$', '\\['), repeat($._root_content), choice('$$', '\\]')),
      ),

    inline_formula: $ =>
      prec.left(
        seq(choice('$', '\\('), repeat($._root_content), choice('$', '\\)')),
      ),

    _math_delimiter_part: $ =>
      choice($.word, $.command_name, '[', ']', '(', ')', '|'),

    math_delimiter: $ =>
      prec.left(
        seq(
          field(
            'left_command',
            // OPTIMIZED: Using regex for left delimiter commands
            token(prec(1, /\\(left|bigl|Bigl|biggl|Biggl)/)),
          ),
          field('left_delimiter', $._math_delimiter_part),
          repeat($._root_content),
          field(
            'right_command',
            // OPTIMIZED: Using regex for right delimiter commands
            token(prec(1, /\\(right|bigr|Bigr|biggr|Biggr)/)),
          ),
          field('right_delimiter', $._math_delimiter_part),
        ),
      ),

    // OPTIMIZED: Using regex for text mode commands
    text_mode: $ =>
      seq(
        field('command', token(prec(1, /\\(text|intertext|shortintertext)/))),
        field('content', $.curly_group),
      ),

    //--- Environments

    begin: $ =>
      prec.right(
        seq(
          field('command', '\\begin'),
          field('name', $.curly_group_text),
          field('options', optional($.brack_group)),
        ),
      ),

    end: $ =>
      prec.right(
        seq(field('command', '\\end'), field('name', $.curly_group_text)),
      ),

    generic_environment: $ =>
      seq(
        field('begin', $.begin),
        repeat($._root_content),
        field('end', $.end),
      ),

    //--- Trivia environments

    ...specialEnvironment({
      rule: 'comment_environment',
      name: 'comment',
      content: $ =>
        field('comment', alias($._trivia_raw_env_comment, $.comment)),
      options: undefined,
    }),

    ...specialEnvironment({
      rule: 'verbatim_environment',
      name: 'verbatim',
      content: $ =>
        field('verbatim', alias($._trivia_raw_env_verbatim, $.comment)),
      options: undefined,
    }),

    ...specialEnvironment({
      rule: 'listing_environment',
      name: 'lstlisting',
      content: $ =>
        field('code', alias($._trivia_raw_env_listing, $.source_code)),
      options: undefined,
    }),

    ...specialEnvironment({
      rule: 'minted_environment',
      name: 'minted',
      content: $ =>
        field('code', alias($._trivia_raw_env_minted, $.source_code)),
      options: $ =>
        seq(
          field('options', optional($.brack_group_key_value)),
          field('language', $.curly_group_text),
        ),
    }),

    // SIMPLIFIED: Unified asy environment (handles asy and asydef)
    ...specialEnvironment({
      rule: 'asy_environment',
      name: /(asy|asydef)/,
      content: $ => field('code', alias($._trivia_raw_env_asy, $.source_code)),
      options: undefined,
    }),

    // SIMPLIFIED: Unified code environment (handles pycode, luacode, luacode*)
    // All use the same scanner - just skip content until \end{...}
    ...specialEnvironment({
      rule: 'code_environment',
      name: /(pycode|luacode\*?)/,
      content: $ =>
        field('code', alias($._trivia_raw_env_pycode, $.source_code)),
      options: undefined,
    }),

    // SIMPLIFIED: Unified sage environment (handles sagesilent and sageblock)
    ...specialEnvironment({
      rule: 'sage_environment',
      name: /(sagesilent|sageblock)/,
      content: $ =>
        field('code', alias($._trivia_raw_env_sagesilent, $.source_code)),
      options: undefined,
    }),

    // OPTIMIZED: Using regex for math environment names (27 variants → 1 pattern)
    ...specialEnvironment({
      rule: 'math_environment',
      name: /(math|displaymath|displaymath\*|equation|equation\*|multline|multline\*|eqnarray|eqnarray\*|align|align\*|aligned|aligned\*|array|array\*|split|split\*|alignat|alignat\*|alignedat|alignedat\*|gather|gather\*|gathered|gathered\*|flalign|flalign\*)/,
      content: $ => repeat($._flat_content),
      options: undefined,
    }),

    //--- Command

    // PHASE 6 OPTIMIZATION: Aggressive consolidation for parser size
    _command: $ =>
      choice(
        // Metadata commands - consolidated (title + author + caption → 1)
        $.metadata_command,
        // Include commands - consolidated (9 → 2 rules)
        $.single_path_include,
        $.double_path_include,
        $.citation,
        // Counter commands - consolidated into single rule
        $.counter_command,
        // Label commands - consolidated into single rule
        $.label_command,
        // Definition commands
        $.new_command_definition,
        $.old_command_definition,
        $.let_command_definition,
        $.paired_delimiter_definition,
        $.environment_definition,
        // Glossary & acronym - consolidated (4 → 2 rules)
        $.glossary_command,
        $.acronym_command,
        $.theorem_definition,
        // Color commands - consolidated (3 → 1)
        $.color_command,
        $.tikz_library_import,
        $.hyperlink,
        $.changes_replaced,
        $.todo,
        // Priority 1: New command types
        $.escape_sequence,
        $.diacritic_command,
        $.linebreak_command,
        $.simple_command,      // Consolidated: spacing + symbol commands
        $.verb_command,
        $.generic_command,
      ),

    todo: $ =>
      seq(
        field('command', $.todo_command_name),
        field('options', optional($.brack_group)),
        field('arg', $.curly_group)
      ),

    todo_command_name: $ => /\\([a-zA-Z]?[a-zA-Z]?todo)/,

    generic_command: $ =>
      prec.right(
        seq(
          field('command', $.command_name),
          repeat(field('arg', $.curly_group)),
        ),
      ),

    command_name: $ => /\\([^\r\n]|[@a-zA-Z]+\*?)?/,

    // Control symbols (escape characters) - Priority 1
    // OPTIMIZED: Using regex for escape sequences (12 variants → 1 pattern)
    escape_sequence: $ =>
      seq(
        '\\',
        /[\$%#&{}_\\,@\/\-]/,
      ),

    // Diacritic commands - Priority 1
    // OPTIMIZED: Using regex for accent marks (15 variants → 1 pattern)
    diacritic_command: $ =>
      prec.right(
        seq(
          '\\',
          field(
            'accent',
            /['`\^"~=.uvHcdbrkt]/
          ),
          field(
            'base',
            optional(
              choice(
                $.curly_group_text, // \'{e}
                $.letter, // \'e
              ),
            ),
          ),
        ),
      ),

    // Line break commands - Priority 1
    linebreak_command: $ =>
      prec.right(
        seq(
          '\\\\',
          field('spacing', optional($.brack_group)), // \\[1cm]
        ),
      ),

    // PHASE 7: Consolidated simple commands (spacing + symbol → 1)
    // Spacing commands + Symbol commands (no arguments)
    simple_command: $ =>
      field(
        'command',
        token(prec(1, /\\(quad|qquad|enspace|enskip|,|!|;|:|thinspace|negthinspace|space|medspace|thickspace|negmedspace|negthickspace|ss|SS|o|O|ae|AE|oe|OE|aa|AA|l|L|i|j|dh|DH|th|TH|dag|ddag|S|P|copyright|pounds|textbackslash|LaTeX|TeX|LaTeXe|textquoteleft|textquoteright|textquotedblleft|textquotedblright|textendash|textemdash|textellipsis|dots|ldots|textbullet|textperiodcentered|textasteriskcentered|textcent|textsterling|textyen|texteuro|textdollar|textexclamdown|textquestiondown|textsection|textparagraph|textdegree|textregistered|texttrademark)/))
      ),

    // OPTIMIZED: Verb command with regex
    verb_command: $ =>
      seq(
        field('command', token(prec(1, /\\verb\*?/))),
        field('delimiter', /[^\s\w]/), // any non-alphanumeric
        field('content', /[^\r\n]+/), // content until delimiter
        // Note: delimiter matching is tricky in tree-sitter
        // May need external scanner for full correctness
      ),

    // PHASE 6: Consolidated counter command (6 rules → 1)
    // All counter commands: newcounter, counterwithin, counterwithout, value, setcounter, addtocounter, stepcounter, refstepcounter, arabic, alph, Alph, roman, Roman, fnsymbol
    counter_command: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\(newcounter|counter(within|without)\*?|value|setcounter|addtocounter|stepcounter|refstepcounter|arabic|alph|Alph|roman|Roman|fnsymbol)/))),
          field('arg1', $.curly_group_text),
          optional(field('arg2', choice($.curly_group_text, $.brack_group_text))),
        )
      ),

    // PHASE 7: Consolidated metadata commands (title + author + caption → 1)
    metadata_command: $ =>
      seq(
        field('command', token(prec(1, /\\(title|author|caption|thanks|date|abstract)/))),
        field('options', optional($.brack_group)),
        field('content', $.curly_group),
      ),

    // PHASE 6: Consolidated include commands (9 rules → 2)
    // single_path_include: All commands that take optional options + single path
    single_path_include: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(documentclass|include|subfileinclude|input|subfile|includegraphics|includesvg|includeinkscape|verbatiminput|VerbatimInput|bibliographystyle|usepackage|RequirePackage|bibliography|addbibresource)/)),
        ),
        field('options', optional($.brack_group_key_value)),
        field('path', $.curly_group_path_list),
      ),

    // double_path_include: Commands that take two paths (directory + file)
    double_path_include: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(import|subimport|inputfrom|subimportfrom|includefrom|subincludefrom)/))
        ),
        field('directory', $.curly_group_path),
        field('file', $.curly_group_path),
      ),

    // OPTIMIZED: Using regex instead of 60-item choice() to reduce parser size
    citation: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(cite|cite\*|Cite|nocite|citet|citep|citet\*|citep\*|citeA|citeR|citeS|citeyearR|citeauthor|citeauthor\*|Citeauthor|Citeauthor\*|citetitle|citetitle\*|citeyear|citeyear\*|citedate|citedate\*|citeurl|fullcite|citeyearpar|citealt|citealp|citetext|parencite|parencite\*|Parencite|footcite|footfullcite|footcitetext|textcite|Textcite|smartcite|Smartcite|supercite|autocite|Autocite|autocite\*|Autocite\*|volcite|Volcite|pvolcite|Pvolcite|fvolcite|ftvolcite|svolcite|Svolcite|tvolcite|Tvolcite|avolcite|Avolcite|notecite|Notecite|pnotecite|Pnotecite|fnotecite)/))
        ),
        optional(
          seq(
            field('prenote', $.brack_group),
            field('postnote', optional($.brack_group)),
          ),
        ),
        field('keys', $.curly_group_text_list),
      ),

    // PHASE 6: Consolidated label command (4 rules → 1)
    // All label commands: label, ref, eqref, vref, Vref, autoref, pageref, cref, Cref, etc., crefrange, Crefrange, newlabel
    label_command: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\(label|newlabel|ref|eqref|vref|Vref|autoref\*?|pageref\*?|autopageref\*?|cref\*?|Cref\*?|cpageref|Cpageref|namecref|nameCref|lcnamecref|namecrefs|nameCrefs|lcnamecrefs|labelcref\*?|labelcpageref\*?|crefrange\*?|Crefrange\*?|cpagerefrange|Cpagerefrange)/))),
          field('name', $.curly_group_label_list),
          optional(field('arg2', $.curly_group)),
        )
      ),

    new_command_definition: $ =>
      choice($._new_command_definition, $._newer_command_definition, $._new_command_copy),

    // OPTIMIZED: Regex for command definition commands
    _new_command_definition: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(newcommand|newcommand\*|renewcommand|renewcommand\*|providecommand|providecommand\*|DeclareRobustCommand|DeclareRobustCommand\*|DeclareMathOperator|DeclareMathOperator\*)/))
        ),
        field('declaration', choice($.curly_group_command_name, $.command_name)),
        optional(
          seq(
            field('argc', $.brack_group_argc),
            field('default', optional($.brack_group)),
          ),
        ),
        field('implementation', $.curly_group),
      ),

    // OPTIMIZED: Regex for newer command definition commands
    _newer_command_definition: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(NewDocumentCommand|RenewDocumentCommand|ProvideDocumentCommand|DeclareDocumentCommand|NewExpandableDocumentCommand|RenewExpandableDocumentCommand|ProvideExpandableDocumentCommand|DeclareExpandableDocumentCommand)/))
        ),
        field('declaration', choice($.curly_group_command_name, $.command_name)),
        field('spec', $.curly_group_spec),
        field('implementation', $.curly_group),
      ),

    // OPTIMIZED: _new_command_copy with regex
    _new_command_copy: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(NewCommandCopy|RenewCommandCopy|DeclareCommandCopy)/)),
        ),
        field('declaration', choice($.curly_group_command_name, $.command_name)),
        field('implementation', $.curly_group_command_name),
      ),

    // OPTIMIZED: Regex for old-style command definitions
    old_command_definition: $ =>
      seq(
        field('command', token(prec(1, /\\(def|gdef|edef|xdef)/))),
        field('declaration', $.command_name)
      ),

    // OPTIMIZED: let_command_definition with regex
    let_command_definition: $ =>
      seq(
        field('command', token(prec(1, /\\(let|glet)/))),
        field('declaration', $.command_name),
        optional('='),
        field('implementation', $.command_name),
      ),

    // OPTIMIZED: paired_delimiter_definition with regex
    paired_delimiter_definition: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(DeclarePairedDelimiter|DeclarePairedDelimiterX)/)),
          ),
          field('declaration', $.curly_group_command_name),
          field('argc', optional($.brack_group_argc)),
          field('left', choice($.curly_group_impl, $.command_name)),
          field('right', choice($.curly_group_impl, $.command_name)),
          field('body', optional($.curly_group)),
        ),
      ),

    environment_definition: $ =>
      choice($._environment_definition, $._newer_environment_definition, $._new_environment_copy),

    // OPTIMIZED: _environment_definition with regex
    _environment_definition: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(newenvironment|renewenvironment)/)),
        ),
        field('name', $.curly_group_text),
        field('argc', optional($.brack_group_argc)),
        field('begin', $.curly_group_impl),
        field('end', $.curly_group_impl),
      ),

    // OPTIMIZED: _newer_environment_definition with regex
    _newer_environment_definition: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(NewDocumentEnvironment|RenewDocumentEnvironment|ProvideDocumentEnvironment|DeclareDocumentEnvironment)/)),
        ),
        field('name', $.curly_group_text),
        field('spec', $.curly_group_spec),
        field('begin', $.curly_group_impl),
        field('end', $.curly_group_impl),
      ),

    // OPTIMIZED: _new_environment_copy with regex
    _new_environment_copy: $ =>
      seq(
        field(
          'command',
          token(prec(1, /\\(NewEnvironmentCopy|RenewEnvironmentCopy|DeclareEnvironmentCopy)/)),
        ),
        field('name', $.curly_group_text),
        field('name', $.curly_group_text),
      ),

    // PHASE 6: Consolidated glossary commands (2 → 1)
    // Combines glossary_entry_definition and glossary_entry_reference
    glossary_command: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(newglossaryentry|gls|Gls|GLS|glspl|Glspl|GLSpl|glsdisp|glslink|glstext|Glstext|GLStext|glsfirst|Glsfirst|GLSfirst|glsplural|Glsplural|GLSplural|glsfirstplural|Glsfirstplural|GLSfirstplural|glsname|Glsname|GLSname|glssymbol|Glssymbol|glsdesc|Glsdesc|GLSdesc|glsuseri|Glsuseri|GLSuseri|glsuserii|Glsuserii|GLSuserii|glsuseriii|Glsuseriii|GLSuseriii|glsuseriv|Glsuseriv|GLSuseriv|glsuserv|Glsuserv|GLSuserv|glsuservi|Glsuservi|GLSuservi)/))
          ),
          field('options', optional($.brack_group_key_value)),
          field('name', $.curly_group_text),
          optional(field('extra', $.curly_group_key_value)),
        )
      ),

    // PHASE 6: Consolidated acronym commands (2 → 1)
    // Combines acronym_definition and acronym_reference
    acronym_command: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(newacronym|acrshort|Acrshort|ACRshort|acrshortpl|Acrshortpl|ACRshortpl|acrlong|Acrlong|ACRlong|acrlongpl|Acrlongpl|ACRlongpl|acrfull|Acrfull|ACRfull|acrfullpl|Acrfullpl|ACRfullpl|acs|Acs|acsp|Acsp|acl|Acl|aclp|Aclp|acf|Acf|acfp|Acfp|ac|Ac|acp|glsentrylong|Glsentrylong|glsentrylongpl|Glsentrylongpl|glsentryshort|Glsentryshort|glsentryshortpl|Glsentryshortpl|glsentryfullpl|Glsentryfullpl)/))
          ),
          field('options', optional($.brack_group_key_value)),
          field('name', $.curly_group_text),
          repeat(field('extra', $.curly_group)),
        )
      ),

    // OPTIMIZED: theorem_definition with regex
    theorem_definition: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(newtheorem\*?|declaretheorem\*?)/)),
          ),
          optional(field('options', $.brack_group_key_value)),
          field('name', $.curly_group_text_list),
          optional(
            choice(
              seq(
                field('title', $.curly_group),
                field('counter', optional($.brack_group_text)),
              ),
              seq(
                field('counter', $.brack_group_text),
                field('title', $.curly_group),
              ),
            ),
          ),
        ),
      ),

    // PHASE 6: Consolidated color commands (3 → 1)
    color_command: $ =>
      prec.right(
        seq(
          field(
            'command',
            token(prec(1, /\\(definecolor|definecolorset|color|pagecolor|textcolor|mathcolor|colorbox)/)),
          ),
          field('arg1', optional($.brack_group_text)),
          field('arg2', $.curly_group_text),
          repeat(field('extra', $.curly_group)),
        )
      ),

    // OPTIMIZED: Using regex for tikz library import
    tikz_library_import: $ =>
      seq(
        field('command', token(prec(1, /\\(usepgflibrary|usetikzlibrary)/))),
        field('paths', $.curly_group_path_list),
      ),

    // OPTIMIZED: Using regex for hyperlink commands
    hyperlink: $ =>
      prec.right(
        seq(
          field('command', token(prec(1, /\\(url|href)/))),
          field('uri', $.curly_group_uri),
          field('label', optional($.curly_group)),
        ),
      ),

    changes_replaced: $ =>
      seq(
        field('command', '\\replaced'),
        field('text_added', $.curly_group),
        field('text_deleted', $.curly_group)
      ),
  },
});
