Gate verdict line (verbatim):
    all 6 stage(s) passed in 186s
    GATE PASSED
Test count: 53 — the runner's own line was "100% tests passed, 0 tests failed out of 53".
(36 carried from stages A and B; 17 added by stage C: 7 library tests in
tests/graph_test.cpp and 10 process-level tests in tests/cli_test.cpp. The stage-C tests were
written first against stub formats: the RED run was "75% tests passed, 13 tests failed out of
52", and the 13 are the ones that pin new behaviour. Three of the 17 were green in that RED
run for a reason other than the one they now pin — the binary already exited 2 with the same
message shape for a flag it did not know and for an invalid config — and they are recorded
here as such rather than counted as TDD witnesses.)
Fuzz stage: runs / seconds / artifacts: 1 invocation / 60 seconds / 0 artifacts (456,935
inputs executed). Stage C extended the target: every accepted input is now rendered in all
three graph formats, and an empty rendering, a non-deterministic one, or one carrying an
absolute URL aborts (SPEC.md §5 puts the graph output under the parser's total contract).
Stage reached: Stage C — the graphs. `--graph --format mermaid|html|dot` (default mermaid),
`--graph-html` as the documented shorthand, exit 2 on an unknown or missing format and on
`--format` without `--graph`, `--list --graph` refused, run options reported as ignored, and
byte-stability per format — plus the README's "why not pre-commit?" first screen with a real
rendered example. Committed as 26b1c41 on main, after ffad71e (stage B). Stage D — converting
the docsum repo to a `gate.toml` — is not started, and the fleet-wide conversion is not in
scope for v1.
What I could not do: check the diagrams as pictures. This box has no renderer and this session
has none either (the `dot` binary is deliberately absent per the amendment, there is no
Mermaid CLI, and the browser available here is remote and cannot read a `file://` path), so the
HTML page and the Mermaid flow were verified by their structure rather than by looking at them:
the page was parsed with Python's html.parser (tags balanced, one `<rect>` per stage, no
`xmlns`, no `http://` or `https://` anywhere) and the dot with pydot (one digraph, one cluster
per tier, one node per stage, run-order edges) — but whether the colours and the layout look
right to a human is not something this run measured. Also: KitCI still runs on `tools/ci.sh`,
so the README's example is the SPEC-shaped Python config rather than this repo's own gate.toml
(that is stage D's work); every line of output shown in the README is still verbatim binary
output.
What I had to guess: seven readings, Q17–Q23 in QUESTIONS.md, each with its question.
Q17 the CLI shape (`--format` refused without `--graph`; flags read left to right); Q18 that
`--graph` prints the graph and reports the run options it ignores on stderr rather than failing
them; Q19 that `--list --graph` is exit 2; Q20 how one node carries several tiers in each of
the three formats (and what a renderer that keeps only the last Mermaid `classDef` will do with
it); Q21 that the standalone page drops the SVG `xmlns`, because that attribute is the one
absolute URL §8's self-containment test forbids; Q22 that `--graph` answers a config with no
stages with exit 0, because exit 2 is a run's answer; and Q23, what this README may claim about
pre-commit — the amendment's fourth difference ("pre-commit stops at the first failing hook")
does not survive a check of pre-commit's own documentation (`fail_fast` is optional and
`false` by default), so the README claims only the differences that do, and says where the
ideas were borrowed from.
