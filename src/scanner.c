// External scanner for tree-sitter-gams.
//
// Recognises three GAMS comment forms that tree-sitter's regex-based lexer
// cannot express cleanly:
//
//   line_comment         — '*' in column 0 through end of line
//   block_comment_c      — /* ... */ (no nesting)
//   block_comment_dollar — $ontext ... $offtext (case-insensitive, both
//                          leading-$ at column 0 and inline $$)
//
// The scanner is called once per parser step, with the lookahead positioned
// before any internal whitespace skip. We therefore skip our own leading
// whitespace before checking for a comment marker. `mark_end` is used so
// that even when the scanner consumes leading whitespace, the produced
// token's range starts at the marker (not at the leading whitespace).
//
// Compile-time invariant: the order of TokenType members must match the
// `externals` array in grammar.js exactly.

#include "tree_sitter/parser.h"
#include <ctype.h>
#include <stddef.h>

typedef enum {
  T_LINE_COMMENT = 0,
  T_BLOCK_COMMENT_C = 1,
  T_BLOCK_COMMENT_DOLLAR = 2,
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

// Advance through the keyword `word` (case-insensitive).
// Returns 1 on full match, 0 if any character mismatches.
static int match_word_ci(TSLexer *lexer, const char *word) {
  for (const char *p = word; *p; ++p) {
    if (lexer->lookahead == 0) return 0;
    if (tolower(lexer->lookahead) != tolower((unsigned char)*p)) return 0;
    lexer->advance(lexer, false);
  }
  return 1;
}

// Skip ASCII whitespace at the current position, advancing the lexer.
// `skip=true` so these chars are not included in the produced token.
static void skip_whitespace(TSLexer *lexer) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    lexer->advance(lexer, true);
  }
}

bool tree_sitter_gams_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  (void)payload;

  // Only run when at least one of our externals is in scope.
  if (!valid_symbols[T_LINE_COMMENT] &&
      !valid_symbols[T_BLOCK_COMMENT_C] &&
      !valid_symbols[T_BLOCK_COMMENT_DOLLAR]) {
    return false;
  }

  // Skip leading whitespace so the upstream `\s`-extras and the column
  // computation don't fight us. After this, get_column() reflects the
  // column of the first non-whitespace character.
  skip_whitespace(lexer);

  // ---- (1) line comment: '*' in column 0 -----------------------------
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

  // ---- (2) C-style block comment: /* ... */ --------------------------
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

  // ---- (3) dollar block comment: $ontext ... $offtext ----------------
  // Open marker accepted with single '$' at column 0, or with '$$' anywhere.
  if (valid_symbols[T_BLOCK_COMMENT_DOLLAR] && lexer->lookahead == '$') {
    int col = lexer->get_column(lexer);
    lexer->advance(lexer, false);
    int double_dollar = 0;
    if (lexer->lookahead == '$') {
      double_dollar = 1;
      lexer->advance(lexer, false);
    }
    if (!double_dollar && col != 0) return false;
    if (!match_word_ci(lexer, "ontext")) return false;
    // Body: scan until matching $offtext or $$offtext.
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
        // Not a closing marker — keep scanning.
      } else {
        lexer->advance(lexer, false);
      }
    }
  }

  return false;
}
