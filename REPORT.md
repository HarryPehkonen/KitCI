Gate verdict line (verbatim):
    all 6 stage(s) passed in 129s
    GATE PASSED
Test count: 16 — the runner's own line was "100% tests passed, 0 tests failed out of 16"
Fuzz stage: runs / seconds / artifacts: 1 invocation / 60 seconds / 0 artifacts (482919 inputs executed)
Stage reached: Stage A — the gate.toml parser, the frozen parsing and CLI tests of SPEC.md §8,
tools/ci.sh with all six stages green, and the libFuzzer target with its checked-in corpus.
Stages B, C and D are not started.
What I could not do: the runner, the graph modes and the fleet conversion are stages B, C and
D — `kit-ci` with no arguments, `--tier`, `--changed`, `--graph` and `--graph-html` each exit
2 with "not implemented yet" rather than pretending to run.
What I had to guess: the `tree` stage (SPEC.md §6 does not name it; the kit's pre-push hook
passes --require-clean, which only `tree` implements) and which fault is reported when one
config has several ("earliest line wins"). Questions Q1 and Q2 in QUESTIONS.md, with Q3–Q8
covering the smaller readings.
