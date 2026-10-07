# Incidents

Symptom, the check that now catches it, and why the check must stay. The gate is the judge;
this file is what stops a check being deleted by someone who sees no reason for it.

## KitCI is a fork of the AI-DEV-STARTER C++ gate, and this is what changed

`tools/ci.sh` was copied from `AI-DEV-STARTER/templates/cpp/ci.sh` (with the kit's
`.githooks/pre-commit` and `.githooks/pre-push`, unchanged) and adapted to the stage set
`SPEC.md` §6 names. The differences, and why:

- **Added `lint` and `fuzz`.** §6 asks for `format -> build -> lint -> tests -> fuzz`. The
  kit's `tidy` stage is this repo's `lint` (clang-tidy); the fuzz stage is new — the kit
  ships no fuzzer, and SPEC.md §6 requires a 60-second libFuzzer campaign on the parser.
- **`build` runs two compilers.** The kit's build stage builds one configuration; §6 asks for
  "both gcc and clang, no warnings", so the stage configures and builds both and counts
  `warning:` in each log.
- **The kit's `release`, `version`, `asan`, `tsan`, `tidy` (as `tidy`) and `pristine` stages
  are not carried.** They are not in §6, and "do not invent scope" is the rule: `kit-ci` has
  no `--version`, and the fuzz stage already runs ASan+UBSan. Porting them back is a decision
  for whoever needs them, not a silent default.
- **`tree` IS carried, although §6 does not name it.** It is the kit's
  clean-worktree/footprint guard, and without it the `--require-clean` the kit's pre-push
  hook passes would do nothing. See `QUESTIONS.md` Q1.
- **The tidy baseline is gone.** The kit's tidy stage can tolerate inherited findings via
  `CI_TIDY_BASELINE`; this repo has no inherited findings, so `lint` requires zero and
  `--write-tidy-baseline` does not exist.

## The lint stage analyses a compile database gcc wrote, and clang-tidy is clang

**Symptom.** `lint` failed with `error: unknown warning option '-Wduplicated-cond'` on every
source, after the build had passed. The compile database carries the flag because the gcc
build used it; clang-tidy parses those same commands with clang, does not know the flag, and
`-Werror` (also in the command) turns the unknown-warning diagnostic into an error.

**Check.** No `-Wduplicated-cond` / `-Wlogical-op`: the flag set is now one both compilers
understand, so a compile database is portable between them. The alternative — teaching each
side to tolerate the other's flags — hides the next such mismatch instead of removing it.

**Why it must stay.** This is the failure mode of "the lint stage reads a database written
by a different compiler": it looks like a lint finding and is actually a configuration
mismatch, and the tempting fix is to silence it.

## A stage that did not run must never read as a pass

The dispatch loop is the kit's, including the backstop that compares the stages that RAN
against the stages REQUESTED and fails the run when they differ. It was kept rather than
simplified: with `set -u`, one unbound variable aborts a stage and the loop together, and
without that comparison the run prints `GATE PASSED` after executing one stage of six.

## `fuzz` runs on a COPY of the corpus

libFuzzer writes the new units it discovers into its first corpus directory. Pointing it at
`fuzz/corpus/` would leave untracked files in the tree, and the `tree` stage would then fail
on the gate's own doing — a stage failing because an earlier stage worked.
