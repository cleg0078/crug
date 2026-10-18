# Feature-flag fixtures (`crug.dg/flags/`)

The Phase 3 exit gate compiles these files under every accepted combination of
`-fcrug-*` options.  They are not run through DejaGnu yet: the `crug.dg` suite
driver arrives with the ownership front end, at which point each file becomes a
`dg-options` test with a fixed expected outcome.

| File | What it pins |
| --- | --- |
| `words-as-identifiers.c` | every contextual word is still an ordinary C identifier under every flag combination (ADR 0018 D1, RFC-0001 section 13.2) |
| `macro-regression.c` | macros may use Crug words and shadow the intrinsic readings (ADR 0018 D3, D9) |
| `trivial.c` | the smallest unit; used for option acceptance and `-dM -E` macro probes |

ADR 0018 D1 also names `off-tokens-1.c`, `off-parse-1.c`, `monotone-borrow-1.c`,
`closure-channels-1.c`, `closure-modern-1.c` and `kernel-no-go-1.c`.  Those are
outcome assertions (accepted or rejected, with which diagnostic), so they need
the DejaGnu driver and are not written yet; `scripts/check-crug-flags.sh`
asserts the same facts for now.
