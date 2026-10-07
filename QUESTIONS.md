# Questions

SPEC.md §0: "If something is genuinely ambiguous, write the question into `QUESTIONS.md` and
take the simplest reading rather than guessing elaborately." These are the readings taken
while building KitCI, each with the question it answers: **Q1–Q8** in stage A (the parser and
the gate), **Q9–Q16** in stage B (the runner), **Q17–Q23** in stage C (the graphs and the
README positioning), **Q24–Q29** in stage D (the docsum conversion and the fleet notes),
**Q31–Q33** in v1.1 (the canonical AST, the graph annotations, and the grammar appendix), and
**Q34** in the docs card of 2026-10-07 (what `docs/FOR-AGENTS.md` may claim, and how).
**Q30 is not a reading** — it is a spec-owner ruling, and it is the one entry that changes the
frozen vocabulary.

## Q1 — Is the stage set exactly the five in §6, or the kit's gate plus those five?

§6 names `format -> build -> lint -> tests -> fuzz`, and the task says to PLUNK-IN the
AI-DEV-STARTER C++ template, whose gate carries eleven stages.

**Reading taken:** the five are all present, in that order, and `tree` is carried as a sixth.
The kit's `pre-push` hook runs `tools/ci.sh --require-clean full`, and `--require-clean` is
implemented *in* `tree` — dropping the stage would leave a flag the hook passes that means
nothing. The kit's `release`, `version`, `asan`, `tsan` and `pristine` stages are not
carried (see `INCIDENTS.md`), and `kitprobes` is not either — this copy carries no probe.
**Question:** should `./tools/ci.sh` with no arguments run five stages or six?

## Q2 — When one config has several faults, which one is reported?

§5 requires "an error with a 1-based line number" but does not say which fault wins when
there are several.

**Reading taken:** the fault on the **earliest line**, with the file scanned for every fault
it can find (syntax, unknown key, wrong type, out-of-range value, then the cross-reference
rules of §3). It is deterministic and it matches §8's `ReportsTheRightLineNumber`, whose
fault is on line 6 and whose error must say 6. **Question:** is "earliest line wins" the
intended contract, or should the cross-reference rules (a stage with no `cmd`, a tier that
names no stage, ...) be reported before the line-local ones?

## Q3 — Is `stages = []` legal?

§3 rule 9 rejects a `[tier.X]` table with **no** `stages` key; an empty array has the key.

**Reading taken:** `[]` parses and resolves to a tier that runs nothing — which then exits
`2` under §2's "a run with zero configured stages exits 2", not `0`. **Question:** should
`stages = []` be rejected at parse time instead?

## Q4 — What is a duplicate key?

The ten rules in §3 do not mention `cmd = "a"` twice in one `[stage.X]`.

**Reading taken:** rejected, because §3 also says the parser "rejects everything else". The
message is `duplicate key 'cmd' in [stage.lint]`. **Question:** is that intended, or should
the last assignment win (real TOML rejects it too, but the frozen list is silent)?

## Q5 — What should a mode that is not implemented yet exit with?

Stage A ships the parser and `--list`; `--tier`, `--changed`, `--graph`, `--graph-html` and
a plain run are stages B and C.

**Reading taken:** the flags are accepted (so the CLI surface of §2 is stable) and exit `2`
with the message `kit-ci: <mode> is not implemented yet`. Exit `0` would claim a run that
did not happen, and `1` means "a stage failed". **Question:** is `2` right for
"not implemented", or should it be a distinct code?

## Q6 — What does `--list` print?

§2 says it prints "stages, tiers, and which stages are in which tier"; §8's
`ListAnswersWithoutReadingSource` wants the answer "parsed and compared structurally".

**Reading taken:** two line shapes, one per element, in this order — `stages: <names in
declaration order>` then `tier <name>: <resolved run order>` for each tier in declaration
order, with `"*"` already expanded. That is what the test parses. **Question:** is a
human-oriented layout wanted instead (which would need a different test)?

## Q7 — Tests beyond §8

§8's list is a minimum: "every test in §8 that stage A covers exists, under that name".
Two more were added where the listed set left something load-bearing unpinned:
`ParserTest.TierKeyAddsStageToTier` (§3's `tier = "..."` adding a stage to a tier's list)
and `CliTest.ListOnInvalidConfigExitsTwo` (§2's exit code `2` for an invalid config).
**Question:** is a strict count of §8's names wanted, with nothing else?

## Q8 — `cmd = ""`

A stage whose `cmd` is present but empty is accepted: §3's rule 2 is about the key being
absent. Running an empty shell string is a no-op, so such a stage can never fail — but
"fails when it should say something" is the runner's business (stage B), not the parser's.
**Question:** should the parser reject an empty `cmd`? **Answered in stage B by Q10** — the
parser still accepts it, and `--strict` (the default) makes the runner fail the stage.

## Q9 — Which tier does a bare `kit-ci` run?

§2 says "run default tier" and never names one; a config may declare `fast`, `full`, or
anything else.

**Reading taken:** `full` when the config declares it, else the first tier the file declares;
a config that declares no tier at all exits `2`. The hooks name their tier (`--tier fast` for
pre-commit, `--tier full` for pre-push), so the bare run belongs to the human, and a human
running the gate by hand wants the strong one. **Question:** should the default tier instead
be the first declared tier in every case?

## Q10 — What does `--strict` / `--no-strict` mean? (This answers Q8.)

§2 makes `--strict` the default and points at §5, which defines only the parser's total
contract — nothing in the spec says what strictness *does*.

**Reading taken:** strict fails a stage whose `cmd` is empty, with `empty cmd (--strict: a
stage that checks nothing is a gate that lies)`; `--no-strict` runs it, and it passes as the
no-op it is. That is the one stage in the frozen vocabulary that could otherwise never fail.
**Question:** is that the intended meaning of the flag, or does strictness govern something
else entirely (warnings, skips, missing tools)?

## Q11 — Does `*` cross `/` in a `files` glob?

§4 says "`*`, `?`, and `**` across path separators, matching the fnmatch family". fnmatch
crosses `/` only when it is not asked for `FNM_PATHNAME`, and `**` means "across separators"
only in shells that define it that way.

**Reading taken:** `*`, `**` and `?` all cross `/` — fnmatch without `FNM_PATHNAME`, with
`**` spelled the same as `*`. §3's own example decides it: `files = "*.py"` on a Python repo
has to match `src/pkg/module.py`, which it would not if `*` stopped at a separator.
**Question:** should `*` be path-segment-local and only `**` cross separators (the gitignore
reading)?

## Q12 — Is `output:<regex>` combined with `nonzero`, and where does `^` match?

§4 states explicitly that `exit:<n>` is "combined implicitly with nonzero" and says nothing
for `output:<regex>`. And pytest's vacuous-green line is rarely the first line of the buffer,
which is exactly what makes `fail_on = "output:^no tests ran"` worth having.

**Reading taken:** a non-zero exit fails under **every** `fail_on` — a stage that crashed must
never read as green — and the regex is compiled with `std::regex::multiline`, so `^` matches
the start of any line and not only the start of the output. **Question:** should
`output:<regex>` *replace* the nonzero rule instead of adding to it?

## Q13 — What does `--changed` compare against, and what happens when git cannot answer?

§2 says "files changed vs merge-base with main"; nothing says what to do in a repo that has no
`main`, or outside a repository at all.

**Reading taken:** `git merge-base HEAD main`, falling back to `origin/main`; the changed set
is `git diff --name-only <base>`, so committed, staged and working-tree changes count together.
When neither reference resolves — not a repository, or no `main` — the run says why on stderr
and runs every stage **unscoped**. Silently skipping the globbed stages would be the one answer
that lies: a pass for work the gate never looked at. **Question:** should a missing `main` be a
hard `2` instead, so nobody mistakes "unscoped" for "scoped"?

## Q14 — Which faults are the runner's to reject?

The parser enforces §3's ten rules. Three values are in the frozen vocabulary but not in that
list: a `fail_on = "output:<regex>"` whose regex does not compile, a `when` that is not
`tool:<name>`, and a `summary` that is not `head:<n>`.

**Reading taken:** the runner rejects all three **before the first stage runs**, quoting the
stage's line number, so exit `2` keeps its meaning ("no stages ran"). A bad regex, an
unrecognised `when` or a misspelled `summary` is a config fault and not a stage result; a
`when` that was quietly ignored would be a skip that lies. **Question:** should §3's rules be
widened so the *parser* owns these (they would then also fail `--list`)?

## Q15 — Is a run where every stage was skipped a pass?

§2 says a run with zero **configured** stages exits `2`; §4 says a skipped stage is "skipped
(not failed, not counted)".

**Reading taken:** "configured" means the tier's resolved stage list, and that list is empty
only when the config declares no stages for the tier — exit `2`, never a pass. A stage that
was configured and then deliberately skipped (`when` tool missing, `--changed` glob unmatched)
is neither a failure nor a lie: a commit that touches only documentation must still be able to
pass the `fast` tier, which is the whole reason `files` exists. **Question:** should an
all-skipped run report a distinct code or word, so a reviewer can see at a glance that nothing
was checked?

## Q16 — Which directory does a stage run in?

§3 says the command is "a shell string, run via `/bin/sh -c`" and does not say from where.

**Reading taken:** kit-ci does not `chdir`. A stage runs in the directory kit-ci was invoked
from, which is also the repository root that `--gate gate.toml` defaults to and the repository
`--changed` asks git about. **Question:** should a stage run from the directory that holds the
`--gate` file instead?

## Q17 — What is the CLI shape of the three graph formats?

SPEC.md §7 names two graph modes; the stage-C amendment makes it `--graph --format
<mermaid|html|dot>` with `--graph-html` kept as the documented shorthand.

**Reading taken:** `--graph` accepts `--format <name>` with exactly `mermaid` (the default),
`html` or `dot`; `--graph-html` is `--graph --format html`; the flags are read left to right,
so a later `--format` overrides the shorthand. An unknown or missing value is exit `2` — the
same total-contract discipline as the parser — with the three valid names in the message, and
`--format` **without** `--graph` is also exit `2`: a flag that would do nothing is refused
rather than accepted quietly. **Question:** should `--format` without `--graph` be ignored
instead of refused, on the grounds that it is harmless?

## Q18 — What does `--graph` do with run options?

`--tier`, `--changed` and `--strict` describe a *run*, and the graph is a function of the whole
parsed config (SPEC.md §7), so none of them can affect the output.

**Reading taken:** the graph is printed and the run options are reported on stderr (`--graph
ignores run options (--tier, --changed, --strict)`), exit `0`. Saying so is the difference
between "ignored" and "silently ignored"; refusing them outright would make the common
`--tier fast --graph` muscle-memory a failure. **Question:** should a run option with `--graph`
be a hard `2` instead?

## Q19 — What happens with `--list --graph`?

Both are answers about the same config, and they are different answers.

**Reading taken:** exit `2` and a message saying the two ask for different outputs. Printing
one and ignoring the other would be exactly the silent-wrong-answer class this tool exists to
avoid. **Question:** should one of them win (and if so, which)?

## Q20 — How does the graph show a stage that belongs to several tiers?

A stage can be in a tier's `stages` list, in a second tier via `stages = ["*"]`, and in a third
by its own `tier = "..."` key. §7 says "one node per stage, colored by tier membership".

**Reading taken:** one node per stage, carrying **every** tier it belongs to. Concretely:
Mermaid emits one `classDef` per tier and a `class` line per tier, with a `%% class ids`
comment naming which is which (Mermaid has no legend); HTML fills the box from the first tier
that runs the stage and prints the full tier list inside the box (plus a real legend); dot
declares the node inside the first cluster that runs it, mentions it bare inside the others,
and puts the full tier list in the node's `comment` attribute. **The consequence, honestly:**
a Mermaid renderer that applies only the last `classDef` will colour a shared stage by the
last tier, and dot cluster membership follows first declaration — both are noted in the README.
**Question:** is a single fill colour with the membership as text wanted instead?

## Q21 — Why does the standalone HTML have no `xmlns` on its `<svg>`?

Every inline SVG on the web is written `xmlns="http://www.w3.org/2000/svg"`, and that string is
the one absolute URL §8's `GraphHtmlIsSelfContained` forbids (`http://` anywhere in the output).

**Reading taken:** the attribute is dropped. The file is an HTML5 document, and in HTML5 an
inline `<svg>` is already in the SVG namespace — the attribute is only needed by the XML
parser. A page whose contract is "opens from `file://` with zero network" should carry no
absolute URL at all. **Question:** is that reading right, or should the page be XHTML (and the
test amended) so the attribute can stay?

## Q22 — Does `--graph` on a config with no stages exit 2?

§2 says a **run** with zero configured stages exits `2`, never `0` ("a gate that checks nothing
is a gate that lies").

**Reading taken:** the `2` belongs to the run, not to the mode. `--graph` on such a config
prints the empty flow and exits `0` — it answered the question it was asked, and an empty
diagram is the truth about a config with no stages. The alternative (exit 2) would make the
honest rendering of a broken config indistinguishable from "could not read the config".
**Question:** should `--graph` refuse a config whose every tier resolves to no stages?

## Q23 — What may the README claim about pre-commit?

The stage-C amendment gave five differences to sell, the fourth being that pre-commit "stops at
the first failing hook", and I am told the README's first screen must answer "why not
pre-commit?".

**Reading taken:** checked against pre-commit's own documentation before writing a line of it.
`fail_fast` is a **top-level option whose default is `false`** ("set to `true` to have
pre-commit stop running hooks after the first failure"), and its hook-level sibling is also
optional — so pre-commit runs every hook and reports every failure, exactly as kit-ci does. The
README therefore claims what survives checking: one grep-able verdict line, the third exit code
(`2` means nothing ran), and the absence of any fail-fast option. Difference 3 is narrower than
the amendment too: pre-commit's `stages` attaches a hook to one of git's own hook stages — a
fixed enum — where a kit-ci tier is an arbitrary, repo-declared, named set chosen at the call
site. Difference 1 was confirmed as written (pre-commit is a Python framework that clones and
installs hook environments on first run; its own quick start shows the download, and building a
copy of node when the machine has none). Similarly, `when = "tool:X"` is credited as
pre-commit's language/adapters idea and `--changed` as its changed-file scoping.
**Question:** is the checked framing wanted, or should the positioning keep the sharper
phrasing even where pre-commit's documentation does not support it?

## Q24 — Where does the engine live for a converted repo?

§9 says "`gate.toml` + a 3-line hook wrapper" and never says where `kit-ci` comes from. Three
readings were possible: commit the binary into each repo (3 MB of platform-specific ELF in a
Python repo, and the policy file stops being the only visible diff), vendor the KitCI source and
build it in the wrapper (a compiler and a network on every machine that runs the gate), or
install it once per machine.

**Reading taken:** installed once per machine, from a new `install(TARGETS kit-ci RUNTIME
DESTINATION bin)` rule in KitCI's `CMakeLists.txt`:

    cmake -S . -B build && cmake --build build
    cmake --install build --prefix ~/.local        # -> ~/.local/bin/kit-ci

The converted repo's wrapper then execs `kit-ci`, and refuses to run at all when it is not on
PATH — measured on a fresh clone with `env PATH=/usr/bin:/bin`: `gate: kit-ci is not installed
...`, exit 1 (the old 552-line bash gate needed only python3 and uv). The engine stays out of the
repo because the whole claim of §1 is that the policy is the diff: a converted repo carries
`gate.toml` and a wrapper, and one KitCI upgrade on the machine moves every repo at once.
**Question:** is a machine-level install acceptable for the fleet (it is what makes the nightly
clone work with no checkout of KitCI), or should `scripts/gate.sh` fall back to building a
pinned KitCI commit when `kit-ci` is absent — one compiler away, but self-contained?

## Q25 — §9 calls docsum "the smallest `scripts/gate.sh` fleet member". It is the largest.

Measured on 2026-10-06: `docsum/scripts/gate.sh` 552 lines, `TNGPlaylists/scripts/gate.sh` 282,
`Notes/scripts/gate.sh` 163. The conversion was done on docsum anyway, because §9 and the card
both name it — but the parenthetical reason does not survive the measurement, and it matters
because the size of a repo's gate is exactly what decides how much of it a frozen vocabulary can
hold: 552 lines of it are the kit's python gate's packaged/unpackaged split, its tool ladder and
its clean-venv story, none of which the config language has a keyword for.
**Question:** if "smallest" was the real intent, should the next fleet member converted be
`Notes` (163 lines, and already the repo with a nightly clean-checkout watcher), so the worked
example shows a conversion that is nearly all `gate.toml`?

## Q26 — Is `--changed` a replacement for the kit's touched-files logic?

The kit's python gate kept a file list of its own — the branch diff against `origin/main`, plus
the working tree, plus untracked files, falling back to the last commit's files when the
checkout is level with `origin/main`, and to the index when there is no commit yet — and used it
to *build the argument list* of `ruff format --check`. `kit-ci --changed` is a different tool: it
scopes a stage's `files` glob and only ever **skips** a stage.

**Reading taken:** they are not interchangeable, and the difference is load-bearing on the
nightly clone. A fresh clone sits level with `origin/main`, so `--changed` resolves to an empty
diff and every globbed stage would be skipped — the formatter would be skipped precisely where
the nightly job exists to run it. So the list stayed in the repo: `[stage.format] cmd =
"scripts/format-changed.sh"` (`files = "*.py"` was deliberately NOT added, because under
`--changed` it would turn "check the touched files" into "check nothing"). The measured reason
the logic must stay at all: `ruff format --check .` fails on this tree — 10 of 25 `.py` files
would be reformatted — so a whole-tree stage would make every commit red for debt nobody in that
commit wrote.
**Question:** should kit-ci pass the changed set to a stage (e.g. `$KIT_CI_FILES`, one word in the
shell string), or is "anything that needs a file list is a script, and the vocabulary stays
frozen" the intended answer?

## Q27 — Who guarantees a stage runs in the repo root, and who chooses the tier?

Q16 recorded that kit-ci does not `chdir`: a stage runs where kit-ci was invoked. A `gate.toml`
means nothing if `cmd = "scripts/py-tool.sh ruff check ."` resolves from the wrong directory.

**Reading taken:** the wrapper owns it. `scripts/gate.sh` cd's to its own parent's parent (the
repo root) before exec'ing `kit-ci`, and it is the documented entry point for all three callers.
Tier selection: the two hooks name their tier explicitly (`--tier fast` on commit, `--tier full`
on push) so the hook cannot drift from the stage list, and the wrapper still honours `GATE_TIER`
(the variable the old gate took) with a default of `full`, so a nightly job that already sets it
keeps working. `scripts/gate.sh` with no arguments runs the full tier.
**Question:** should kit-ci itself `chdir` to the directory holding the `--gate` file (Q16 left
this open — this is the first repo where the answer has consequences), and should `GATE_TIER`
survive at all now that a tier is named at the call site?

## Q28 — What the conversion is NOT a line-for-line replacement

The conversion kept every check, but five things the 552-line script did are not reproduced, and
each is recorded here rather than quietly dropped. (1) The `tools` stage — the table naming, for
each tool, which rung of the ladder answered and where from — is gone; kit-ci prints the stages
and its own skips, so a run no longer says that ruff came from `uv run --with` because this repo
pins no tools. (2) `STRICT_TOOLS=1` (a missing tool FAILS instead of skipping) has no equivalent:
`py-tool.sh`'s last rung always fails loudly, so a missing tool is now always a failure rather
than a configurable skip — the safer default, but not the same switch. (3) The gate's `kit probes`
step is gone, and cannot come back: it was a stage INSIDE the kit's python gate, and a repo whose
policy is a `gate.toml` has no kit revision to hold itself to (docsum never carried a probe; the
record in `.ai-dev-starter.json` now says so). (4) `identity` no longer asks the throwaway venv's
interpreter in the full tier — it always asks `python3` with `PYTHONPATH=.`, i.e. the tree; the
old full-tier version asked the same tree through a venv, and both compare the same pair. (5) The
old verdict could carry `(last commit, since this checkout is level with origin/main)`; kit-ci's
verdict line is its own one-liner, so that note now appears in the `format` stage's output
instead. Also worth recording: the two `scripts/py-tool.sh`/`format-changed.sh` scripts still pass
`NO_COLOR`, `UV_NO_PROGRESS` and `PYTHONDONTWRITEBYTECODE` themselves, because the engine does not
set a stage's environment.
**Question:** is that ledger the right level of fidelity for v1, or is any one of the five worth
carrying back into the engine as a keyword (the tools table and a `strict` switch are the two that
look like they could be)?

## Q29 — What does "the HTML carries no absolute URL" mean when the config names one?

The fuzz stage found this on 2026-10-06, during stage D (card t_5ee2ee12), and it is why the push
that published this stage's report was first REFUSED:
`.ci-logs/fuzz-artifacts/crash-300de12c3ac55acba12edd364e9b90eacaaa41cf`, a valid config whose
`repo = "kit-chttp://i"`. The page echoes the repo name in its summary line, so the bytes of the
standalone HTML contained `http://` — and the oracle aborts on exactly that (`fuzz_gate_toml.cpp:42`,
mirroring the frozen `GraphHtmlIsSelfContained` in `tests/graph_test.cpp`).

Two readings. (a) The oracle is too strict: a URL in *inert text* cannot be fetched, so the check
should look for fetching positions (`src=`, `href=`, `url(`, `@import`, `xmlns`) instead. (b) The
engine is wrong: §7 and §8 state the property at the byte level ("no `http://` or `https://`
reference in the HTML output"), and `repo` is a free display string — §3 restricts the vocabulary's
*names* to `[A-Za-z0-9_-]`, not the repo name — so a legal config was violating a stated contract.

**Reading taken: (b), and the page now guarantees the property by construction.** Display text goes
through `HtmlDisplay()`, which writes the colon of a `://` separator as `&#58;`: a browser renders
the name unchanged (`kit-chttp://i`), the page's bytes carry no absolute URL whatever the config
says, and the cheap grep stays a valid test of the property the SPEC states. Test first —
`GraphHtmlHidesAUrlInTheRepoName` fails on the old code and passes on the new — and the input is now
a checked-in corpus seed, so a reintroduction aborts when the campaign loads it instead of waiting
for a mutation to find it again. Reading (a) is not crazy (it is what "no external requests" means
in general), but choosing it would mean rewriting a frozen test's meaning and the oracle together —
a spec-owner's call, not a worker's. Note what (b) deliberately does NOT cover: Mermaid and dot
still echo the repo name verbatim in their comments (they have no entity syntax), and no frozen test
asks them not to.
**Question:** keep the byte-level invariant for all three formats (dot's `// repo:` and Mermaid's
`%% repo:` would then need their own escape), or move the check to fetching positions in the HTML
and let the raw text through everywhere?

## Q30 — Is an empty `cmd` a config fault? (the one SPEC-OWNER RULING)

Q8 and Q10 left this to the run: the parser accepted `cmd = ""` and `--strict` made the RUNNER
fail that stage, on the grounds that a stage which can never fail is the runner's business and
not the parser's.

The spec owner ruled otherwise on 2026-10-06 (card `t_6989cec2`, the fleet conversion's
pre-step), and the reason is `--list` and `--graph`: they read the same parse a run uses, so a
config that cannot honestly run a stage should not parse at all. **Ruling, implemented:** an
empty — or whitespace-only — `cmd` is a config fault reported on the `cmd` key's own line,
unless `[gate]` sets `strict = false`, which is the config saying the no-op is deliberate. That
key (`[gate] strict`, default `true`) and the bare boolean it takes are the only vocabulary
additions v1 has ever accepted, and they were accepted by ruling rather than chosen by a worker.

Test first, watched red for the right reason, both new tests in `tests/parser_test.cpp`:
`ParserTest.RejectsEmptyCmd` failed because the old parser ACCEPTED the config, and
`ParserTest.StrictFalsePermitsEmptyCmd` failed with `unknown key 'strict' in [gate] (line 2)`.
Both green after. The RUN-time rule is untouched: `RunnerTest.StrictFailsAStageWithAnEmptyCmd`
still fails the stage under the default `RunOptions.strict` — its fixture now declares
`strict = false` so that a Config holding an empty `cmd` can exist to be run at all, and its
assertions are unchanged.

**The layering, stated so nobody reads either half as dead weight:** the config key governs the
PARSE; the flag governs the RUN. A repo that wants a deliberate no-op stage sets
`[gate] strict = false` and still needs `--no-strict` to run it, because `--strict` is the
default. Two controls, two questions — is this config honest, and what did this run declare —
but if that reads as two switches for one decision, collapsing them (the key becoming the run's
default strictness) is a spec owner's call, not a worker's.
**Question:** is that layering what was ruled, or should `[gate] strict = false` also become the
run's default strictness, so the key alone is enough?

## Q31 — What does a backslash inside a string actually escape?

Writing the EBNF appendix (SPEC.md, v1.1) meant stating the `string` production, and this is the
one place the parser does not follow TOML. §3 calls `gate.toml` "a deliberately tiny TOML
subset", and TOML's basic string defines an escape SET (`\b`, `\t`, `\n`, `\f`, `\r`, `\"`,
`\\`, `\uXXXX`, `\UXXXXXXXX`) with anything else a fault. `ParseString` has no set: a `\` takes
the NEXT BYTE LITERALLY, whatever it is. Measured on 2026-10-06 with the built binary:

    cmd = "echo \"a\nb\" and \\ done"    ->    cmd: echo "anb" and \ done

(`\n` is the letter `n`; there is no way to write a newline inside a string, because a line is
one line.) A `\` immediately before the closing quote escapes it, so the string has no
terminator and the fault is `unterminated string` — which falls out of the same production.

**Reading taken:** the literal rule, written down rather than changed. It is what the frozen
vocabulary already means, every converted repo's `cmd` is a shell string that `/bin/sh`
interprets again, and no test changed — the appendix is the first document to SAY what the
parser has always done (Appendix A, note 3).
**Question:** is "an escape quotes the next byte" the intent (the `sh` reading), or should the
subset adopt TOML's escape set and reject an unknown escape — which would turn a `cmd`
containing `\n` from legal into a config fault?

**The related tie-break, recorded in the same place:** a fault that DISCARDS an entry makes the
table look like it is missing that key, and the key's absence is reported on the table's own
(earlier) line — so for `[stage.x]` on line 1 with `cmd = "a" "b"` on line 2, the trailing-
character fault is real but Q2's earliest-line rule reports `stage 'x' has no cmd (line 1)`,
i.e. the less specific of the two. **Question:** should a fault that discards an entry suppress
the derived "no cmd" fault on an earlier line, so the message names the typo?

## Q32 — What shape does `--ast` print?

The card that asked for `--ast` fixed the semantics (canonical JSON, byte-stable, one parse,
defaults materialised, declaration order, semantic-only — no line numbers) and left three
details to the implementation.

**Reading taken:**
1. **An absent optional string is `null`, not `""`.** The parser stores `""` for "not written"
   and the runner reads that same `""` as "no glob" and "no tool gate", so `null` is the
   runner's own view of the key — and it keeps "not written" distinct from `files = ""`, which
   the config language permits and which `""` in the JSON would report identically.
2. **The layout is fixed and pretty, not compact**: 2-space indent, one array element per line,
   keys in the documented order (`gate`, `tiers`, `stages`; and inside a stage, the grammar's
   own order). No list is ever taken from a `std::map` iteration — every order is one the config
   itself established — because "canonical" has to survive a recompilation, not just a rerun.
3. **`tier` and `tiers[].stages` are different questions.** The stage's `tier` key is what the
   config declared; a tier's `stages` is the run order that tier resolves to. A stage can be in
   a tier with no `tier =` line at all, and can carry `tier = "x"` while `x` also lists it, so
   dropping either one would make some membership unreadable from the JSON.

**Question:** is `null` for "not written" right, or should the three optional strings be omitted
from the object entirely — or emitted as `""`, which is what the struct holds?

## Q33 — Which stage settings a graph node annotates

The card fixed the principle — "defaults are what NO annotation means", annotate only what a
stage deviates on — and named four settings (`fail_on`, `timeout`, `when`, `files`).

**Reading taken:** five, not four. `summary` has a default (`head:40`) exactly as `fail_on` and
`timeout` do, so `summary = "head:10"` is a deviation a reader of the graph is not being told
about if it is left out — the one silent deviation, which is the thing the principle exists to
prevent. `cmd` is never annotatable (every stage has one) and `tier` is not either (membership
is already the colour, the cluster, the label and the comment). Placement: Mermaid's second
label line, the HTML detail panel (an SVG box has no room for a second line, and the page is
standalone already), dot's existing `comment` attribute. A stage carrying all five defaults is
annotated nowhere in any format, and all three formats are golden-tested byte-stable with
annotations present.
**Question:** is `summary` right to be annotated (it changes no stage's outcome, only how much
failure output is shown), or should the annotation list stay at the four the card named?

## Q34 — What may `docs/FOR-AGENTS.md` claim about an agent reading this repo?

The card asked for a page recording agent-discoverability as a design property, with the live
experiment as its evidence: a subagent with zero context, denied every `*.md` file and the web,
added a `filesize` stage to a converted repo's gate in 64.6 s and twelve tool calls, and the
result was verified independently (fast tier 4/4 green; a staged 3 MB file failed with the bytes
named; `--list` showed the stage in both tiers). Two things the card left open: how strong the
claim may be, and whether one verified experiment is evidence or an anecdote.

**Reading taken:** the claim is written as **agent-sufficient on the demonstrated surface** — add
a stage to an existing, commented gate, cold, without reading prose — and the page states its own
limits rather than leaving them for a hostile reader: one task, one model, one repo family; the
untouched edges (`when`, `files` with `--changed`, `strict`, the error contract, a repo with no
gate at all); the two hints it was given (`gate.toml`, `kit-ci`); and the inverse test nobody has
run, a config whose comments are absent. Its three "why it works" properties are described as
things a *repo author* controls in `gate.toml` — the comments as the manual, the binary's
`--help`/`--list`/`--ast` as answers that cannot go stale, the small vocabulary — not as engine
features, because only the author of a gate can supply the first and the third. The doc's
commands were run by hand in a scratch clone of Permuto before they were written down, which is
how this repo's other docs were written too.
**Question:** is one verified experiment enough for a page in this repo to use the word
"measured", or should it read as a single case study until a second task — a different edge, or a
config with no comments — has been run? The card asked for the evidence to be cited without
softening; the limits paragraph is what "without softening" turned out to mean.
