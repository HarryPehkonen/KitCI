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
2. A `[stage.X]` table with no `cmd`.
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

## 7. Graph outputs (the part Harri asked for by name)

Two graph modes, both derived from the parsed config — never a stale drawing:
- `--graph`: Mermaid `flowchart` text. One node per stage, colored by tier membership,
  edges in run order. A human pastes it into any Mermaid renderer.
- `--graph-html`: a **standalone HTML page** — no JavaScript, no external requests,
  inline SVG rendering of the same flow (style after the kit-ci-flow.html prototype:
  dark background, box per stage, arrows in run order, a legend). It must open
  directly from a file:// URL with no network access.

Both must be byte-stable for a given config (same config in, same bytes out) so they
can be golden-file tested and diffed.

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
