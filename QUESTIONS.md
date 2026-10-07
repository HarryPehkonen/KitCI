# Questions

SPEC.md §0: "If something is genuinely ambiguous, write the question into `QUESTIONS.md` and
take the simplest reading rather than guessing elaborately." These are the readings taken in
stage A, each with the question it answers. None of them changes the frozen vocabulary.

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
**Question:** should the parser reject an empty `cmd`?
