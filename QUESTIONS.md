# Questions

SPEC.md §0: "If something is genuinely ambiguous, write the question into `QUESTIONS.md` and
take the simplest reading rather than guessing elaborately." These are the readings taken
while building KitCI, each with the question it answers: **Q1–Q8** in stage A (the parser and
the gate), **Q9–Q16** in stage B (the runner). None of them changes the frozen vocabulary.

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
