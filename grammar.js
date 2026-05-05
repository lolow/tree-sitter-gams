module.exports = grammar({
  name: 'gams',

  extras: $ => [
    /[ \t\r]/,
    $.line_comment,
    $.block_comment_c,
    $.block_comment_dollar,
    /\n/,
  ],

  externals: $ => [
    $.line_comment,
    $.block_comment_c,
    $.block_comment_dollar,
    $.dollar_directive_keyword,
    $.dollar_directive_args,
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat(choice(
      $.statement,
      $.dollar_directive,
    )),

    // A GAMS dollar directive: $name [args ...] through end of line, or
    // $$name [args ...] inline. The scanner emits the keyword and args
    // as two separate tokens so highlighting can colour the keyword
    // distinctly from the (heterogeneous, free-form) argument text.
    dollar_directive: $ => seq(
      $.dollar_directive_keyword,
      optional($.dollar_directive_args)
    ),

    statement: $ => prec(30,
      seq(
        $.statement_without_semicolon,
        ';'
      )
    ),

    statement_without_semicolon: $ =>
      prec(25,
        choice(
          prec(20, $.loop_statement),
          prec(20, $.if_statement),

          prec(10, $.set_declaration),
          prec(9, $.parameter_declaration),
          prec(9, $.scalar_declaration),
          prec(9, $.variable_declaration),
          prec(9, $.equation_declaration),
          prec(9, $.equation_definition),
          prec(9, $.table_declaration),
          prec(9, $.model_declaration),
          prec(9, $.solve_statement),
          prec(9, $.display_statement),
          prec(9, $.option_statement),
          prec(9, $.abort_statement),
          prec(9, $.acronym_declaration),

          $.alias_declaration,
          prec(1, $.assignment_statement)
        )
    ),

    // utils
    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
    set_element: $ => /[a-zA-Z_][a-zA-Z0-9_\-]*/,
    set_element_selection: $ => choice(
      $.set_element,
      seq(
        token("("), commaSep1($.set_element), token(")")
      ),
      seq($.set_element, "*", $.set_element),
      seq($.number, "*", $.number)
    ),

    identifier_with_domain: $ =>
      prec(3,
        seq(
          $.identifier,
          token.immediate('('),
          $.identifier_with_domain_args,
          ')'
        )
    ),

    identifier_with_domain_args: $ =>
      seq(
        $.identifier,
        optional(
          prec(3, repeat(seq(',', $.identifier)))
        )
      ),

    indexed_reference: $ => 
      prec(2,
        seq(
          $.identifier,
          optional(
            seq(
              token.immediate('.'),
              $.variable_attribute_keyword
            )
          ),
          token.immediate('('),
          $.indexed_reference_args,
          ')'
        )
      ),

    reference: $ => 
      prec(2,
        seq(
          $.identifier,
          token.immediate('.'),
          $.variable_attribute_keyword
        )
      ),

    indexed_reference_args: $ =>
      seq(
        $.index_element,
        optional(
          prec(2, repeat(
            seq(
            ',',
            $.index_element
          )
        )))
      ),
    
    index_element: $ =>
      choice(
          $.string,
          seq($.identifier, '*', $.identifier),
          seq($.identifier, '+', $.number),
          seq($.number, '*', $.number),
          $.identifier_with_domain,
          $.identifier,
          $.number
        ),

    // Variable / equation suffix attributes accessed via dotted reference
    // (e.g. x.l, eq.range). Variable suffixes (l/lo/up/fx/m/scale/prior/
    // stage) and equation suffixes (range/slack/slacklo/slackup/infeas)
    // share the same lexer token because both attach to identifiers via
    // an immediate `.`.
    variable_attribute_keyword: $ =>
      choice(
        // variable level/bounds/dual/scale
        token.immediate(caseInsensitive('up')),
        token.immediate(caseInsensitive('lo')),
        token.immediate(caseInsensitive('l')),
        token.immediate(caseInsensitive('fx')),
        token.immediate(caseInsensitive('scale')),
        token.immediate(caseInsensitive('prior')),
        token.immediate(caseInsensitive('stage')),
        token.immediate(caseInsensitive('m')),
        // equation slack / range / infeasibility
        token.immediate(caseInsensitive('range')),
        token.immediate(caseInsensitive('slacklo')),
        token.immediate(caseInsensitive('slackup')),
        token.immediate(caseInsensitive('slack')),
        token.immediate(caseInsensitive('infeas')),
      ),
      
    number: $ => /[+-]?(?:\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/,

    // Compile-time macro reference. Two forms:
    //   %name%  — named macro (e.g. %scenario%)
    //   %1, %2  — positional macro arg passed to $batinclude
    // Strings can contain the same token; see `string` for re-entry.
    macro_ref: $ => token(choice(
      /%[A-Za-z_][A-Za-z_0-9]*%/,
      /%[0-9]+/
    )),

    // GAMS literal constants. `yes`/`no` are booleans; `inf` (positive
     // infinity), `na` (not-available), and `eps` (small positive) are
     // numeric sentinels recognised in expression contexts.
    bool: $ => token(caseInsensitive('yes|no|inf|na|eps')),

    // GAMS comments handled by the external scanner in src/scanner.c.
    // Three forms, all of which the scanner skips leading whitespace
    // before recognising:
    //   line_comment        — '*' in column 0 through end of line
    //   block_comment_c     — /* ... */ (no nesting)
    //   block_comment_dollar — $ontext ... $offtext (case-insensitive,
    //                          both leading-$ at column 0 and inline $$)

    string: $ => choice(
      seq('"', repeat(/[^"]/), '"'),
      seq("'", repeat(/[^']/), "'")
    ),

    // set declaration

    set_keyword: $ => prec(10, choice(
      token.immediate(caseInsensitive('set')),
      token.immediate(caseInsensitive('sets'))
    )),

    set_declaration: $ => prec(10, seq(
      $.set_keyword,
      commaOrNewlineSep1($.set_entry)
      )
    ),

    set_entry: $ => seq(
      choice(
        $.identifier_with_domain,
        $.identifier,               // set_name
      ),                
      optional($.string),           // ["text"]
      optional($.element_block)     // [/element [text], .../]
    ),

    element_block: $ => seq(
        '/',
        choice(
          commaOrNewlineSep1($.element_entry),
          seq($.identifier, '*', $.identifier),
          seq($.number, '*', $.number) 
        ),
        '/'
    ),

    element_entry: $ => seq(
      // Set elements may contain hyphens (e.g. `san-diego`). The lexer
      // prefers `identifier` (the word rule) for hyphen-free names like
      // `seattle`, so we accept either token here. Multi-dimensional
      // (tuple) elements use `.` as the separator, e.g. for a 2D set:
      //   set map(i,j) / a.x, b.y, c.z /;
      choice($.set_element, $.identifier),
      repeat(seq(
        token.immediate('.'),
        choice($.set_element, $.identifier)
      )),
      optional($.string)
    ),

    // subset

    // alias
    alias_keyword: $ => prec(9, 
      token.immediate(caseInsensitive('alias')),
    ),


    alias_declaration: $ => seq(
      $.alias_keyword,
      '(',
      commaSep1($.identifier), // aliases
      ')'
    ),

    scalar_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('scalar')),
      token.immediate(caseInsensitive('scalars'))
    )),

    // scalar declaration
    scalar_declaration: $ => seq(
      $.scalar_keyword,
      commaOrNewlineSep1($.scalar_entry)
    ),

    scalar_entry: $ => seq(
      $.identifier,                 // scalar_name
      optional($.string),            // ["text"]
      optional($.scalar_value_block) // [/numerical_value/]
    ),

    
    scalar_value_block: $ => seq(
      '/',
      choice($.number, $.bool),
      '/'
    ),

    // parameter declaration
    parameter_keyword: $ => prec(10,
      choice(
        token.immediate(caseInsensitive('parameter')),
        token.immediate(caseInsensitive('parameters'))
      )
    ),

    parameter_declaration: $ =>
      seq(
        $.parameter_keyword,
        commaOrNewlineSep1($.param_entry)
      ),

    param_entry: $ => seq(
      choice(
        $.identifier_with_domain,    // param_name(index_list)
        $.identifier               // param_name
      ),
      optional($.string),           // ["text"]
      optional(seq(/\s*\n\s*/, $.param_data_block))           // [/ ... /]
    ),

    param_data_block: $ => seq(
      '/',
      commaOrNewlineSep1($.param_assignment),
      '/'
    ),

    param_assignment: $ => seq(
      $.index_atom,                 // element or tuple (i1, i1.j1, etc.)
      optional('='),                // optional equals sign
      field('value', $.number)
    ),

    // variable declaration
    variable_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('variable')),
      token.immediate(caseInsensitive('variables'))
    )),

    variable_declaration: $ => seq(
      optional($.var_type),
      $.variable_keyword,
      commaOrNewlineSep1($.var_entry)
    ),

    var_entry: $ => seq(
      choice(
        $.identifier,                      // var_name
        $.identifier_with_domain
      ),
      optional($.string),                // ["text"]
      optional($.var_data_block)         // [/ ... /]
    ),

    var_data_block: $ => seq(
      '/',
      commaOrNewlineSep1($.var_attr_assignment),
      '/'
    ),

    // j1.up 10    i1.j2.lo 5     k.m 0    a.scale 20
    var_attr_assignment: $ => seq(
      $.index_atom,
      token.immediate('.'),        // no space between element and dot attribute
      $.variable_attribute_keyword,
      field('value', $.number)
    ),

    // i1, i1.j1, i1.j1.k3 (support multi-dimensional tuples separated by dots)
    index_atom: $ => seq(
      $.set_element_selection,
      repeat(seq('.', $.set_element_selection))
    ),

    var_attr: $ => token(/(up|lo|l|m|scale)/i),

    var_type: $ => prec(9,
      choice(
        token.immediate(caseInsensitive('free')),
        token.immediate(caseInsensitive('positive')),
        token.immediate(caseInsensitive('negative')),
        token.immediate(caseInsensitive('integer')),
        token.immediate(caseInsensitive('binary')),
        token.immediate(caseInsensitive('sos1')),
        token.immediate(caseInsensitive('sos2')),
        token.immediate(caseInsensitive('semicont')),
        token.immediate(caseInsensitive('semiint'))
      )
    ),

    // equation declaration

    equation_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('equation')),
      token.immediate(caseInsensitive('equations'))
    )),

    equation_declaration: $ => seq(
      $.equation_keyword,
      commaOrNewlineSep1($.eq_entry)
    ),

    eq_entry: $ => seq(
      choice(
        $.identifier,                      // eq_name
        $.identifier_with_domain
      ),
      optional($.string),                // ["text"]
      optional($.eq_data_block)         // [/ ... /]
    ),

    eq_data_block: $ => seq(
      '/',
      commaOrNewlineSep1($.eq_attr_assignment),
      '/'
    ),

    // j1.up 10    i1.j2.lo 5     k.m 0    a.scale 20
    eq_attr_assignment: $ => seq(
      $.index_atom,
      token.immediate('.'),        // no space between element and dot attribute
      $.variable_attribute_keyword,
      field('value', $.number)
    ),

    // Equation definition: name[(domain)][$cond] .. lhs =E= rhs
    //
    // GAMS relational operators (case-insensitive):
    //   =e= equality   =l= less-or-equal   =g= greater-or-equal
    //   =n= no relation (variational)
    //   =x= external function   =c= cone (MCP)   =b= boolean
    equation_definition: $ => prec(9, seq(
      field('name', choice($.identifier, $.identifier_with_domain)),
      optional(seq('$', field('condition', $.expression))),
      $.equation_definition_op,
      field('lhs', $.expression),
      $.equation_relational_op,
      field('rhs', $.expression)
    )),

    equation_definition_op: $ => '..',

    equation_relational_op: $ => token(caseInsensitive('=e=|=l=|=g=|=n=|=x=|=c=|=b=')),

    // Table declaration. The 2D data layout (column header + rows) is
    // captured as one opaque `table_body` token through the next `;`.
    // This keeps the table block visually distinct for highlighting
    // without modelling row/column semantics in the syntax tree.
    table_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('table')),
      token.immediate(caseInsensitive('tables'))
    )),

    table_declaration: $ => seq(
      $.table_keyword,
      field('name', choice($.identifier_with_domain, $.identifier)),
      optional(field('description', $.string)),
      optional(field('data', $.table_body))
    ),

    table_body: $ => token(prec(-1, /[^;]+/)),



    // models declaration

    model_declaration: $ => seq( $.model_keyword, 
      commaOrNewlineSep1($.model_entry)
    ),

    model_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('model')),
      token.immediate(caseInsensitive('models'))
    )),

    model_entry: $ => seq(
      $.identifier,
      optional($.string),
      optional($.model_data_block)
    ),

    model_data_block: $ => seq( '/', commaOrNewlineSep1($.model_item), '/' ),
    
    model_item: $ => choice(
      token(caseInsensitive('all')) ,
      $.identifier,               // eqn_name
      $.identifier_with_domain,    // var_name(set_name)
      seq($.identifier, choice('+', '-'), $.identifier)
    ),

    // Solve statement

    solve_keyword: $ => token.immediate(caseInsensitive('solve')),

    solve_direction: $ => choice(
      token.immediate(caseInsensitive('maximizing')),
      token.immediate(caseInsensitive('minimizing'))
    ),

    solve_statement: $ => prec(9,
      seq(
        $.solve_keyword,
        field('model', $.identifier),
        choice(
          // using <type> [<direction> <objective>]
          // direction+objective absent for MCP/CNS/EMP solves.
          seq(
            token.immediate(caseInsensitive('using')),
            field('model_type', choice($.model_type, $.identifier)),
            optional(seq(
              $.solve_direction,
              field('objective', $.identifier)
            ))
          ),
          // <direction> <objective> using <type>
          seq(
            $.solve_direction,
            field('objective', $.identifier),
            token.immediate(caseInsensitive('using')),
            field('model_type', choice($.model_type, $.identifier))
          )
        )
      )
    ),

    // Recognised GAMS solver model types (case-insensitive). Wrapped in
    // a `prec` so the model_type token wins the lexer tie-break against
    // `identifier` for inputs like `lp` / `nlp` / `mip`. The choice in
    // solve_statement still falls back to plain identifier so a user-
    // defined or future model type parses cleanly.
    model_type: $ => token(prec(1, caseInsensitive(
      'lp|nlp|mip|rmip|minlp|rminlp|qcp|miqcp|rmiqcp|mcp|mpec|rmpec|dnlp|cns|emp'
    ))),

    // Display statement
    display_keyword: $ => token.immediate(caseInsensitive('display')),

    display_statement: $ => prec(9, seq($.display_keyword, commaSep1($.display_item))),

    display_item: $ => choice( $.string, $.expression),

    // option <name> = <value> [, <name> = <value>] ...
    // Per GAMS, option names are not reserved — they're just identifiers.
    option_statement: $ => prec(9, seq(
      token.immediate(caseInsensitive('option')),
      commaSep1($.option_assignment)
    )),

    option_assignment: $ => seq(
      field('name', $.identifier),
      '=',
      field('value', choice($.number, $.bool, $.identifier, $.string))
    ),

    // abort [.noError] [<expression> | <string>] ...
    abort_statement: $ => prec(9, seq(
      token.immediate(caseInsensitive('abort')),
      optional(seq(token.immediate('.'), token.immediate(caseInsensitive('noError')))),
      commaSep1($.display_item)
    )),

    // acronym <name> [, <name>] ...
    acronym_keyword: $ => prec(9, choice(
      token.immediate(caseInsensitive('acronym')),
      token.immediate(caseInsensitive('acronyms'))
    )),

    acronym_declaration: $ => seq(
      $.acronym_keyword,
      commaSep1($.identifier)
    ),

    // Expressions

    expression: $ =>
      prec(3,
        choice(
          prec.left(5, $.unary_builtin_function_expr),
          prec.left(4, $.binary_builtin_function_expr),
          prec.left(3, $.multi_args_builtin_function_expr),
          prec.left(2, $.number),
          prec.left(2, $.bool),
          prec.left(2, $.string),
          prec.left(2, $.macro_ref),
          prec.left(2, $.unary_expr),
          prec.left(2, $.indexed_reference),
          prec.left(2, $.paren_expr),
          prec.left(2, $.binary_expr),
          prec.left(2, $.indexed_operation),
          prec.left(1, $.reference),
        // $.call_expr,
          $.conditional_expr,
          prec.left(-1, alias($.identifier, $.bare_identifier)),
      )),

    paren_expr: $ => seq('(', $.expression, ')'),

    unary_expr: $ => prec(100, seq(choice('+', '-', caseInsensitive('not')), $.expression)),

    binary_operator_keyword : $ => choice(
      token('+'), token('-'), token('*'), token('/'), token('**'),
      token('>'), token('<'), token('>='), token('<='), token('<>'),
      token(caseInsensitive('and')), token(caseInsensitive('or')),
      token(caseInsensitive('gt')), token(caseInsensitive('lt')),
      token(caseInsensitive('ge')), token(caseInsensitive('le')),
      token(caseInsensitive('mod'))
    ),

    binary_expr: $ => prec.left(1, seq(
      $.expression,
      $.binary_operator_keyword,
      $.expression
    )),

    indexed_operation_keyword: $ => choice(
      'sum', 'prod', 'smin', 
      'smax', 'sand', 'sor'
    ),

    indexed_operation: $ => seq(
      $.indexed_operation_keyword,
      token.immediate('('),
      $.index_list,
      optional(
        seq(
          '$',
          $.expression
        )
      ),
      token(','),
      $.expression,
      token(')')
    ),

    index_list: $ =>
      choice(
        $.index_element,
        seq(
          token('('),
          $.index_element,
          optional(
              seq(
                ',',
                $.index_element
              )
            ),
          token(')')
          )
        ),

    conditional_expr: $ => prec.left(1, seq(
      $.expression,
      '$',
      $.expression
    )),

    unary_builtin_function_keyword: $ =>
      prec(5,
        choice(
          'abs',
          'ord',
          'card',
          'val',
          'exp',
          'log',
          'log10',
          'sqrt',
          'sin',
          'cos',
          'tan',
          'asin',
          'acos',
          'arctan',
          'sinh',
          'cosh',
          'tanh',
          'ceil',
          'floor',
          'sign',
          'sqr',
          'trunc',
          'frac'
        )
    ),

    unary_builtin_function_expr: $ =>
      prec(5,
        seq(
          $.unary_builtin_function_keyword,
          '(',
          $.expression,
          ')'
        )
      ),


    binary_builtin_function_keyword: $ =>
      prec(4,
        choice(
          'uniform',
          'power',
          'mod'
        )
      ),

    binary_builtin_function_expr: $ =>
      prec(4,
        seq(
          $.binary_builtin_function_keyword,
          '(',
          $.expression,
          ',',
          $.expression,
          ')'
        )
      ),

    multi_args_builtin_function_keyword: $ =>
      prec(3,
        choice(
          'max',
          'min',
          'round'
        )
      ),

    multi_args_builtin_function_expr: $ =>
      prec(3,
        seq(
          $.multi_args_builtin_function_keyword,
          '(',
          $.expression,
          optional(
            repeat(
              seq(
                ',',
                $.expression
              )
            )
          ),
          ')'
        )
      ),
    

    // Assignments

    assignment_statement: $ =>
      prec(1,
        seq(
          field(
            "left_hand_side",
            choice(
              $.indexed_reference,
              $.reference,
              $.identifier, // i
            )
          ),
          optional(
            field("condition",
              prec(2,
                seq(
                  '$',
                  $.expression
                )
              )
            )
          ),
          '=',
          field("right_hand_side",
            $.expression
          )
        )
      ),

    // loops
    loop_keyword: $ => token.immediate(caseInsensitive('loop')),

    loop_statement: $ => prec(20,
      seq(
        $.loop_keyword,
        token('('),
        $.index_list,
        optional(
          seq(
            '$',
            $.expression
          )
        ),
        token(','),
        repeat($.statement),
        optional(
          $.statement_without_semicolon,
        ),
        token(')')
      )
    ),

    // if elseif else control flow
    if_statement: $ => prec(20,
      seq(
        field("if",
          seq(
            'if',
            '(',
            alias($.expression, $.condition), // logical condition
            ',',
            $.statement,
            repeat($.statement), // one or more statements
          )
        ),
        repeat(
          field("elseif",
            seq(
              'elseif',
              alias($.expression, $.condition),
              ',',
              $.statement,
              repeat($.statement)
            )
          ),
        ),
        optional(
          field("else",
            seq(
              'else',
              $.statement,
              repeat($.statement)
            )
          )
        ),
        ')'
      )
    )
  },
  conflicts: $ => [
    // `name(arg, arg)` is ambiguous between a set/parameter/equation
    // declaration domain (bare identifiers only) and an indexed expression
    // reference (full index_element). The parser commits once a non-bare
    // index appears or `..` follows.
    [$.identifier_with_domain_args, $.index_element],
  ],
});

// separate one or more term by comma or newline
function commaOrNewlineSep1(rule) {
  return seq(
    rule, 
    repeat(
      seq(
        choice(',', /\r?\n/),
        rule)
      )
  );
}

function newlineSep1(rule) {
  return seq(rule, 
      repeat(seq(/\r?\n/, rule)));
}

// separate one or more term by comma
function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

function toCaseInsensitive(a) {
  var ca = a.charCodeAt(0);
  if (ca>=97 && ca<=122) return `[${a}${a.toUpperCase()}]`;
  if (ca>=65 && ca<= 90) return `[${a.toLowerCase()}${a}]`;
  return a;
}

function caseInsensitive (keyword) {
  return new RegExp(keyword
    .split('')
    .map(toCaseInsensitive)
    .join('')
  )
}