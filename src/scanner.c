// External scanner for tree-sitter-gams.
//
// Recognises four GAMS lexical forms that tree-sitter's regex-based lexer
// cannot express cleanly:
//
//   line_comment         — '*' in column 0 through end of line
//   block_comment_c      — /* ... */ (no nesting)
//   block_comment_dollar — $ontext ... $offtext (case-insensitive, both
//                          leading-$ at column 0 and inline $$)
//   dollar_directive     — $name [args...] through end of line, where name
//                          is anything other than ontext/offtext. Anchored
//                          at column 0 with single $, anywhere with $$.
//
// The scanner is called once per parser step, with the lookahead positioned
// before any internal whitespace skip. We therefore skip our own leading
// whitespace before checking for a marker.
//
// Compile-time invariant: the order of TokenType members must match the
// `externals` array in grammar.js exactly.

#include "tree_sitter/parser.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

typedef enum {
  T_LINE_COMMENT = 0,
  T_BLOCK_COMMENT_C = 1,
  T_BLOCK_COMMENT_DOLLAR = 2,
  T_DOLLAR_DIRECTIVE = 3,
} TokenType;

void *tree_sitter_gams_external_scanner_create(void) { return NULL; }
void tree_sitter_gams_external_scanner_destroy(void *p) { (void)p; }
unsigned tree_sitter_gams_external_scanner_serialize(void *p, char *buf) {
  (void)p; (void)buf; return 0;
}
void tree_sitter_gams_external_scanner_deserialize(void *p, const char *buf,
                                                   unsigned n) {
  (void)p; (void)buf; (void)n;
}

// Advance through the keyword `word` (case-insensitive). On full match the
// lexer is positioned past the word. On any mismatch the lexer is positioned
// somewhere in the middle (caller must accept that or design accordingly).
static int match_word_ci(TSLexer *lexer, const char *word) {
  for (const char *p = word; *p; ++p) {
    if (lexer->lookahead == 0) return 0;
    if (tolower(lexer->lookahead) != tolower((unsigned char)*p)) return 0;
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

bool tree_sitter_gams_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  (void)payload;

  if (!valid_symbols[T_LINE_COMMENT] &&
      !valid_symbols[T_BLOCK_COMMENT_C] &&
      !valid_symbols[T_BLOCK_COMMENT_DOLLAR] &&
      !valid_symbols[T_DOLLAR_DIRECTIVE]) {
    return false;
  }

  skip_whitespace(lexer);

  // ---- line comment: '*' in column 0 ---------------------------------
  if (valid_symbols[T_LINE_COMMENT] &&
      lexer->lookahead == '*' &&
      lexer->get_column(lexer) == 0) {
    lexer->advance(lexer, false);
    while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
      lexer->advance(lexer, false);
    }
    lexer->result_symbol = T_LINE_COMMENT;
    return true;
  }

  // ---- C-style block comment: /* ... */ ------------------------------
  if (valid_symbols[T_BLOCK_COMMENT_C] && lexer->lookahead == '/') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '*') return false;
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

  // ---- $ entrypoint: dispatch between $ontext block and $directive ----
  if ((valid_symbols[T_BLOCK_COMMENT_DOLLAR] ||
       valid_symbols[T_DOLLAR_DIRECTIVE]) &&
      lexer->lookahead == '$') {
    int col = lexer->get_column(lexer);
    lexer->advance(lexer, false);
    int double_dollar = 0;
    if (lexer->lookahead == '$') {
      double_dollar = 1;
      lexer->advance(lexer, false);
    }
    if (!double_dollar && col != 0) return false;

    // Read up to 7 chars of the directive name (ontext/offtext are 6).
    char name[8] = {0};
    int n = 0;
    while (n < 7 && (is_id_start(lexer->lookahead) ||
                     (lexer->lookahead >= '0' && lexer->lookahead <= '9'))) {
      name[n++] = (char)tolower(lexer->lookahead);
      lexer->advance(lexer, false);
    }
    if (n == 0) return false;

    int is_ontext  = (n == 6 && memcmp(name, "ontext", 6) == 0);

    // ---- block_comment_dollar: scan to matching $offtext --------------
    if (is_ontext && valid_symbols[T_BLOCK_COMMENT_DOLLAR]) {
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

    // ---- dollar_directive: consume to end of line --------------------
    if (valid_symbols[T_DOLLAR_DIRECTIVE]) {
      while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
        lexer->advance(lexer, false);
      }
      lexer->result_symbol = T_DOLLAR_DIRECTIVE;
      return true;
    }

    return false;
  }

  return false;
}
