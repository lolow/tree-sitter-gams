// External scanner for tree-sitter-gams.
//
// Recognises lexical forms that tree-sitter's regex-based lexer cannot
// express cleanly, plus dedicated tokens for the GAMS block-control
// directives so the grammar can model their structural pairing.
//
// Comments
//   line_comment             column-0 '*' through end of line, OR '#' /
//                            '//' anywhere through end of line
//   block_comment_c          /* ... */ (no nesting)
//   block_comment_dollar     $ontext ... $offtext (case-insensitive,
//                            both leading-$ at column 0 and inline $$)
//
// Generic dollar directive
//   dollar_directive_keyword $name (no .label suffix; label is a
//                            separate regex token in the grammar)
//   dollar_directive_end     emitted at \n or EOF inside a directive's
//                            args, terminating the args repeat
//
// Block-control directives — each opens or closes a paired construct
//   ifthen_keyword           $ifthen / $ifthenE / $ifthenI
//   elseif_keyword           $elseIf / $elseIfE / $elseIfI
//   else_keyword             $else
//   endif_keyword            $endif
//   onecho_keyword           $onEcho / $onEchoS / $onEchoV
//   offecho_keyword          $offEcho
//   onput_keyword            $onPut / $onPutS / $onPutV
//   offput_keyword           $offPut
//   onembedded_keyword       $onEmbeddedCode / $onEmbeddedCodeS /
//                            $onEmbeddedCodeV
//   offembedded_keyword      $offEmbeddedCode
//   embedded_body            Opaque content between $onEmbeddedCode
//                            and $offEmbeddedCode (consumed as a
//                            single token; injection.scm re-parses
//                            it as the chosen embedded language).
//
// Compile-time invariant: the order of TokenType members must match
// the `externals` array in grammar.js exactly.

#include "tree_sitter/parser.h"
#include <stddef.h>
#include <string.h>

static inline int32_t ascii_tolower(int32_t c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

typedef enum {
  T_LINE_COMMENT = 0,
  T_BLOCK_COMMENT_C,
  T_BLOCK_COMMENT_DOLLAR,
  T_DOLLAR_DIRECTIVE_KEYWORD,
  T_DOLLAR_DIRECTIVE_END,
  T_IFTHEN_KEYWORD,
  T_ELSEIF_KEYWORD,
  T_ELSE_KEYWORD,
  T_ENDIF_KEYWORD,
  T_ONECHO_KEYWORD,
  T_OFFECHO_KEYWORD,
  T_ONPUT_KEYWORD,
  T_OFFPUT_KEYWORD,
  T_ONEMBEDDED_KEYWORD,
  T_OFFEMBEDDED_KEYWORD,
  T_EMBEDDED_BODY,
  T_ECHO_BODY,
  T_PUT_BODY,
  T_COUNT_,
} TokenType;

static int g_dummy;
void *tree_sitter_gams_external_scanner_create(void) { return &g_dummy; }
void tree_sitter_gams_external_scanner_destroy(void *p) { (void)p; }
unsigned tree_sitter_gams_external_scanner_serialize(void *p, char *buf) {
  (void)p; (void)buf; return 0;
}
void tree_sitter_gams_external_scanner_deserialize(void *p, const char *buf,
                                                   unsigned n) {
  (void)p; (void)buf; (void)n;
}

static int match_word_ci(TSLexer *lexer, const char *word) {
  for (const char *p = word; *p; ++p) {
    if (lexer->lookahead == 0) return 0;
    if (ascii_tolower(lexer->lookahead) !=
        ascii_tolower((unsigned char)*p)) return 0;
    lexer->advance(lexer, false);
  }
  return 1;
}

static void skip_whitespace(TSLexer *lexer) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    lexer->advance(lexer, true);
  }
}

static int is_id_start(int32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_id_continue(int32_t c) {
  return is_id_start(c) || (c >= '0' && c <= '9');
}

// Case-insensitive comparison of name buffer against a literal.
static int name_eq(const char *name, int n, const char *lit) {
  int len = (int)strlen(lit);
  if (n != len) return 0;
  return memcmp(name, lit, (size_t)len) == 0;
}

// Map the directive name to a control-directive token type, or
// T_DOLLAR_DIRECTIVE_KEYWORD if none matches.
static TokenType classify_directive(const char *name, int n) {
  // ifthen / ifthenE / ifthenI
  if (name_eq(name, n, "ifthen") || name_eq(name, n, "ifthene") ||
      name_eq(name, n, "iftheni")) return T_IFTHEN_KEYWORD;
  // elseif / elseifE / elseifI
  if (name_eq(name, n, "elseif") || name_eq(name, n, "elseife") ||
      name_eq(name, n, "elseifi")) return T_ELSEIF_KEYWORD;
  if (name_eq(name, n, "else"))    return T_ELSE_KEYWORD;
  if (name_eq(name, n, "endif"))   return T_ENDIF_KEYWORD;
  // onEcho / onEchoS / onEchoV / offEcho
  if (name_eq(name, n, "onecho") || name_eq(name, n, "onechos") ||
      name_eq(name, n, "onechov")) return T_ONECHO_KEYWORD;
  if (name_eq(name, n, "offecho")) return T_OFFECHO_KEYWORD;
  // onPut / onPutS / onPutV / offPut
  if (name_eq(name, n, "onput") || name_eq(name, n, "onputs") ||
      name_eq(name, n, "onputv")) return T_ONPUT_KEYWORD;
  if (name_eq(name, n, "offput")) return T_OFFPUT_KEYWORD;
  // onEmbeddedCode / onEmbeddedCodeS / onEmbeddedCodeV / offEmbeddedCode
  if (name_eq(name, n, "onembeddedcode") ||
      name_eq(name, n, "onembeddedcodes") ||
      name_eq(name, n, "onembeddedcodev")) return T_ONEMBEDDED_KEYWORD;
  if (name_eq(name, n, "offembeddedcode")) return T_OFFEMBEDDED_KEYWORD;
  return T_DOLLAR_DIRECTIVE_KEYWORD;
}

bool tree_sitter_gams_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  (void)payload;

  // ---- (priority 1) directive end-of-line marker ---------------------
  if (valid_symbols[T_DOLLAR_DIRECTIVE_END]) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\r') {
      lexer->advance(lexer, true);
    }
    if (lexer->lookahead == '\n') {
      lexer->advance(lexer, false);
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_END;
      return true;
    }
    if (lexer->lookahead == 0) {
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_END;
      return true;
    }
    // Not at EOL — fall through so other arg tokens get a chance.
  }

  // ---- (priority 2) opaque block bodies ($onEcho / $onPut /
  //                   $onEmbeddedCode). Each is active right after
  //                   the corresponding open directive's
  //                   directive_end. The scanner consumes lines
  //                   until it reaches a column-0 $off<closer>; that
  //                   closing directive itself is NOT part of the
  //                   body token (mark_end excludes it).
  {
    const char *closer = NULL;
    TokenType produced = (TokenType)0;
    if (valid_symbols[T_EMBEDDED_BODY]) {
      closer = "offembeddedcode"; produced = T_EMBEDDED_BODY;
    } else if (valid_symbols[T_ECHO_BODY]) {
      closer = "offecho"; produced = T_ECHO_BODY;
    } else if (valid_symbols[T_PUT_BODY]) {
      closer = "offput"; produced = T_PUT_BODY;
    }
    if (closer) {
      int saw_newline = 1; // open directive's end token was a \n
      for (;;) {
        if (lexer->lookahead == 0) {
          // EOF without finding the closer — don't emit a body token.
          // The parser then reports the unclosed block as an ERROR
          // node, matching the integrity check we get for $ifthen.
          return false;
        }
        if (saw_newline && lexer->lookahead == '$' &&
            lexer->get_column(lexer) == 0) {
          lexer->mark_end(lexer);
          lexer->advance(lexer, false);
          if (lexer->lookahead == '$') lexer->advance(lexer, false);
          if (match_word_ci(lexer, closer)) {
            lexer->result_symbol = produced;
            return true;
          }
          // Not the closer; keep scanning. mark_end will be reset
          // on the next pass that finds either the closer or EOF.
        }
        saw_newline = (lexer->lookahead == '\n');
        lexer->advance(lexer, false);
      }
    }
  }

  skip_whitespace(lexer);

  // ---- column-0 '*' or anywhere '#' line comment ----------------------
  if (valid_symbols[T_LINE_COMMENT] &&
      ((lexer->lookahead == '*' && lexer->get_column(lexer) == 0) ||
       lexer->lookahead == '#')) {
    lexer->advance(lexer, false);
    while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
      lexer->advance(lexer, false);
    }
    lexer->result_symbol = T_LINE_COMMENT;
    return true;
  }

  // ---- '/'-prefixed: '//' line comment OR '/* … */' block comment -----
  if ((valid_symbols[T_LINE_COMMENT] || valid_symbols[T_BLOCK_COMMENT_C]) &&
      lexer->lookahead == '/') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/' && valid_symbols[T_LINE_COMMENT]) {
      lexer->advance(lexer, false);
      while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
        lexer->advance(lexer, false);
      }
      lexer->result_symbol = T_LINE_COMMENT;
      return true;
    }
    if (lexer->lookahead == '*' && valid_symbols[T_BLOCK_COMMENT_C]) {
      lexer->advance(lexer, false);
      for (;;) {
        if (lexer->lookahead == 0) return false;
        if (lexer->lookahead == '*') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            lexer->result_symbol = T_BLOCK_COMMENT_C;
            return true;
          }
        } else {
          lexer->advance(lexer, false);
        }
      }
    }
    return false;
  }

  // ---- $ entrypoint --------------------------------------------------
  // Any of the directive-keyword externals can land here.
  int any_keyword_valid =
      valid_symbols[T_BLOCK_COMMENT_DOLLAR] ||
      valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD] ||
      valid_symbols[T_IFTHEN_KEYWORD] ||
      valid_symbols[T_ELSEIF_KEYWORD] ||
      valid_symbols[T_ELSE_KEYWORD] ||
      valid_symbols[T_ENDIF_KEYWORD] ||
      valid_symbols[T_ONECHO_KEYWORD] ||
      valid_symbols[T_OFFECHO_KEYWORD] ||
      valid_symbols[T_ONPUT_KEYWORD] ||
      valid_symbols[T_OFFPUT_KEYWORD] ||
      valid_symbols[T_ONEMBEDDED_KEYWORD] ||
      valid_symbols[T_OFFEMBEDDED_KEYWORD];

  if (any_keyword_valid && lexer->lookahead == '$') {
    int col = lexer->get_column(lexer);
    lexer->advance(lexer, false);
    int double_dollar = 0;
    if (lexer->lookahead == '$') {
      double_dollar = 1;
      lexer->advance(lexer, false);
    }
    // Accept the keyword if:
    //   - $$<name>  (inline form, anywhere)
    //   - $<name>   at column 0 (top-level directive)
    //   - $<name>   when DIRECTIVE_END is valid — chained directive
    //               inside another's args
    //   - $<name>   when ENDIF / OFFECHO / OFFPUT / OFFEMBEDDED is
    //               valid — we are inside a control block, where
    //               indented $-directives are conventional
    int in_directive_args = valid_symbols[T_DOLLAR_DIRECTIVE_END];
    int in_control_block =
        valid_symbols[T_ENDIF_KEYWORD] ||
        valid_symbols[T_OFFECHO_KEYWORD] ||
        valid_symbols[T_OFFPUT_KEYWORD] ||
        valid_symbols[T_OFFEMBEDDED_KEYWORD] ||
        valid_symbols[T_ELSEIF_KEYWORD] ||
        valid_symbols[T_ELSE_KEYWORD];
    if (!double_dollar && col != 0 &&
        !in_directive_args && !in_control_block) {
      return false;
    }

    // Read the directive name. Buffer up to 15 chars (long enough for
    // "onembeddedcodes"); the lexer keeps advancing past the cap so the
    // emitted token spans the whole keyword.
    char name[16] = {0};
    int n = 0;
    while (is_id_continue(lexer->lookahead)) {
      if (n < 15) name[n] = (char)ascii_tolower(lexer->lookahead);
      n++;
      lexer->advance(lexer, false);
    }
    if (n == 0) return false;

    // ---- block_comment_dollar: scan to matching $offtext --------------
    if (n == 6 && memcmp(name, "ontext", 6) == 0 &&
        valid_symbols[T_BLOCK_COMMENT_DOLLAR]) {
      for (;;) {
        if (lexer->lookahead == 0) return false;
        if (lexer->lookahead == '$') {
          int close_col = lexer->get_column(lexer);
          lexer->advance(lexer, false);
          int close_double = 0;
          if (lexer->lookahead == '$') {
            close_double = 1;
            lexer->advance(lexer, false);
          }
          if ((close_double || close_col == 0) &&
              match_word_ci(lexer, "offtext")) {
            lexer->result_symbol = T_BLOCK_COMMENT_DOLLAR;
            return true;
          }
        } else {
          lexer->advance(lexer, false);
        }
      }
    }

    // The keyword span ends at the end of the name. The optional
    // `.<label>` suffix is a separate token (defined in grammar.js
    // as `directive_label` via token.immediate); leaving the lexer
    // positioned at the `.` lets it match next.
    TokenType kind = classify_directive(name, n);
    if (kind != T_DOLLAR_DIRECTIVE_KEYWORD) {
      // Control directive: only emit it where the parser admits it.
      // An orphan $endif / $else / $offEcho etc. produces no token
      // here; tree-sitter then reports an ERROR node, which is the
      // intended structural-integrity feedback.
      if (valid_symbols[kind]) {
        lexer->result_symbol = kind;
        return true;
      }
      return false;
    }
    if (valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) {
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_KEYWORD;
      return true;
    }
    return false;
  }

  return false;
}
