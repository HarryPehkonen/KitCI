# KitCI

A CI gate engine in one self-contained C++17 binary. It reads a per-repo `gate.toml`, runs
the stages that repo declares, accumulates every failure instead of stopping at the first,
and prints one unmistakable verdict line. The same binary serves C++, Python and Deno repos —
the only thing that varies is each repo's `gate.toml`.

`SPEC.md` is the frozen contract. This README says how to build and run what exists today.

## Status

**Stages A and B — the parser and the runner.** `kit-ci` parses `gate.toml` (every documented
key, every rejection rule, a 1-based line number on every error), answers `--list`, and runs a
tier: every stage in run order, `fail_on`, `timeout`, the `when` and `--changed`/`files` skips,
one verdict line and one exit code. The graph modes (Stage C) and the fleet conversion
(Stage D) are named in `SPEC.md` §9 and are not implemented yet: asking for them exits `2` with
a message saying so. That exit code is the honest one — `0` would claim a run that did not
happen.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure    # 36 tests
./build/kit-ci --help
```

`cmake -DKITCI_BUILD_FUZZ=ON` (clang) builds the libFuzzer target instead of the object
library; `-DKITCI_BUILD_TESTS=OFF` skips the suite.

## Running the stages

```bash
./build/kit-ci                    # the default tier: "full" if declared, else the first
./build/kit-ci --tier fast        # a named tier
./build/kit-ci --changed          # scope the `files` globs to what changed vs merge-base main
./build/kit-ci --no-strict        # an empty cmd is a no-op rather than a failure
```

Exit codes: `0` every stage passed, `1` at least one stage failed, `2` nothing ran — the config
is unreadable or invalid, or the tier resolved to no stages at all.

**Every stage runs.** A failing stage does not stop the run; one run names every culprit:

```
GATE FAILED — 1 passed, 2 failed (lint, tests), 0 skipped
```

A failed stage prints its command and the first `summary` lines of its combined output, so the
command can be re-run by hand. A stage whose `when` tool is absent, or whose `files` glob
matches nothing under `--changed`, is **skipped**: not failed, not counted. `--changed` needs a
merge base with `main`; when git cannot give it one, the run says so and runs every stage
unscoped rather than skipping silently (QUESTIONS.md Q13). The rest of the readings the runner
took — the default tier, strictness, glob semantics, what happens to an all-skipped run — are
Q9–Q16 in `QUESTIONS.md`.

## The gate

Everything goes through `tools/ci.sh` — no hosted CI, no `.github/workflows/`. It is what
the kit's git hooks in `.githooks/` run, and what a nightly clean checkout runs:

```bash
git config core.hooksPath .githooks     # one-time, per clone
./tools/ci.sh                           # every stage: tree format build lint tests fuzz
./tools/ci.sh fast                      # format build tests   (the pre-commit tier)
./tools/ci.sh --require-clean full      # the pre-push tier
./tools/ci.sh --list                    # what the stages are
```

| Stage | What it checks |
|---|---|
| `tree` | every file committed or ignored; the gate's own footprint is in `.gitignore`; `--require-clean` fails on uncommitted edits |
| `format` | `clang-format --dry-run -Werror` on the files this branch touches — working tree *and* staged copy |
| `build` | cmake configure + build with **gcc and clang**, zero warnings (warnings are errors) |
| `lint` | `clang-tidy` over every source in the compile database; findings are fixed in the code, never by widening `.clang-tidy` |
| `tests` | `ctest`, and the count is printed |
| `fuzz` | the parser as a libFuzzer target (`-fsanitize=fuzzer,address,undefined`) for 60 s, starting from the checked-in `fuzz/corpus/` |

A failing stage prints everything that failed inside it and then stops the run, and the
summary names every requested stage that therefore did not run. A run where a stage did not
execute never prints `GATE PASSED`.

## The config file

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
fail_on = "nonzero"           # nonzero | output:<regex> | exit:<n>   (default nonzero)
timeout = 120                 # optional seconds; default 300; 0 = no timeout
files = "*.py"                # optional glob; with --changed, skip when nothing matches
when = "tool:ruff"            # optional; skip when the tool is not on PATH
summary = "head:40"           # optional; how much failed output to show
```

The vocabulary is deliberately frozen: if the bash gates do not express it, KitCI has no
keyword for it. The escape hatch for anything exotic is `cmd` — a stage whose `cmd` is any
shell string.

### The error contract

Any byte string is either a valid config or an error carrying a 1-based line number — no
crash, no hang, no error without a line. When one input has several faults the one on the
**earliest line** is reported, so the same bytes always produce the same answer. Messages
are short, lowercase and name the fault:

```
kit-ci: unknown key 'colur' in [stage.lint] (line 4)
```

`fuzz/corpus/` holds the seeds the 60-second campaign starts from: a minimal config, a
config using every documented key, and five deliberate rejections (unknown key, unterminated
string, unterminated array, a duplicate table, bad values).

## Layout

```
include/kitci/parser.hpp   the parsed-config model and the parser's API
include/kitci/runner.hpp   the runner's API: RunOptions, StageResult, RunResult
src/parser.cpp             the TOML-subset parser
src/runner.cpp             stage execution: fail_on, timeouts, skips, the verdict
src/main.cpp               the command line
tests/parser_test.cpp      the frozen parsing tests (SPEC.md §8)
tests/runner_test.cpp      the frozen runner tests (SPEC.md §8)
tests/cli_test.cpp         the frozen CLI tests, run against the real binary
tests/fixtures/            configs the CLI test feeds to the binary
fuzz/fuzz_gate_toml.cpp    the libFuzzer entry point
fuzz/corpus/               the checked-in starting corpus
tools/ci.sh                the gate
.githooks/                 pre-commit (fast tier), pre-push (full tier)
```

## License

Unlicense — see `LICENSE`.
