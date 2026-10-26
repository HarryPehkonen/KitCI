# KitCI — specification v1

You are implementing a C++17 command-line tool from scratch, test-first. Read this whole
file before writing code. Follow the stages in order. Do not skip the gate.

## 0. How to work

- **Tests first.** Write the test that fails, watch it fail for the right reason, then
  make it pass. A test that has never failed proves nothing.
- **Run the gate after every change**: `./tools/ci.sh` must print a PASS verdict.
- **Do not invent scope.** If this spec does not ask for it, do not build it. If
  something is genuinely ambiguous, write the question into `QUESTIONS.md` and take
  the simplest reading rather than guessing elaborately.
- **Report evidence, not impressions.** Paste the real gate output and the real test
  counts. Never say "all tests pass" without the line that shows it.
- Keep every file formatted and warning-free. The gate is the judge, not your taste.

## 1. What to build

`kit-ci` (repo name `KitCI`) is a single, self-contained C++17 binary: a CI gate engine.
It reads a per-repo config file (`gate.toml`, a deliberately tiny TOML subset), runs the
stages a repo declares, accumulates every failure instead of stopping at the first, and
prints one unmistakable verdict line. The same binary serves C++, Python, and Deno
repos — the only thing that varies is each repo's `gate.toml`.

Three callers, one definition:
- a human, by hand: `kit-ci`
- git hooks: `kit-ci --tier fast` (pre-commit), `kit-ci --tier full` (pre-push)
- a nightly clean-checkout job: the same binary on a fresh clone

Why a binary instead of bash: the three bash gate templates in AI-DEV-STARTER
(templates/cpp/ci.sh, templates/python/gate.sh, templates/deno/gate.sh) have drifted
apart, and AI agents editing 800-line bash gates misread them (FSMgine incidents,
2026-10-06). The engine/policy split fixes the class: engine = compiled, changes
rarely; policy = gate.toml, a visible diff in every commit.

## 2. Command-line surface (frozen)

```
kit-ci                       run default tier
kit-ci --tier fast|full      run a named tier
kit-ci --changed             scope stage file globs to files changed vs merge-base with main
kit-ci --list                print stages, tiers, and which stages are in which tier
kit-ci --ast                 print the parsed config as canonical JSON (one reading of it)
kit-ci --graph               print the flow as Mermaid text (stages, tiers, flow)
kit-ci --graph-html          print a standalone HTML page embedding the diagram
kit-ci --gate <path>         config path (default: gate.toml in repo root)
kit-ci --strict / --no-strict  strictness (default strict; see §5)
```

Exit codes: `0` all stages passed; `1` at least one stage failed; `2` config unreadable
or invalid (no stages ran). A run with zero configured stages exits `2`, not `0` — a
gate that checks nothing is a gate that lies.

## 3. The config file — gate.toml (frozen vocabulary, nothing more)

A tiny TOML subset. The parser accepts exactly the following and rejects everything
else with a 1-based line number and a message (total input contract — see §5).

```toml
[gate]
repo = "name"                 # optional, display only
strict = false                # optional, default true — false permits an EMPTY cmd
                              # (spec-owner ruling, 2026-10-06: see §3 rule 2)

[tier.fast]
stages = ["format", "lint", "tests"]

[tier.full]
stages = ["*"]                # "*" means every declared stage, in declaration order

[stage.lint]
cmd = "ruff check ."          # required — a shell string, run via /bin/sh -c
tier = "fast"                 # optional — adds the stage to that tier's list
fail_on = "nonzero"           # optional — see §4; default "nonzero"
timeout = 120                 # optional seconds; default 300; 0 = no timeout
files = "*.py"                # optional glob — stage is skipped when no
                              # --changed-scoped file matches (see --changed)
when = "tool:ruff"            # optional — stage is skipped when the tool is
                              # not on PATH ("tool:<name>")
summary = "head:40"           # optional — how much of failed output to show;
                              # default "head:40"
```

Rules the config parser enforces (each is an error with a line number):
1. Unknown key in any table.
2. A `[stage.X]` table with no `cmd` — or with an EMPTY one, unless `[gate]` sets
   `strict = false` (a stage that checks nothing is a gate that lies; the fault is reported on
   the `cmd` line).
3. `stages` naming a stage that is not declared.
4. A `tier` value naming a tier that has no `[tier.X]` table.
5. `fail_on` not one of the allowed values (§4).
6. `timeout` not an integer ≥ 0.
7. Duplicate table headers (a second `[stage.lint]`).
8. Values of the wrong type (`cmd` must be a string, `stages` an array of strings,
   `timeout` an integer).
9. A `[tier.X]` table with no `stages` key.
10. A stage listed twice within one tier's `stages`.

The vocabulary is deliberately frozen: if the bash gates do not express it, KitCI has
no keyword for it. Escape hatch for anything exotic: put it in `cmd` — a stage whose
`cmd` is any shell string. No new config language feature may be added in v1.
Two things were added by a SPEC-OWNER ruling rather than by a worker, and both are recorded in
`QUESTIONS.md` Q30: `[gate] strict = false` (the one key that lets a config declare an empty
`cmd` deliberate instead of a fault), and the boolean literal that key takes — exactly `true`
and `false`, unquoted, and nowhere else in the vocabulary.

## 4. Stage semantics (frozen)

- Stages run in **tier order**: the tier's `stages` list order; `"*"` means declaration
  order.
- **Every stage runs.** A failing stage does not stop the run — all failures
  accumulate, and the verdict names every culprit. This is the existing kit gates'
  behavior and it is load-bearing: one run tells you everything that is wrong.
- `fail_on` values:
  - `"nonzero"` (default): non-zero exit is a failure.
  - `"output:<regex>"`: failure when stdout+stderr matches the regex (e.g. pytest's
    vacuous-green case: `fail_on = "output:^no tests ran"`).
  - `"exit:<n>"`: failure when the exit code is exactly `<n>` — combined implicitly
    with nonzero: `exit:5` means "exit 5 fails AND any other nonzero fails". (The
    Python gate treats pytest exit 5 — no tests collected — as a failure; this is how
    that is expressed.)
- `timeout`: the stage is killed and fails with `timed out after Ns`.
- `when = "tool:<name>"`: skipped (not failed, not counted) when `<name>` is not on PATH.
- `files = "<glob>"` (only meaningful with `--changed`): when `--changed` is given
  and no changed file matches the glob, the stage is **skipped**; without `--changed`
  the stage always runs. Glob syntax: `*`, `?`, and `**` across path separators,
  matching the fnmatch family.
- Output of a failed stage: first `summary` lines (default 40) of combined output,
  indented, plus the full command so the human can re-run it.

## 5. Total parser contract — never undefined behaviour

Any byte string is either a valid `gate.toml` or an error with a 1-based line number.
The same for the `--graph`/`--graph-html` output: both are pure functions of the
parsed config. The TOML-subset parser is fuzzed (§6) with the same discipline as
FSMgine's: any crash, hang, or missing line number is a bug in the parser, never an
acceptable input.

Error message style: short, lowercase, names the fault — `unknown key 'colur' in
[stage.lint] (line 4)`.

## 6. The gate (no exceptions, no hosted CI)

Follow AI-DEV-STARTER's PLUNK-IN for the C++ template, adapted:
- `./tools/ci.sh` exists with stages: `format -> build -> lint -> tests -> fuzz`
- `build`: both **gcc and clang**, no warnings. Warnings are errors.
- `lint`: whatever the kit configures. Fix findings in the code, never by widening
  the configuration.
- `tests`: all of them, and the count must be printed.
- `fuzz`: the TOML-subset parser as a libFuzzer target, built with
  `-fsanitize=fuzzer,address,undefined`, run for **60 seconds** with a checked-in
  corpus. Any artifact is a failure to investigate, not to delete.
- `./tools/ci.sh` with no arguments runs all of the above and prints an unmistakable
  verdict line. `--list` prints the stage set (FSMgine incident, 2026-10-06: a tool
  that must read gate source to learn its stages is a bug).
- **Do not add `.github/workflows/` or any hosted CI.** Gates live in the repo.
- Commit hooks: the kit's, wired and working. Do not commit with `--no-verify`.

## 7. The config, presented: graphs and the canonical AST (the part Harri asked for by name)

Every mode here is a **reading of one parse** — the same parse the runner runs, never a second
one and never a stale drawing — and every one of them is byte-stable for a given config (same
config in, same bytes out) so the output can be golden-file tested and diffed. A mode refuses
to be one of two answers at once: `--list`, `--graph` and `--ast` are three different outputs,
and asking for two of them is exit `2`.

### 7.1 The graphs

Two graph modes, both derived from the parsed config:
- `--graph`: Mermaid `flowchart` text. One node per stage, colored by tier membership,
  edges in run order. A human pastes it into any Mermaid renderer.
- `--graph-html`: a **standalone HTML page** — no JavaScript, no external requests,
  inline SVG rendering of the same flow (style after the kit-ci-flow.html prototype:
  dark background, box per stage, arrows in run order, a legend). It must open
  directly from a file:// URL with no network access.

The stage-C amendment adds `--graph --format mermaid|html|dot` (`mermaid` is the default,
`--graph-html` stays as the documented shorthand for `--graph --format html`), and an unknown
or missing format name is exit `2` (QUESTIONS.md Q17).

### 7.2 Node annotations: a default is what NO annotation means

A node annotates exactly what a stage **deviates** on, and nothing else. The reader's rule is
"no annotation, default setting", so what is written on a node is precisely the list of ways
that stage is not the standard one — and a stage that writes nothing standard carries no
annotation anywhere. The annotatable settings are the five with a default or an absent state:
`fail_on`, `timeout`, `files`, `when`, `summary`. `cmd` is never an annotation (every stage has
one) and neither is `tier` (membership is already the colour, the cluster and the label).
Each format spends the room it has:
- **Mermaid**: a compact second line in the node label, after `<br/>` — a stage with a default
  is a one-line label.
- **HTML**: a detail panel under the diagram, one row per deviating stage. The SVG box has no
  room for a second line, and the page is standalone already.
- **dot**: the node's own `comment` attribute, which already carried the stage's tier list.

Annotations are text escaped by the same rules as every other label (SPEC.md §5), and the HTML
keeps its byte-level self-containment: an annotation carrying a URL is written with an entity,
like the repo name (Q29).

### 7.3 `--ast` — the parsed config as canonical JSON

`--ast` prints the whole parsed config as canonical JSON on stdout and exits `0`. It is the
machine's reading of one parse, for diffing, for a tool that wants the policy as data, and for
`jq`. The form is fixed, not a matter of taste:

- one JSON document, newline-terminated, 2-space indentation, one array element per line;
- top-level keys in this order: `gate`, `tiers`, `stages`;
- `gate`: `repo` (a string, or `null` when the config did not write one) and `strict`;
- `tiers`: **declaration order**, each `{name, stages}`, where `stages` is the run order a run
  of that tier would use — `"*"` already expanded and `tier = "..."` membership already
  included, i.e. exactly what `resolved_stages()` returns and what `--list` prints for it;
- `stages`: **declaration order** — the `--list` order, not alphabetical — each with every
  attribute the grammar documents: `name`, `cmd`, `tier`, `fail_on`, `timeout`, `files`,
  `when`, `summary`;
- **defaults are materialised** (the runner's view of "what will this gate actually do"): a
  stage that wrote no `fail_on` shows `"fail_on": "nonzero"`, no `timeout` shows
  `"timeout": 300`, no `summary` shows `"summary": "head:40"`;
- the optional strings with no default action (`tier`, `files`, `when`) are `null` when the
  config did not write them — which is exactly how the runner reads the `""` the parser stores;
- **semantic only**: no line numbers and no source positions. The grammar appendix (below)
  carries the positional truth; `--ast` carries the meaning;
- escaping is JSON's own (RFC 8259): a quote or a backslash in `cmd` is escaped, no key is
  dropped, and no config the parser accepted is refused. A config that does not parse is the
  parser's error, exit `2`, with no partial document.

## 8. Tests to write first (frozen list — these names, this meaning)

Config parsing:
- `ParsesMinimalConfig`
- `ParsesEveryDocumentedKey`
- `RejectsUnknownKey` / `RejectsStageWithoutCmd`
- `RejectsUnknownStageInTier` / `RejectsTierNotDeclared`
- `RejectsBadFailOn` / `RejectsBadTimeout`
- `RejectsDuplicateTable` / `RejectsWrongValueType`
- `RejectsTierWithoutStages` / `RejectsStageTwiceInTier`
- `ReportsTheRightLineNumber` — the fault is on line 6, the error says 6.

Runner:
- `RunsStageAndPassesOnZeroExit`
- `FailsAndKeepsGoing` — two failing stages, both named in the verdict, exit 1.
- `ExitFiveIsFailureWithExitFailOn` — a stage exiting 5 with `fail_on = "exit:5"`.
- `OutputRegexFailOnMatches`
- `TimeoutKillsStage`
- `ToolMissingSkipsStage` — skipped, not failed.
- `ChangedGlobSkipsStage` / `ChangedGlobRunsStageOnMatch`
- `ZeroStagesExitsTwo`

Graphs:
- `GraphIsStableForSameConfig` — two parses, identical bytes.
- `GraphListsEveryStage` / `GraphHtmlIsSelfContained` — no `http://` or `https://`
  references in the HTML output.

Graphs (v1.1):
- `GraphAnnotatesNonDefaultAttributes` — `fail_on "exit:5"`, `timeout 60`, `when "tool:ruff"`
  and `files "*.py"` appear on the node in each format, and a stage carrying every default
  carries no annotation in any of them.
- `GraphAnnotationsAreStable` — annotated output is byte-stable too, and still one whole
  document per format.

The canonical AST (v1.1):
- `AstIsStableForSameConfig` — the same `gate.toml` parsed twice, byte-identical stdout.
- `AstMaterializesDefaults` — a stage that omits `timeout`/`fail_on` shows them resolved
  (`"nonzero"`, `300`) in the output.
- `AstCoversEveryDocumentedKey` — every stage attribute in the grammar appears in the output
  for a fully-specified config (`cmd`, `tier`, `fail_on`, `timeout`, `files`, `when`,
  `summary`; `[gate]`'s `repo` and `strict`; tiers and their stage lists).
- `AstRejectsOrEscapesNothingNew` — `--ast` on a config that fails to parse exits `2` with the
  parser's own error and no partial JSON.

CLI:
- `ListAnswersWithoutReadingSource` — `--list` output for a fixture config, parsed
  and compared structurally.

## 9. Stages of work

Build them in order. **Stop at the end of each stage and report.** Do not start the
next stage in the same sitting.

- **Stage A: the parser + gate.** TOML subset, error contract, frozen parsing tests,
  ci.sh with all five stages green, fuzz target running 60s clean.
- **Stage B: the runner.** Stage execution, fail_on semantics, timeouts, `when`/`files`
  skipping, verdict line, exit codes, frozen runner tests.
- **Stage C: the graphs.** `--graph` (Mermaid) and `--graph-html` (standalone HTML),
  frozen graph tests, byte-stability.
- **Stage D: fleet integration.** Convert ONE repo (docsum — smallest `scripts/gate.sh`
  fleet member) to `gate.toml` + a 3-line hook wrapper. Verify by hand and by the
  nightly-job pattern. This stage touches another repo — flag it in the report; the
  fleet-wide conversion is NOT in scope for v1.

## 10. Acceptance for stage A

- `./tools/ci.sh` prints its PASS verdict, with the fuzz stage having run 60 seconds.
- Every test in §8 that stage A covers (parsing + CLI list) exists, under that name,
  and passes.
- A `REPORT.md` exists, filled in as described below.

## 11. REPORT.md — the only narration allowed

Fill in exactly these fields, with real values copied from real runs:

    Gate verdict line (verbatim):
    Test count:
    Fuzz stage: runs / seconds / artifacts
    Stage reached:
    What I could not do:
    What I had to guess:      (with the question, or "nothing")

Do not grade yourself, do not summarise your approach, do not describe the code. The
gate decides whether this worked; your job is to report what it did. If you could not
finish a stage, say which part and stop — an honest stop is a valid result, and a
claim you cannot back with gate output is not.

## Appendix A — the grammar of `gate.toml` (EBNF)

The parser is the reference implementation; this appendix is the same contract written down, so
that the whole syntax can be read in one place. It was written *after* the parser and changed
nothing: where the two disagree, the parser is the bug to fix. This is the **syntax** only —
§3's ten rules are the semantic checks sitting on top of it, which is why a bare `true` is a
well-formed `value` here and still a fault as a `cmd`.

    gate.toml   = { line } ;
    line        = blank | table | entry ;

    blank       = { ws } , end-of-line ;

    table       = { ws } , "[" , head , "]" , { ws } , end-of-line ;
    head        = "gate"
                | ( "stage" | "tier" ) , "." , ident ;

    entry       = { ws } , key , { ws } , "=" , { ws } , value , { ws } , end-of-line ;
    key         = ident ;

    ident       = ident-char , { ident-char } ;
    ident-char  = letter | digit | "_" | "-" ;
    letter      = "A"-"Z" | "a"-"z" ;
    digit       = "0"-"9" ;

    value       = string | array | integer | boolean ;
    string      = '"' , { string-byte | escape } , '"' ;
    string-byte = ? any byte except '"' , "\" , and the line's end of line ? ;
    escape      = "\" , ? any byte, taken literally ? ;
    array       = "[" , { ws } , [ item , { { ws } , "," , { ws } , item } , [ "," ] ] ,
                  { ws } , "]" ;
    item        = string ;
    integer     = [ "-" ] , digit , { digit } ;
    boolean     = "true" | "false" ;

    ws          = " " | "\t" | "\r" ;
    end-of-line = "\n" | end-of-input ;

What the productions do not say, and the parser does — each of these is a fact about the
implementation, checked against it rather than inferred from it:

1. **A line is one line.** `end-of-line` is the only place a value ends, so neither a `string`
   nor an `array` ever spans lines: a `[` on one line and its items on the next is
   `unterminated array`.
2. **A comment is stripped before the rest of the line is read.** The `#` that starts a comment
   is the first `#` that is not inside a `string`, and inside a `string` a `\` makes the next
   byte part of the string rather than the `#` a comment. What follows a `#` is never parsed at
   all, so `[gate#` is read as a `table` with its `]` missing (the header fault), not as a
   comment after a valid header. A `#` inside a string is an ordinary byte (`cmd = "a#b"`).
3. **An `escape` takes the next byte literally, whatever it is.** `\"` is a `"`, `\\` is a `\`
   and `\n` is the letter `n` — not a newline. A `\` immediately before the closing `"` escapes
   it, so the string has no terminator and the fault is `unterminated string`. This is the one
   place the subset deliberately does not follow TOML's escape set (`\n`, `\t`, `\uXXXX`, …):
   the free-text values are shell strings, `/bin/sh` does its own interpreting, and a `cmd` that
   wants a newline can write a shell escape. Recorded, not changed, in QUESTIONS.md.
4. **Trailing characters are a fault, never partly read.** After a `table`'s `]` and after an
   `entry`'s `value`, only `ws` may follow. `[gate]x` is rejected on its own line; for
   `cmd = "a" "b"` the entry is never recorded, so the stage it belonged to ALSO has no `cmd` —
   and since the `[stage.X]` header is on an earlier line, §5's earliest-line rule is what
   reports it (`stage 'x' has no cmd`), not the trailing-character fault. Both faults are real;
   only one can be the answer.
5. **`entry` is context-sensitive and the grammar above is flat.** An `entry` belongs to the
   most recent preceding `table`; a key before the first table header is
   `key '<k>' outside any table`. `[gate]` may appear anywhere in the file (at most once), and
   the entries that follow each table header are that table's, in file order.
6. **Tables may appear in any order**, and the cross-references — a `tier` naming a tier with
   no `[tier.X]`, `stages` naming a stage that is not declared — are checked after the whole
   file has been read. When a file holds several faults, the one reported is the one on the
   EARLIEST line (QUESTIONS.md Q2); ties go to the fault found first.
7. **`ident` is `[A-Za-z0-9_-]+` and nothing else.** `[ stage.x ]`, an empty name (`[stage.]`)
   and a three-part header (`[stage.a.b]`) are all faults, while `[stage.9]` and `[stage.-]`
   are legal names. The free-text keys (`repo`, `cmd`, `fail_on`, `files`, `when`, `summary`)
   are NOT idents: they are `string`s, and anything the shell — or, for `output:<regex>`, the
   regex engine — accepts fits in them.
8. **One rule here is conditional on another line.** An empty (or `ws`-only) `cmd` is a fault
   unless `[gate]` says `strict = false`, in which case the same bytes parse (§3 rule 2, the
   spec-owner ruling in QUESTIONS.md Q30). It is the only production whose outcome depends on a
   different line of the same file.

