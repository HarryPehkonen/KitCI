Gate verdict line (verbatim):
    all 6 stage(s) passed in 169s
    GATE PASSED
Test count: 36 — the runner's own line was "100% tests passed, 0 tests failed out of 36"
(16 carried from stage A, 20 added by stage B: the 14 runner tests of SPEC.md §8 plus the
readings, and 6 CLI tests for the process-level contracts — exit codes, --tier, --changed).
Fuzz stage: runs / seconds / artifacts: 1 invocation / 60 seconds / 0 artifacts (470832
inputs executed). The target is still the gate.toml parser; stage B added no fuzz target
(SPEC.md §6 names one, for the parser).
Stage reached: Stage B — the runner. Stage execution in tier order, every failure
accumulated, fail_on ("nonzero", "output:<regex>", "exit:<n>"), timeouts, the `when` and
`files` skips, `--changed` against merge-base main, the verdict line and the exit codes.
Stage A's numbers are in the previous report, at commit 6dcc88f. Stages C and D are not
started.
What I could not do: nothing in stage B — every stage-B item of SPEC.md §4/§8/§9 is
implemented and green. `--graph` and `--graph-html` still exit 2 with "not implemented yet
(stage C)", as stage A left them; the fleet conversion (stage D) is untouched.
What I had to guess: eight readings, Q9–Q16 in QUESTIONS.md, each with its question:
Q9 the tier a bare `kit-ci` runs ("full" if declared, else the first declared); Q10 what
`--strict` means (an empty `cmd` fails — this is the answer to stage A's Q8); Q11 that `*`
crosses `/` in a `files` glob; Q12 that `output:<regex>` is combined with nonzero and that
`^` matches any line start; Q13 that `--changed` falls back to `origin/main` and runs
unscoped (never silently skipping) when git cannot answer; Q14 that the runner rejects a
regex that will not compile, an unknown `when` and a bad `summary` before the first stage
runs; Q15 that an all-skipped run is a pass while a tier with no stages is exit 2; Q16 that
a stage runs in kit-ci's own working directory.
