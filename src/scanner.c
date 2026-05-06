// External scanner for tree-sitter-gams.
//
// Recognises lexical forms that tree-sitter's regex-based lexer cannot
// express cleanly:
//
//   line_comment             column-0 '*' through end of line, OR '#' /
//                            '//' anywhere through end of line
//   block_comment_c          /* ... */ (no nesting)
//   block_comment_dollar     $ontext ... $offtext (case-insensitive,
//                            both leading-$ at column 0 and inline $$)
//   dollar_directive_keyword $name for any non-block directive. The
//                            $ifthen / $elseIf / $else / $endif family
//                            is intentionally NOT given dedicated
//                            tokens — those directives parse as
//                            generic dollar_directive nodes (via the
//                            extras path in grammar.js) so the parser
//                            doesn't fight real GAMS code that
//                            intersperses conditional directives with
//                            arbitrary content.
//   dollar_directive_end     emitted at \n or EOF inside a directive's
//                            args, terminating the args repeat
//
// Block-control directives — paired open/close with an opaque body.
// These ARE structural because their bodies hold non-GAMS text
// (echo/put output or an embedded sub-language) that the main lexer
// cannot tokenise. The body markers are scanned as single opaque
// tokens up to the matching $off<name> at column 0.
//   onecho_keyword           $onEcho / $onEchoS / $onEchoV
//   offecho_keyword          $offEcho
//   onput_keyword            $onPut / $onPutS / $onPutV
//   offput_keyword           $offPut
//   onembedded_keyword       $onEmbeddedCode / $onEmbeddedCodeS /
//                            $onEmbeddedCodeV
//   offembedded_keyword      $offEmbeddedCode
//   echo_body                Opaque text between $onEcho and $offEcho
//   put_body                 Opaque text between $onPut and $offPut
//   embedded_body            Opaque text between $onEmbeddedCode and
//                            $offEmbeddedCode (injections.scm
//                            re-parses it as the chosen sub-language)
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
  T_ONECHO_KEYWORD,
  T_OFFECHO_KEYWORD,
  T_ONPUT_KEYWORD,
  T_OFFPUT_KEYWORD,
  T_ONEMBEDDED_KEYWORD,
  T_OFFEMBEDDED_KEYWORD,
  T_EMBEDDED_BODY,
  T_ECHO_BODY,
  T_PUT_BODY,
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

static int name_eq(const char *name, int n, const char *lit) {
  int len = (int)strlen(lit);
  if (n != len) return 0;
  return memcmp(name, lit, (size_t)len) == 0;
}

// Map the directive name to a block-control token type, or
// T_DOLLAR_DIRECTIVE_KEYWORD for any other directive (which the
// grammar parses generically via the extras path).
static TokenType classify_directive(const char *name, int n) {
  if (name_eq(name, n, "onecho") || name_eq(name, n, "onechos") ||
      name_eq(name, n, "onechov")) return T_ONECHO_KEYWORD;
  if (name_eq(name, n, "offecho")) return T_OFFECHO_KEYWORD;
  if (name_eq(name, n, "onput") || name_eq(name, n, "onputs") ||
      name_eq(name, n, "onputv")) return T_ONPUT_KEYWORD;
  if (name_eq(name, n, "offput")) return T_OFFPUT_KEYWORD;
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
  // The parser admits this external only inside a dollar_directive's
  // body; seeing it in valid_symbols is the signal to terminate.
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
  //                   the corresponding open directive's directive_end.
  //                   The scanner consumes lines until it reaches a
  //                   column-0 $off<closer>; that closing directive
  //                   itself is NOT part of the body token (mark_end
  //                   excludes it).
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
          // The parser then reports the unclosed block as an ERROR.
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
          // on the next pass.
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
    //   - $<name>   when an OFF closer is valid — we are inside a
    //               control block body, where indented $-directives
    //               are conventional (though for the on/off/embedded
    //               blocks the body is opaque and consumed before
    //               this branch fires; this matters only at the
    //               body→close boundary for the closer itself).
    int in_directive_args = valid_symbols[T_DOLLAR_DIRECTIVE_END];
    int in_control_block =
        valid_symbols[T_OFFECHO_KEYWORD] ||
        valid_symbols[T_OFFPUT_KEYWORD] ||
        valid_symbols[T_OFFEMBEDDED_KEYWORD];
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
      // Block-control directive: only emit it where the parser
      // admits it. Otherwise fall through to the generic
      // dollar_directive_keyword path so e.g. an orphan $offEcho
      // outside an on/echo block still parses (as a generic
      // directive line) instead of producing ERROR nodes. This
      // matches the "directives are extras" philosophy applied to
      // $ifthen / $endif.
      if (valid_symbols[kind]) {
        lexer->result_symbol = kind;
        return true;
      }
    }
    if (valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) {
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_KEYWORD;
      return true;
    }
    return false;
  }

  return false;
}
