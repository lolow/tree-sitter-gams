// External scanner for tree-sitter-gams.
//
// Recognises five lexical forms that tree-sitter's regex-based lexer cannot
// express cleanly:
//
//   line_comment             — '*' in column 0 through end of line, OR the
//                              configured end-of-line-comment marker (set
//                              by `$eolcom <marker>`) through end of line
//   block_comment_c          — /* ... */ (no nesting)
//   block_comment_dollar     — $ontext ... $offtext (case-insensitive, both
//                              leading-$ at column 0 and inline $$)
//   dollar_directive_keyword — $name or $$name; the name itself, no args
//   dollar_directive_args    — everything after the keyword through end of
//                              line, opaque so highlights stay neutral
//
// Persistent state (round-tripped through serialize/deserialize):
//   eol_marker[3], eol_marker_len  — current $eolcom marker; len==0 means
//                                    EOL comments are disabled. Set by
//                                    `$eolcom <chars>` and cleared by
//                                    `$offEolCom`.
//
// The scanner is called once per parser step, with the lookahead positioned
// before any internal whitespace skip. We therefore skip our own leading
// whitespace before checking for a marker.
//
// Compile-time invariant: the order of TokenType members must match the
// `externals` array in grammar.js exactly.

#include "tree_sitter/parser.h"
#include <stddef.h>
#include <string.h>

// Inline ASCII tolower — Zed compiles the grammar to WebAssembly, which
// does not provide libc functions (an `<ctype.h>` `tolower` import fails
// instantiation with "invalid import 'tolower'").
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

#define EOL_MARKER_MAX 3

typedef struct {
  unsigned char eol_marker[EOL_MARKER_MAX];
  unsigned char eol_marker_len;  // 0 = EOL comments disabled
} ScannerState;

// Single static state: tree-sitter is single-threaded per parser, and
// `serialize`/`deserialize` keep the value consistent across rollbacks
// during incremental reparse. Static avoids needing libc malloc, which
// is unavailable in the WASM target.
static ScannerState g_state;

void *tree_sitter_gams_external_scanner_create(void) {
  g_state.eol_marker[0] = 0;
  g_state.eol_marker[1] = 0;
  g_state.eol_marker[2] = 0;
  g_state.eol_marker_len = 0;
  return &g_state;
}
void tree_sitter_gams_external_scanner_destroy(void *p) { (void)p; }

unsigned tree_sitter_gams_external_scanner_serialize(void *p, char *buf) {
  (void)p;
  buf[0] = (char)g_state.eol_marker[0];
  buf[1] = (char)g_state.eol_marker[1];
  buf[2] = (char)g_state.eol_marker[2];
  buf[3] = (char)g_state.eol_marker_len;
  return 4;
}
void tree_sitter_gams_external_scanner_deserialize(void *p, const char *buf,
                                                   unsigned n) {
  (void)p;
  if (n >= 4) {
    g_state.eol_marker[0] = (unsigned char)buf[0];
    g_state.eol_marker[1] = (unsigned char)buf[1];
    g_state.eol_marker[2] = (unsigned char)buf[2];
    g_state.eol_marker_len = (unsigned char)buf[3];
  } else {
    g_state.eol_marker[0] = 0;
    g_state.eol_marker[1] = 0;
    g_state.eol_marker[2] = 0;
    g_state.eol_marker_len = 0;
  }
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

  // ---- (priority 1) directive arguments ---------------------------------
  // The parser admits this external only right after a
  // dollar_directive_keyword, so seeing it in valid_symbols is a reliable
  // signal that we should consume the rest of the directive's line.
  // The grammar has \n in `extras`, so by the time scan() runs the lexer
  // may already have crossed a newline. get_column == 0 means we are at
  // the start of a new line and the directive had no args.
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

  // ---- (priority 2) end-of-line comment via $eolcom marker ------------
  // Only fires when an `$eolcom <marker>` directive earlier in the file
  // has activated this state. Tries marker[0]; if marker is multi-char,
  // advances and checks the rest. On a non-match the scanner returns
  // false and tree-sitter restores the position, so other tokens get
  // their normal chance.
  if (g_state.eol_marker_len > 0 && valid_symbols[T_LINE_COMMENT] &&
      lexer->lookahead == g_state.eol_marker[0]) {
    lexer->advance(lexer, false);
    int matched = 1;
    for (int i = 1; i < g_state.eol_marker_len; i++) {
      if (lexer->lookahead != g_state.eol_marker[i]) { matched = 0; break; }
      lexer->advance(lexer, false);
    }
    if (matched) {
      while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
        lexer->advance(lexer, false);
      }
      lexer->result_symbol = T_LINE_COMMENT;
      return true;
    }
    // Marker didn't fully match; fall through. The lexer position resets
    // when we return false below.
    return false;
  }

  // ---- column-1 '*' line comment --------------------------------------
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

    // Read the full directive name. We capture the first 9 chars (the
    // length of "offeolcom") into a buffer for keyword dispatch; the
    // lexer keeps advancing past the cap so the emitted token spans
    // the whole keyword.
    char name[10] = {0};
    int n = 0;
    while (is_id_continue(lexer->lookahead)) {
      if (n < 9) name[n] = (char)ascii_tolower(lexer->lookahead);
      n++;
      lexer->advance(lexer, false);
    }
    if (n == 0) return false;

    int is_ontext  = (n == 6 && memcmp(name, "ontext", 6) == 0);
    int is_eolcom  = (n == 6 && memcmp(name, "eolcom", 6) == 0);
    int is_offeol  = (n == 9 && memcmp(name, "offeolcom", 9) == 0);

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

    // ---- $eolcom <marker>: capture the marker into scanner state ----
    // GAMS allows the marker to be 1–3 non-whitespace characters. The
    // emitted keyword token covers only `$eolcom`; mark_end locks that
    // span before we look at the marker chars. Reading the marker
    // advances the lexer further so those bytes become part of the
    // dollar_directive_args token on the next scan call.
    if (is_eolcom && valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) {
      lexer->mark_end(lexer);
      // Skip spaces/tabs (NOT newline) between keyword and marker.
      while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, false);
      }
      int m = 0;
      unsigned char marker[EOL_MARKER_MAX] = {0};
      while (m < EOL_MARKER_MAX && lexer->lookahead != 0 &&
             lexer->lookahead != '\n' && lexer->lookahead != ' ' &&
             lexer->lookahead != '\t') {
        marker[m++] = (unsigned char)lexer->lookahead;
        // Don't advance: leave the marker chars in the input so the
        // args branch consumes them on the next scan call.
        // We need to peek ahead for multi-char markers though, so
        // advance and rely on tree-sitter restoring position when we
        // return after mark_end.
        lexer->advance(lexer, false);
      }
      if (m > 0) {
        for (int i = 0; i < EOL_MARKER_MAX; i++) g_state.eol_marker[i] = marker[i];
        g_state.eol_marker_len = (unsigned char)m;
      }
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_KEYWORD;
      return true;
    }

    // ---- $offEolCom: turn off EOL comments ---------------------------
    if (is_offeol && valid_symbols[T_DOLLAR_DIRECTIVE_KEYWORD]) {
      g_state.eol_marker_len = 0;
      lexer->result_symbol = T_DOLLAR_DIRECTIVE_KEYWORD;
      return true;
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
