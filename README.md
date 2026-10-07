# KitCI

A CI gate engine in one self-contained C++17 binary. It reads a per-repo `gate.toml`, runs
the stages that repo declares, accumulates every failure instead of stopping at the first,
and prints one unmistakable verdict line. The same binary serves C++, Python and Deno repos —
the only thing that varies is each repo's `gate.toml`.

`SPEC.md` is the frozen contract. This README says how to build and run what exists today.
**Starting from zero, in a repo that has no gate yet? `docs/GETTING-STARTED.md` is the
five-minute version** — install the engine once, arm the hooks once, then just work.

## Why not `pre-commit`?

`pre-commit` is the incumbent for "a config-driven local gate with no hosted CI" (and `prek`
and `husky` cover the same ground in Rust and JavaScript). KitCI is not a claim to be unique;
these are the differences, and they are the reason this exists.

**1. One static binary, no runtime.** `pre-commit` is a Python framework that manages per-hook
tool installation: a hook is named as a `repo` + `rev`, cloned and installed into
`~/.cache/pre-commit` on first run, with the network needed to get it (its own docs' quick
start shows it downloading and building, including a copy of node, when the machine lacks
one). `kit-ci` is one binary: a stage is a shell string, run with `/bin/sh -c`. Build it once
(the kit's template installs it next to `tools/`), and the gate then runs on a bare clone with
a compiler and nothing else — no package manager, no per-hook environment, no first-run
download.

**2. The policy file is the whole policy, and an agent can edit it.** Fifteen lines of
`gate.toml` name the stages, their tiers and their failure rules; there is no hook
repository, no revision to pin, no manifest of entry points, and no plugin registry. That is
the point for the repos this was built for: an AI agent edits a small config file reliably,
and reads or refactors an 800-line bash gate unreliably (the FSMgine incidents, 2026-10-06).
A gate change is a visible commit to one text file.

**3. Tiers are named sets declared per repo, chosen by whoever invokes the gate.**
`[tier.fast] stages = ["format", "lint"]`, then `kit-ci --tier fast` from a git hook,
`kit-ci` by hand, `--tier full` on a nightly clean checkout. `"*"` means every declared stage,
and a stage can join a tier from its own table with `tier = "full"`. `pre-commit`'s `stages`
attaches a hook to one of git's own hook stages instead — useful, but it is a fixed enum, not
a repo-defined set.

**4. The verdict is one line, and it names every culprit.** Both tools run all their checks
and report every failure (`pre-commit`'s `fail_fast` is `false` by default). What differs is
the answer at the end: `pre-commit` prints a column of per-hook results, and exit `1` means
"something failed"; `kit-ci` prints

```
GATE FAILED — 1 passed, 2 failed (lint, tests), 0 skipped
```

and has a third exit code: `2` means **nothing ran** — the config was unreadable or invalid,
or the tier resolved to no stages. A gate that checks nothing cannot read as green, and there
is deliberately no option to stop at the first failure.

**5. The gate explains itself.** `--graph` prints the policy as a Mermaid diagram,
`--graph --format html` as a standalone offline page, `--graph --format dot` as GraphViz text.
Nothing in `pre-commit`, `prek` or `husky` does this: the config file is also the
documentation, generated from the same parse the run uses, so it cannot go stale.

### What we borrowed, by name

Credit where it is due, and no pretending otherwise:

- **`--changed`** is `pre-commit`'s idea that a hook should only be asked about the files that
  changed. `pre-commit` scopes to the staged set during a commit; `kit-ci --changed` scopes a
  stage's `files` glob to the branch diff against `main`.
- **`when = "tool:<name>"`** is `pre-commit`'s language/adapters idea in one line: skip the
  stage cleanly when the tool is not on this machine, rather than fail a commit on a laptop
  that never installed it. (Its weakness is that the tool has to be installed by the hook
  manager; ours has to be installed by you.)
- **Hook-manager ergonomics** — name the tier in the hook, let the gate own the stage list —
  so a stage cannot be added to the gate and forgotten by a hook.

## Status

**Stages A–D — the parser, the runner, the graphs, and the first converted repo.** `kit-ci`
parses `gate.toml` (every documented key, every rejection rule, a 1-based line number on every
error), answers `--list`, runs a tier (every stage in run order, `fail_on`, `timeout`, the `when`
and `--changed`/`files` skips, one verdict line, exit codes 0/1/2), and renders the config in
three text formats. Stage D converted the `docsum` repo: its gate is now `gate.toml` (six stages,
two tiers) plus a 36-line wrapper and four small stage scripts — 239 lines of repo-owned shell
where the kit's 552-line bash copy used to be, with the checks unchanged. The worked example,
including what the conversion could not express and what it costs, is the next section but one:
"Converting a repo".

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure    # the whole suite
./build/kit-ci --help

# the one-time machine install: Release, in its own build dir — this is the engine
# every gate on the machine runs, and a Debug build is 8.6x its size for nothing
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
cmake --install build-release --prefix ~/.local   # -> ~/.local/bin/kit-ci
```

`cmake -DKITCI_BUILD_FUZZ=ON` (clang) builds the libFuzzer target instead of the object
library; `-DKITCI_BUILD_TESTS=OFF` skips the suite.

## Asking the gate what it is

```bash
./build/kit-ci --list                    # stages, tiers, and which stages are in which tier
./build/kit-ci --graph                   # the flow as Mermaid text (GitHub renders it)
./build/kit-ci --graph --format dot      # GraphViz text
./build/kit-ci --graph --format html     # standalone page, inline SVG, no JavaScript
./build/kit-ci --graph-html              # shorthand for --graph --format html
```

All three formats are **emitted, never rendered**: no image library, no subprocess calling a
renderer, no network. `dot` does not have to be installed for `--format dot` to work (it is
not installed on the machine that wrote this) — the text goes to whatever viewer you point at
it. Every format is a pure function of the parsed config and therefore byte-stable: same
config in, same bytes out, so the output can be committed, diffed and golden-tested.

The graph mode is a *reading* of the config, so it takes no run options: `--graph --tier fast`
prints the whole config and says on stderr that it ignored `--tier`. Two output modes at once
(`--list --graph`) is exit `2`, and so is an unknown format name:

```
$ ./build/kit-ci --graph --format svg
kit-ci: unknown --format 'svg' (want mermaid, html or dot)
$ echo $?
2
```

### A real example

For a Python repo, `gate.toml`:

```toml
[gate]
repo = "docsum"

[tier.fast]
stages = ["format", "lint"]

[tier.full]
stages = ["*"]

[stage.format]
cmd = "ruff format --check ."

[stage.lint]
cmd = "ruff check ."

[stage.tests]
cmd = "pytest -q"
fail_on = "output:^no tests ran"

[stage.fuzz]
cmd = "./tools/fuzz.sh"
tier = "full"
timeout = 120
```

`kit-ci --graph` prints (verbatim output of the binary above):

```mermaid
flowchart LR
    %% kit-ci gate flow — from the parsed gate.toml; same config, same bytes
    %% one node per stage, one edge per step in a tier's run order
    %% repo: docsum
    s0["format"]
    s1["lint"]
    s2["tests"]
    s3["fuzz"]
    s0 --> s1
    s1 --> s2
    s2 --> s3
    classDef tier0 fill:#0b253a,stroke:#38bdf8,color:#e2e8f0
    classDef tier1 fill:#0f2a1e,stroke:#34d399,color:#e2e8f0
    class s0,s1 tier0
    class s0,s1,s2,s3 tier1
    %% class ids: tier0=fast tier1=full
```

`--graph-html` prints the same flow as a page you can open from `file://` with no network:
dark background, one box per stage (the box carries the tiers it belongs to), arrows in run
order, a legend. It contains no absolute URL at all — not even an SVG namespace, which an
inline `<svg>` in an HTML5 document does not need — and no JavaScript.

Read the Mermaid the way it is written: the `%% class ids` comment names which `classDef` is
which tier, because that is what Mermaid has instead of a legend, and a stage in two tiers
carries two classes (a renderer that keeps only the last `classDef` will colour it by the last
tier). In `dot`, a stage several tiers run is declared in the first cluster that runs it and
mentioned by name inside the others, so the node's `comment` attribute names every tier it
belongs to.

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
Q9–Q16 in `QUESTIONS.md`; the graph readings, and the one about what this README may claim
about `pre-commit`, are Q17–Q23.

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
| `fuzz` | the parser and the graph formats as a libFuzzer target (`-fsanitize=fuzzer,address,undefined`) for 60 s, starting from the checked-in `fuzz/corpus/` |

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

`fuzz/corpus/` holds the seeds the 60-second campaign starts from: a minimal config, a config using
every documented key, and five deliberate rejections (unknown key, unterminated string,
unterminated array, a duplicate table, bad values) — plus, since stage D, the input that made the
HTML page carry an absolute URL in its repo name (`repo-name-with-a-url.toml`): it is a regression
seed, so undoing that fix aborts the campaign when it loads the corpus rather than waiting for a
mutation to find the input again (QUESTIONS.md Q29). The campaign does not only parse:
every accepted input is also rendered in all three graph formats, and a format that crashed,
returned nothing, or emitted an absolute URL is a finding (SPEC.md §5).

## Converting a repo (the worked example: docsum)

Stage D (SPEC.md §9) converted one fleet member. `docsum`'s gate was a 552-line copy of the kit's
python template; it is now a policy file, a wrapper and four small scripts. Everything in this
section is measured on the converted repo rather than sketched.

`docsum/gate.toml` — the whole policy:

```toml
[gate]
repo = "docsum"

[tier.fast]
stages = ["lint", "format", "tests", "types", "identity"]

[tier.full]
stages = ["*"]

[stage.lint]
cmd = "scripts/py-tool.sh ruff check ."

[stage.format]
cmd = "scripts/format-changed.sh"           # the branch's touched .py files, not the whole tree

[stage.tests]
cmd = "scripts/py-tool.sh pytest -q -p no:cacheprovider"
fail_on = "exit:5"                           # pytest collects no tests -> a failure

[stage.types]
cmd = "scripts/py-tool.sh mypy ."

[stage.cleanenv]
cmd = "scripts/clean-env.sh"                 # throwaway venv + requirements.txt + the suite there
timeout = 600

[stage.identity]
cmd = "scripts/identity.sh"                  # VERSION == docsum.__version__
```

`docsum/scripts/gate.sh` — the wrapper all three callers run: it `cd`s to the repo root, refuses
to run when `kit-ci` is not on PATH (a gate that cannot find its engine must not read as green),
and execs `kit-ci --tier "${GATE_TIER:-full}"`. The two hooks name their tier: `--tier fast` on
commit (5 stages — no throwaway venv is built at commit time), `--tier full` on push.

```
$ kit-ci --list                                  # from the converted repo
stages: lint format tests types cleanenv identity
tier fast: lint format tests types identity
tier full: lint format tests types cleanenv identity

$ scripts/gate.sh                                # the full tier, 10 s warm
GATE PASSED — 6 passed, 0 failed, 0 skipped

$ git clone . /tmp/x && (cd /tmp/x && scripts/gate.sh)     # the nightly pattern
GATE PASSED — 6 passed, 0 failed, 0 skipped
```

Four things a reader converting the next repo should take away, all of them measured:

1. **The engine is installed, not committed**: `cmake --install build-release --prefix ~/.local`
   (the Release recipe under "Build and test", and `docs/GETTING-STARTED.md` §1). A
   converted repo carries policy only; the price is that a machine without `kit-ci` cannot run the
   gate at all (the wrapper says so and exits 1 — the old bash copy needed only python3 and uv).
2. **Anything needing more than one command is a script, and the vocabulary stays frozen.** Three
   of docsum's six stages call a repo-owned script: a tool ladder (`.venv/bin/<tool>` → PATH →
   `uv run --with`), the touched-file list, and the identity pair. `cmd` is any shell string; that
   escape hatch was enough.
3. **`--changed` is not the touched-files logic.** `--changed` scopes a `files` glob and only ever
   *skips*; a stage that needs the file list as *arguments* keeps its own list. On a fresh clone —
   level with `origin/main`, i.e. exactly the nightly job — `--changed` resolves to an empty diff
   and would skip the very stages that job exists to run.
4. **Tiers are what the hooks name.** The old gate's `GATE_TIER=fast` line ("creating and
   populating a venv is push-time work") is now `[tier.fast]`, and each hook names the tier
   instead of repeating any stage list.

What the conversion did **not** carry over — the tools table, `STRICT_TOOLS=1`, the `kit probes`
step, the identity check's venv interpreter, and the `(last commit, …)` note on the verdict — is
recorded in `QUESTIONS.md` Q24–Q28 (with the reason and the cost of each) rather than left for a
reader to discover.

## Layout

```
include/kitci/parser.hpp   the parsed-config model and the parser's API
include/kitci/runner.hpp   the runner's API: RunOptions, StageResult, RunResult
include/kitci/graph.hpp    the graph API: GraphFormat and the three renderings
src/parser.cpp             the TOML-subset parser
src/runner.cpp             stage execution: fail_on, timeouts, skips, the verdict
src/graph.cpp              Mermaid, the inline-SVG HTML page, and GraphViz dot
src/main.cpp               the command line
tests/parser_test.cpp      the frozen parsing tests (SPEC.md §8)
tests/runner_test.cpp      the frozen runner tests (SPEC.md §8)
tests/graph_test.cpp       the frozen graph tests (SPEC.md §8 and the stage-C amendment)
tests/cli_test.cpp         the frozen CLI tests, run against the real binary
tests/fixtures/            configs the CLI test feeds to the binary
fuzz/fuzz_gate_toml.cpp    the libFuzzer entry point (parser + graph outputs)
fuzz/corpus/               the checked-in starting corpus
tools/ci.sh                the gate
.githooks/                 pre-commit (fast tier), pre-push (full tier)
```

## License

Unlicense — see `LICENSE`.
