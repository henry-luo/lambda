// TypeScript grammar v2: External type parser architecture
// Extends JS grammar; type expressions are opaque external tokens parsed by C++ type parser

const JavaScript = require('tree-sitter-javascript/grammar');

module.exports = function defineGrammar(dialect) {
  return grammar(JavaScript, {
    name: dialect,

    externals: ($, previous) => previous.concat([
      $._function_signature_automatic_semicolon,
      $.__error_recovery,
      // v2: opaque type tokens — scanned by external scanner, parsed later by C++ type parser
      $._ts_type,             // full type expression body (e.g. "string | number", "(a: Foo) => Bar")
      $._ts_type_arguments,   // <Type, ...> in expression context (needs <> disambiguation)
      $._ts_type_parameters,  // <T extends U, V = W> in declaration context (unambiguous)
      $._ts_interface_body,   // opaque interface body { ... } (scanned by external scanner)
    ]),

    // No type supertypes needed — types are opaque tokens
    supertypes: ($, previous) => previous,

    precedences: ($, previous) => previous.concat([
      [
        'call',
        'instantiation',
        'unary',
        'binary',
        $.await_expression,
        $.arrow_function,
      ],
      [
        'extends',
        'instantiation',
      ],
      [$.accessibility_modifier, $.primary_expression],
      ['unary_void', $.expression],
      [$.extends_clause, $.primary_expression],
      ['unary', 'assign'],
      ['declaration', $.expression],
      [$.override_modifier, $.primary_expression],
      [$.decorator_call_expression, $.decorator],
      [$.new_expression, $.primary_expression],
      [$.meta_property, $.primary_expression],
      // [$.construct_signature, $._property_name], // removed: construct_signature no longer reachable
    ]),

    conflicts: ($, previous) => previous.concat([
      // parenthesized class expression
      [$.class],

      [$.primary_expression, $._parameter_name],

      [$.nested_identifier, $.nested_type_identifier, $.primary_expression],
      [$.nested_identifier, $.nested_type_identifier],

      // async/new/<T> ambiguity with external type tokens
      [$.primary_expression, $._property_name, $.arrow_function],
      [$.arrow_function, $._property_name],
      [$.new_expression, $._property_name],
      [$.primary_expression, $.mapped_type_clause],
    ]).concat(
      dialect === 'typescript' ? [] : [
        [$.jsx_opening_element, $._ts_type_parameters],
      ],
    ),

    inline: ($, previous) => previous
      .filter((rule) => ![
        '_formal_parameter',
        '_call_signature',
      ].includes(rule.name))
      .concat([
        $._type_identifier,
        $._jsx_start_opening_element,
      ]),

    rules: {
      // ================================================================
      // CLASS / FIELD OVERRIDES (add TS modifiers + type annotations)
      // ================================================================

      public_field_definition: $ => seq(
        repeat(field('decorator', $.decorator)),
        optional(choice(
          seq('declare', optional($.accessibility_modifier)),
          seq($.accessibility_modifier, optional('declare')),
        )),
        choice(
          seq(optional('static'), optional($.override_modifier), optional('readonly')),
          seq(optional('abstract'), optional('readonly')),
          seq(optional('readonly'), optional('abstract')),
          optional('accessor'),
        ),
        field('name', $._property_name),
        optional(choice('?', '!')),
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),

      catch_clause: $ => seq(
        'catch',
        optional(
          seq(
            '(',
            field('parameter', choice($.identifier, $._destructuring_pattern)),
            optional(field('type', $.type_annotation)),
            ')',
          ),
        ),
        field('body', $.statement_block),
      ),

      // ================================================================
      // CALL / NEW EXPRESSION OVERRIDES (add type_arguments)
      // ================================================================

      call_expression: $ => choice(
        prec('call', seq(
          field('function', choice($.expression, $.import)),
          field('type_arguments', optional(alias($._ts_type_arguments, $.type_arguments))),
          field('arguments', $.arguments),
        )),
        prec('template_call', seq(
          field('function', choice($.primary_expression, $.new_expression)),
          field('arguments', $.template_string),
        )),
        prec('member', seq(
          field('function', $.primary_expression),
          '?.',
          field('type_arguments', optional(alias($._ts_type_arguments, $.type_arguments))),
          field('arguments', $.arguments),
        )),
      ),

      new_expression: $ => prec.right('new', seq(
        'new',
        field('constructor', $.primary_expression),
        field('type_arguments', optional(alias($._ts_type_arguments, $.type_arguments))),
        field('arguments', optional($.arguments)),
      )),

      // ================================================================
      // EXPRESSION OVERRIDES (assignment, lhs, primary, expression)
      // ================================================================

      assignment_expression: $ => prec.right('assign', seq(
        optional('using'),
        field('left', choice($.parenthesized_expression, $._lhs_expression)),
        '=',
        field('right', $.expression),
      )),

      _augmented_assignment_lhs: ($, previous) => choice(previous, $.non_null_expression),

      _lhs_expression: ($, previous) => choice(previous, $.non_null_expression),

      primary_expression: ($, previous) => choice(
        previous,
        $.non_null_expression,
      ),

      expression: ($, previous) => {
        const choices = [
          $.as_expression,
          $.satisfies_expression,
          $.instantiation_expression,
          $.internal_module,
        ];

        if (dialect === 'typescript') {
          choices.push($.type_assertion);
          choices.push(...previous.members.filter((member) =>
            member.name !== '_jsx_element',
          ));
        } else if (dialect === 'tsx') {
          choices.push(...previous.members);
        } else {
          throw new Error(`Unknown dialect ${dialect}`);
        }

        return choice(...choices);
      },

      _jsx_start_opening_element: $ => seq(
        '<',
        optional(
          seq(
            choice(
              field('name', choice(
                $._jsx_identifier,
                $.jsx_namespace_name,
              )),
              seq(
                field('name', choice(
                  $.identifier,
                  alias($.nested_identifier, $.member_expression),
                )),
                field('type_arguments', optional(alias($._ts_type_arguments, $.type_arguments))),
              ),
            ),
            repeat(field('attribute', $._jsx_attribute)),
          ),
        ),
      ),

      jsx_opening_element: $ => prec.dynamic(-1, seq(
        $._jsx_start_opening_element,
        '>',
      )),

      jsx_self_closing_element: $ => prec.dynamic(-1, seq(
        $._jsx_start_opening_element,
        '/>',
      )),

      // ================================================================
      // IMPORT / EXPORT OVERRIDES (add 'type' keyword)
      // ================================================================

      export_specifier: (_, previous) => seq(
        optional(choice('type', 'typeof')),
        previous,
      ),

      _import_identifier: $ => choice($.identifier, alias('type', $.identifier)),

      import_specifier: $ => seq(
        optional(choice('type', 'typeof')),
        choice(
          field('name', $._import_identifier),
          seq(
            field('name', choice($._module_export_name, alias('type', $.identifier))),
            'as',
            field('alias', $._import_identifier),
          ),
        ),
      ),

      import_attribute: $ => seq(choice('with', 'assert'), $.object),

      import_clause: $ => choice(
        $.namespace_import,
        $.named_imports,
        seq(
          $._import_identifier,
          optional(seq(
            ',',
            choice(
              $.namespace_import,
              $.named_imports,
            ),
          )),
        ),
      ),

      import_statement: $ => seq(
        'import',
        optional(choice('type', 'typeof')),
        choice(
          seq($.import_clause, $._from_clause),
          $.import_require_clause,
          field('source', $.string),
        ),
        optional($.import_attribute),
        $._semicolon,
      ),

      export_statement: ($, previous) => choice(
        previous,
        seq(
          'export',
          'type',
          $.export_clause,
          optional($._from_clause),
          $._semicolon,
        ),
        seq('export', '=', $.expression, $._semicolon),
        seq('export', 'as', 'namespace', $.identifier, $._semicolon),
      ),

      import_require_clause: $ => seq(
        $.identifier,
        '=',
        'require',
        '(',
        field('source', $.string),
        ')',
      ),

      import_alias: $ => seq(
        'import',
        $.identifier,
        '=',
        choice($.identifier, $.nested_identifier),
        $._semicolon,
      ),

      // ================================================================
      // EXPRESSION EXTENSIONS (non_null, as, satisfies, type_assertion, instantiation)
      // ================================================================

      non_null_expression: $ => prec.left('unary', seq(
        $.expression, '!',
      )),

      type_assertion: $ => prec.left('unary', seq(
        alias($._ts_type_arguments, $.type_arguments),
        $.expression,
      )),

      as_expression: $ => prec.left('binary', seq(
        $.expression,
        'as',
        choice('const', alias($._ts_type, $.type)),
      )),

      satisfies_expression: $ => prec.left('binary', seq(
        $.expression,
        'satisfies',
        alias($._ts_type, $.type),
      )),

      instantiation_expression: $ => prec('instantiation', seq(
        $.expression,
        field('type_arguments', alias($._ts_type_arguments, $.type_arguments)),
      )),

      // ================================================================
      // VARIABLE / PARAMETER OVERRIDES (add type annotations)
      // ================================================================

      variable_declarator: $ => choice(
        seq(
          field('name', choice($.identifier, $._destructuring_pattern)),
          field('type', optional($.type_annotation)),
          optional($._initializer),
        ),
        prec('declaration', seq(
          field('name', $.identifier),
          '!',
          field('type', $.type_annotation),
        )),
      ),

      parenthesized_expression: $ => seq(
        '(',
        choice(
          seq($.expression, field('type', optional($.type_annotation))),
          $.sequence_expression,
        ),
        ')',
      ),

      _formal_parameter: $ => choice(
        $.required_parameter,
        $.optional_parameter,
      ),

      required_parameter: $ => seq(
        $._parameter_name,
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),

      optional_parameter: $ => seq(
        $._parameter_name,
        '?',
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),

      _parameter_name: $ => seq(
        repeat(field('decorator', $.decorator)),
        optional($.accessibility_modifier),
        optional($.override_modifier),
        optional('readonly'),
        field('pattern', choice($.pattern, $.this)),
      ),

      // ================================================================
      // TYPE ANNOTATION (thin wrapper — content is opaque _ts_type)
      // ================================================================

      type_annotation: $ => seq(':', alias($._ts_type, $.type)),

      asserts: $ => seq(
        'asserts',
        choice($.identifier, $.this),
      ),

      asserts_annotation: $ => seq(':', $.asserts),

      type_predicate: $ => seq(
        field('name', choice($.identifier, $.this)),
        'is',
        alias($._ts_type, $.type),
      ),

      type_predicate_annotation: $ => seq(':', $.type_predicate),

      // ================================================================
      // FUNCTION SIGNATURES (add type_parameters + return type)
      // ================================================================

      _call_signature: $ => seq(
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        field('parameters', $.formal_parameters),
        field('return_type', optional(
          choice($.type_annotation, $.asserts_annotation, $.type_predicate_annotation),
        )),
      ),

      function_signature: $ => seq(
        optional('async'),
        'function',
        field('name', $.identifier),
        $._call_signature,
        choice($._semicolon, $._function_signature_automatic_semicolon),
      ),

      arrow_function: $ => seq(
        optional('async'),
        choice(
          field('parameter', choice(
            alias($._reserved_identifier, $.identifier),
            $.identifier,
          )),
          $._call_signature,
        ),
        '=>',
        field('body', choice(
          $.expression,
          $.statement_block,
        )),
      ),

      // ================================================================
      // DECORATOR OVERRIDE (add type_arguments)
      // ================================================================

      decorator: $ => seq(
        '@',
        choice(
          $.identifier,
          alias($.decorator_member_expression, $.member_expression),
          alias($.decorator_call_expression, $.call_expression),
          alias($.decorator_parenthesized_expression, $.parenthesized_expression),
        ),
      ),

      decorator_call_expression: $ => prec('call', seq(
        field('function', choice(
          $.identifier,
          alias($.decorator_member_expression, $.member_expression),
        )),
        optional(field('type_arguments', alias($._ts_type_arguments, $.type_arguments))),
        field('arguments', $.arguments),
      )),

      decorator_parenthesized_expression: $ => seq(
        '(',
        choice(
          $.identifier,
          alias($.decorator_member_expression, $.member_expression),
          alias($.decorator_call_expression, $.call_expression),
        ),
        ')',
      ),

      // ================================================================
      // CLASS OVERRIDES (add TS-specific members)
      // ================================================================

      class: $ => prec('literal', seq(
        repeat(field('decorator', $.decorator)),
        'class',
        field('name', optional($._type_identifier)),
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        optional($.class_heritage),
        field('body', $.class_body),
      )),

      abstract_class_declaration: $ => prec('declaration', seq(
        repeat(field('decorator', $.decorator)),
        'abstract',
        'class',
        field('name', $._type_identifier),
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        optional($.class_heritage),
        field('body', $.class_body),
      )),

      class_declaration: $ => prec.left('declaration', seq(
        repeat(field('decorator', $.decorator)),
        'class',
        field('name', $._type_identifier),
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        optional($.class_heritage),
        field('body', $.class_body),
        optional($._automatic_semicolon),
      )),

      class_heritage: $ => choice(
        seq($.extends_clause, optional($.implements_clause)),
        $.implements_clause,
      ),

      extends_clause: $ => seq(
        'extends',
        commaSep1($._extends_clause_single),
      ),

      _extends_clause_single: $ => prec('extends', seq(
        field('value', $.expression),
        field('type_arguments', optional(alias($._ts_type_arguments, $.type_arguments))),
      )),

      implements_clause: $ => seq(
        'implements',
        commaSep1(alias($._ts_type, $.type)),
      ),

      class_body: $ => seq(
        '{',
        repeat(choice(
          seq(
            repeat(field('decorator', $.decorator)),
            $.method_definition,
            optional($._semicolon),
          ),
          seq($.method_signature, choice($._function_signature_automatic_semicolon, ',')),
          $.class_static_block,
          seq(
            choice(
              $.abstract_method_signature,
              $.index_signature,
              $.method_signature,
              $.public_field_definition,
            ),
            choice($._semicolon, ','),
          ),
          ';',
        )),
        '}',
      ),

      method_definition: $ => prec.left(seq(
        optional($.accessibility_modifier),
        optional('static'),
        optional($.override_modifier),
        optional('readonly'),
        optional('async'),
        optional(choice('get', 'set', '*')),
        field('name', $._property_name),
        optional('?'),
        $._call_signature,
        field('body', $.statement_block),
      )),

      method_signature: $ => seq(
        optional($.accessibility_modifier),
        optional('static'),
        optional($.override_modifier),
        optional('readonly'),
        optional('async'),
        optional(choice('get', 'set', '*')),
        field('name', $._property_name),
        optional('?'),
        $._call_signature,
      ),

      abstract_method_signature: $ => seq(
        optional($.accessibility_modifier),
        'abstract',
        optional($.override_modifier),
        optional(choice('get', 'set', '*')),
        field('name', $._property_name),
        optional('?'),
        $._call_signature,
      ),

      // ================================================================
      // DECLARATIONS (TS-specific)
      // ================================================================

      declaration: ($, previous) => choice(
        previous,
        $.function_signature,
        $.abstract_class_declaration,
        $.module,
        prec('declaration', $.internal_module),
        $.type_alias_declaration,
        $.enum_declaration,
        $.interface_declaration,
        $.import_alias,
        $.ambient_declaration,
      ),

      // Type alias: type Name<Params> = _ts_type ;
      type_alias_declaration: $ => seq(
        'type',
        field('name', $._type_identifier),
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        '=',
        field('value', alias($._ts_type, $.type)),
        $._semicolon,
      ),

      // Interface: structural shell — body members use _ts_type for annotations
      interface_declaration: $ => seq(
        'interface',
        field('name', $._type_identifier),
        field('type_parameters', optional(alias($._ts_type_parameters, $.type_parameters))),
        optional($.extends_type_clause),
        field('body', $.interface_body),
      ),

      extends_type_clause: $ => seq(
        'extends',
        commaSep1(alias($._ts_type, $.type)),
      ),

      // v2: interface body is an opaque external token — parsed by C++ interface body parser
      interface_body: $ => $._ts_interface_body,

      // call_signature, property_signature, construct_signature — removed
      // (only used in interface_body which is now an opaque external token)

      index_signature: $ => seq(
        optional(
          seq(
            field('sign', optional(choice('-', '+'))),
            'readonly',
          ),
        ),
        '[',
        choice(
          seq(
            field('name', choice(
              $.identifier,
              alias($._reserved_identifier, $.identifier),
            )),
            ':',
            field('index_type', alias($._ts_type, $.type)),
          ),
          $.mapped_type_clause,
        ),
        ']',
        field('type', choice(
          $.type_annotation,
          $.omitting_type_annotation,
          $.adding_type_annotation,
          $.opting_type_annotation,
        )),
      ),

      mapped_type_clause: $ => seq(
        field('name', $._type_identifier),
        'in',
        field('type', alias($._ts_type, $.type)),
        optional(seq('as', field('alias', alias($._ts_type, $.type)))),
      ),

      omitting_type_annotation: $ => seq('-?:', alias($._ts_type, $.type)),
      adding_type_annotation: $ => seq('+?:', alias($._ts_type, $.type)),
      opting_type_annotation: $ => seq('?:', alias($._ts_type, $.type)),

      // ================================================================
      // ENUM (full grammar — generates runtime code)
      // ================================================================

      enum_declaration: $ => seq(
        optional('const'),
        'enum',
        field('name', $.identifier),
        field('body', $.enum_body),
      ),

      enum_body: $ => seq(
        '{',
        optional(seq(
          sepBy1(',', choice(
            field('name', $._property_name),
            $.enum_assignment,
          )),
          optional(','),
        )),
        '}',
      ),

      enum_assignment: $ => seq(
        field('name', $._property_name),
        $._initializer,
      ),

      // ================================================================
      // NAMESPACE / MODULE / AMBIENT
      // ================================================================

      module: $ => seq('module', $._module),

      internal_module: $ => seq('namespace', $._module),

      _module: $ => prec.right(seq(
        field('name', choice($.string, $.identifier, $.nested_identifier)),
        field('body', optional($.statement_block)),
      )),

      ambient_declaration: $ => seq(
        'declare',
        choice(
          $.declaration,
          seq('global', $.statement_block),
          seq('module', '.', alias($.identifier, $.property_identifier), ':', alias($._ts_type, $.type), $._semicolon),
        ),
      ),

      // ================================================================
      // MODIFIERS
      // ================================================================

      accessibility_modifier: _ => choice('public', 'private', 'protected'),

      override_modifier: _ => 'override',

      // ================================================================
      // NESTED TYPE IDENTIFIER (for extends_type_clause and elsewhere)
      // ================================================================

      nested_type_identifier: $ => prec('member', seq(
        field('module', choice($.identifier, $.nested_identifier)),
        '.',
        field('name', $._type_identifier),
      )),

      // ================================================================
      // TYPE IDENTIFIER
      // ================================================================

      _type_identifier: $ => alias($.identifier, $.type_identifier),

      // v2: removed type-name keywords (any, number, boolean, string, symbol, object)
      // — no longer used as keyword tokens after type externalization; they're just identifiers now
      // Also removed duplicate 'readonly' and redundant 'export' (already in JS _reserved_identifier)
      _reserved_identifier: (_, previous) => choice(
        'declare',
        'namespace',
        'type',
        'public',
        'private',
        'protected',
        'override',
        'readonly',
        'module',
        'new',
        previous,
      ),
    },
  });
};

function commaSep1(rule) {
  return sepBy1(',', rule);
}

function commaSep(rule) {
  return sepBy(',', rule);
}

function sepBy(sep, rule) {
  return optional(sepBy1(sep, rule));
}

function sepBy1(sep, rule) {
  return seq(rule, repeat(seq(sep, rule)));
}
