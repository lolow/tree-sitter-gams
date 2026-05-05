# Changes from upstream

This fork tracks [`Schlegen/tree-sitter-gams`](https://github.com/Schlegen/tree-sitter-gams).
Upstream commit pinned in `NOTICE`: `78dd717` (2025-12-29).

## v0.1.0 — 2026-05-05

### Comments — replaced
The upstream `comment` rule (`#`-prefixed, single-line) was incorrect for
GAMS. This fork replaces it with three real GAMS comment forms via an
external scanner (`src/scanner.c`):

- `line_comment` — `*` in column 0 through end of line
- `block_comment_dollar` — `$ontext … $offtext` (case-insensitive, both
  leading-`$` at column 0 and inline `$$` forms)
- `block_comment_c` — `/* … */`

The scanner skips its own leading whitespace because tree-sitter calls
externals once per parser step, before the lexer's whitespace skip.

### Added — top-level forms

- `dollar_directive` — `$<name>` (column 0) or `$$<name>` (anywhere)
  consumed through end of line. Recognised as a top-level node alongside
  `statement` in `source_file`. Dispatches to `block_comment_dollar`
  when the name is `ontext`.
- `equation_definition` — `name[(domain)][\$cond] .. lhs <op> rhs ;` with
  `=e=/=l=/=g=/=n=/=x=/=c=/=b=`. Required a new `conflicts` declaration
  between `identifier_with_domain_args` and `index_element`.
- `table_declaration` — keyword + name + optional description +
  opaque `table_body` token through next `;`. The 2D layout is
  intentionally not modelled.
- `option_statement` — `option <name>=<value>[, …]`.
- `abort_statement` — `abort[.noError] <items>`.
- `acronym_declaration` — `acronym(s) <name>[, …]`.

### Added — expression-level

- `macro_ref` — `%name%` and `%digit+` positional macros, available
  as an expression alternative.
- `model_type` — case-insensitive choice over the 15 GAMS solver types
  (`lp/nlp/mip/rmip/minlp/rminlp/qcp/miqcp/rmiqcp/mcp/mpec/rmpec/dnlp/cns/emp`),
  recognised after `using` in `solve_statement`. `prec(1)` to win the
  lexer tie-break against `identifier`.

### Extended

- `bool` — promoted to a literal-constant rule covering `yes/no/inf/na/eps`
  (case-insensitive). `scalar_value_block` now accepts either a number or
  a literal so `scalar pi /eps/;` parses.
- `solve_statement` — direction + objective made optional in the
  `using <type>` form so `solve m using mcp;` no longer requires a
  `maximizing`/`minimizing` clause.
- `variable_attribute_keyword` — added equation suffixes (`range`,
  `slack`, `slacklo`, `slackup`, `infeas`) and the missing variable
  suffixes (`prior`, `stage`).
- `element_entry` — accepts both `set_element` (allows hyphens like
  `san-diego`) and `identifier`. Without this, the canonical GAMS
  `transport.gms` example would ERROR on `san-diego`.

### Test fixtures
Upstream `test/*.gms` files were written for the broken `#`-comment rule.
Every leading `#` was converted to a column-1 `*` so the fixtures match
real GAMS syntax.

### Build

- `tree-sitter.json` added so `tree-sitter generate` uses ABI 15 by
  default.
- `.gitignore` narrowed from `src/*` to the four generated artefacts so
  hand-written `src/scanner.c` is committed.

### Coverage gate

The canonical GAMS `transport.gms` example (50 lines, sets w/ hyphenated
elements, parameters, table, scalar, parameter assignment, variables w/
modifier, equations declaration, three equation definitions with
`=E=/=L=/=G=`, model + solve + display) parses with **zero ERROR
nodes**.

Remaining ERROR nodes in upstream `test/*.gms` (in `assignments`,
`code_sample`, `control_flow`, `display`, `parameters`) are pre-existing
upstream gaps in expression and parameter-data-block parsing — out of
scope for this release.
