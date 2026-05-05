// External scanner for tree-sitter-gams.
//
// Recognises five lexical forms that tree-sitter's regex-based lexer cannot
// express cleanly:
//
//   line_comment             — '*' in column 0 through end of line
//   block_comment_c          — /* ... */ (no nesting)
//   block_comment_dollar     — $ontext ... $offtext (case-insensitive, both
//                              leading-$ at column 0 and inline $$)
//   dollar_directive_keyword — $name or $$name; the name itself, no args
//   dollar_directive_args    — everything after the keyword through end of
//                              line, opaque so highlights stay neutral
//
// The scanner is called once per parser step, with the lookahead positioned
// before any internal whitespace skip. We therefore skip our own leading
// whitespace before checking for a marker.
//
// State machine:
//   normal -> normal               (most lookahead patterns)
//   normal -> after_directive_kw   after emitting dollar_directive_keyword
//   after_directive_kw -> normal   after emitting dollar_directive_args
//
// The `after_directive_kw` flag is serialised so incremental reparsing
// resumes correctly across edits.
//
// Compile-time invariant: the order of TokenType members must match the
// `externals` array in grammar.js exactly.

#include "tree_sitter/parser.h"
#include <stddef.h>
#include <string.h>

// Inline ASCII tolower — Zed compiles the grammar to WebAssembly, which
// does not provide libc functions (an `<ctype.h>` `tolower` import fails
// instantiation with "invalid import 'tolower'"). GAMS keywords are
// 7-bit ASCII so a hand-rolled fold suffices.
static inline int32_t ascii_tolower(int32_t c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

typedef enum {
  T_LINE_COMMENT = 0,
  T_BLOCK_COMMENT_C = 1,
  T_BLOCK_COMMENT_DOLLAR = 2,
  T_DOLLAR_DIRECTIVE_KEYWORD = 3,
  T_DOLLAR_DIRECTIVE_ARGS = 4,
} TokenType;

// No persistent state — the parser tracks "are we expecting directive
// args here?" through valid_symbols, and we use the lexer column to
// distinguish "still on the directive's line" from "moved to the next
// line". This avoids needing serialize/deserialize round-tripping
// through tree-sitter's incremental-reparse machinery.
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

bool tree_sitter_gams_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  (void)payload;

  if (!valid_symbols[T_LINE_COMMENT] &&
      !valid_symbols[T_BLOCK_COMMENT_C] &&
      !valid_symbols[T_BLOCK_COMMENT_DOLLAR] &&
      !valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD] &&
      !valid_symbols[T_DOLLAR_DIRECTIVE_ARGS]) {
    return false;
  }

  // ---- (priority 1) directive arguments: the parser admits this
  //                   external only right after a dollar_directive_keyword,
  //                   so seeing it in valid_symbols is a reliable signal
  //                   that we should try to consume the rest of the line.
  // The grammar lists \n in `extras`, so by the time scan() runs the
  // lexer may already have crossed a newline. get_column == 0 means we
  // are at the start of a new line and the directive had no args.
  if (valid_symbols[T_DOLLAR_DIRECTIVE_ARGS] &&
      lexer->get_column(lexer) > 0 &&
      lexer->lookahead != 0 && lexer->lookahead != '\n') {
    while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
      lexer->advance(lexer, false);
    }
    lexer->result_symbol = T_DOLLAR_DIRECTIVE_ARGS;
    return true;
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
       valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) &&
      lexer->lookahead == '$') {
    int col = lexer->get_column(lexer);
    lexer->advance(lexer, false);
    int double_dollar = 0;
    if (lexer->lookahead == '$') {
      double_dollar = 1;
      lexer->advance(lexer, false);
    }
    if (!double_dollar && col != 0) return false;

    // Read the full directive name. We capture only the first 6 chars
    // (the length of "ontext" / "offtext") into a small buffer for the
    // block-comment dispatch; subsequent chars are still consumed by
    // the lexer so the produced token spans the whole keyword.
    char name[7] = {0};
    int n = 0;
    while (is_id_start(lexer->lookahead) ||
           (lexer->lookahead >= '0' && lexer->lookahead <= '9')) {
      if (n < 6) name[n] = (char)ascii_tolower(lexer->lookahead);
      n++;
      lexer->advance(lexer, false);
    }
    if (n == 0) return false;

    // ontext is exactly 6 letters, immediately followed by a non-name
    // character (whitespace, newline, EOF). If something longer like
    // "ontext_x" appears, treat it as a regular directive name.
    int is_ontext = (n == 6 && memcmp(name, "ontext", 6) == 0);

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

    // ---- dollar_directive_keyword: just $<name> ----------------------
    if (valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) {
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_KEYWORD;
      return true;
    }

    return false;
  }

  return false;
}
