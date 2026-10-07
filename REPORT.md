Gate verdict line (verbatim): PENDING — filled from the full gate run below
Test count: 16 (ctest: "100% tests passed, 0 tests failed out of 16")
Fuzz stage: runs / seconds / artifacts: PENDING
Stage reached: Stage A (the parser, the frozen parsing and CLI tests, tools/ci.sh, the fuzz
target and its corpus). Stages B, C and D are not started.
What I could not do: the runner, the graph modes and the fleet conversion are stages B, C and
D — `kit-ci` with no arguments, `--tier`, `--changed`, `--graph` and `--graph-html` each exit
2 with "not implemented yet" rather than pretending to run.
What I had to guess: the `tree` stage (SPEC.md §6 does not name it; the kit's pre-push hook
passes --require-clean, which only `tree` implements) and which fault is reported when one
config has several ("earliest line wins"). Questions Q1 and Q2 in QUESTIONS.md, with Q3-Q8
covering the smaller readings.
