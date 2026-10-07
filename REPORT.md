Gate verdict line (verbatim):
    all 6 stage(s) passed in 183s
    GATE PASSED
    (the pre-push tier, run by .githooks/pre-push on commit 1596500 — the push that published it:
    "all 6 stage(s) passed in 188s / GATE PASSED", 459,041 fuzz inputs, 0 artifacts. The push of the
    commit that added this paragraph was REFUSED by that same tier until the fuzz finding below was
    fixed; the final push ran it green — see the Fuzz stage field.)
Test count: 54 — the runner's own line was "100% tests passed, 0 tests failed out of 54".
(53 carried from stages A–C; the 54th is `GraphHtmlHidesAUrlInTheRepoName`, written by stage D and
watched fail first — see the fuzz paragraph below. Stage D added no other test and changed no other
engine behaviour, but it did add an `install(TARGETS kit-ci RUNTIME DESTINATION bin)` rule to
CMakeLists.txt, so a machine installs the engine instead of finding it in a build tree. That rule is
NOT covered by ctest: it is covered by running it — `cmake --install build --prefix ~/.local`
installed 3.0 MB to ~/.local/bin/kit-ci, and every docsum run below is that binary.)
Fuzz stage: runs / seconds / artifacts: 4 invocations at 60 seconds each; 1 artifact, fixed in this
sitting. Default tier: 442,338 inputs / 0 artifacts. Pre-push tier on 1596500: 459,041 inputs /
0 artifacts. Pre-push tier on the report commit: **123,240 inputs / 1 artifact**, which is why that
push was refused. Pre-push tier on the fix (2b2c8da): 425,125 inputs / 0 artifacts. (Every push runs
one more 60-second campaign, so for the version you are reading the number is a lower bound by one;
those verdicts are in gate-evidence/KitCI/.) The artifact
(`.ci-logs/fuzz-artifacts/crash-300de12c3ac55acba12edd364e9b90eacaaa41cf`, preserved at
`gate-evidence/KitCI/fuzz-artifact-repo-name-with-a-url.input`) is a config whose
`repo = "kit-chttp://i"` made the standalone HTML page carry an absolute URL inside its `<p>` — the
one thing the stage-C oracle aborts on. It is a stage-C defect, not a stage-D one: the target and
`src/graph.cpp` are byte-identical to 9b1136e (checked with `git diff`, not assumed), so the input
has been in the campaign's reach since stage C and this sitting's longer campaign is what hit it.
Fixed test-first — the new test fails on the old code and passes on the new — and the input is now a
checked-in corpus seed (`fuzz/corpus/repo-name-with-a-url.toml`), so a reintroduction aborts the
campaign when it loads the seed rather than waiting for a random mutation to find it again. The
reading the fix took, and what else it implies, is QUESTIONS.md Q29.
Stage reached: Stage D — fleet integration (SPEC.md §9): ONE repo converted, docsum. Its 552-line
copy of the kit's python gate is now `gate.toml` (six stages: lint format tests types cleanenv
identity, in two tiers) plus a 36-line wrapper (`scripts/gate.sh`) and four repo-owned stage scripts
it names — 239 lines of shell in the repo where 552 used to be, with every check kept and its teeth
re-measured rather than assumed. The two hooks are still the kit's files with one line changed each:
they name the tier (`--tier fast` on commit, `--tier full` on push), which is the line the old gate
drew with `GATE_TIER`. docsum's `.ai-dev-starter.json` records the three edited copies as `adapted`
with their new hashes (its own rule), `INCIDENTS.md` carries the wiring record the kit hook copies
require, and `README.md` gained "The gate". Verification, all of it on real runs: the full tier
`GATE PASSED — 6 passed, 0 failed, 0 skipped` (10 s warm, exit 0); the same tier from a fresh
`git clone` in a temp dir (the nightly pattern) with no `.venv` and no dev tooling; both hooks run as
git runs them (fast tier 5 stages on commit, full tier 6 on push); and two deliberate breaks —
`VERSION` forced to 9.9.9 → `GATE FAILED — 3 passed, 2 failed (tests, identity)`, and a new
unformatted `.py` → `GATE FAILED — 4 passed, 1 failed (format)` — both exit 1 with every culprit
named. A fresh clone with `kit-ci` removed from PATH exits 1 with a message, so a missing engine
cannot read as a green gate. Committed as 6823b36 in the docsum repo (11 files, +351/−547, one
commit ahead of origin/main, WITHOUT pushing: publishing that repo was not asked for). This repo's
side — the install rule, README.md ("Converting a repo" and the status line), QUESTIONS.md Q24–Q29,
this report, and the graph fix with its test and corpus seed — is committed and pushed to main. The
fleet-wide conversion is still not in scope, per §9.
What I could not do:
  - Cover the install rule with a test. `ctest` runs the suite; nothing in it exercises
    `cmake --install`, so that rule's evidence is the install it performed and the binary every
    docsum run in this sitting used, not a test that goes red when it is deleted.
  - Cover the docsum side with a test either. That repo's suite does not read `gate.toml`, so
    nothing there would fail if the policy file lost a stage — the honest guard is the
    `kit-ci --list` output, which is evidence, not a test. (A one-line check in docsum's own
    `tests/` that its `gate.toml` still declares the six stages would close this; it is not in
    this card's scope.)
  - Measure a cold clean-environment stage. `scripts/clean-env.sh` ran in 5.4–5.5 s every time
    because uv's cache was warm; what a first run on a machine that has never fetched pytest,
    openai, tiktoken and tqdm costs is not something this sitting measured (which is why the
    stage's timeout is 600 s rather than the 300 s default — a guess, not a measurement).
  - Render any of the graph formats as a picture, still: there is no renderer on this box. The HTML
    fix was checked as bytes and as structure (the page now carries `&#58;//` where the config
    spells `://`, and no `http://` anywhere), not by looking at it.
  - Say why the third campaign found what the first two did not. The first two runs of this stage
    found nothing at 442,338 and 459,041 inputs; the third found it at 123,240. The numbers record
    the campaigns' reach, and nothing in the target changed between them.
  - Convert more than one repo, or touch `Notes` and `TNGPlaylists`: §9 puts the fleet out of
    scope, and docsum is the one it names.
What I had to guess: six readings, Q24–Q29 in QUESTIONS.md, each with its question. Q24 where the
engine lives for a converted repo (installed once per machine via a new `install()` rule, never
committed — the measured price is that a clone without `kit-ci` on PATH cannot gate at all); Q25
that §9's "smallest `scripts/gate.sh` fleet member" is not true of docsum (552 lines, the largest of
the three) and the conversion was done anyway because the stage and the card name it; Q26 that
`--changed` is NOT the kit's touched-files logic and the file list therefore stays in a script —
under `--changed` a fresh clone, level with `origin/main`, resolves to an empty diff and would skip
the stages the nightly job exists to run; Q27 who guarantees a stage runs in the repo root and who
picks the tier (the wrapper cd's and keeps `GATE_TIER`; the hooks name `--tier`); Q28 the fidelity
ledger — the five things the 552-line script did that a `gate.toml` plus scripts does not reproduce
(the tools table, `STRICT_TOOLS=1`, the `kit probes` step, the identity stage's venv interpreter,
the `(last commit, …)` verdict note); Q29 the self-containment finding — whether the byte-level
check is the right invariant (kept: the page now guarantees it for every config) or should move to
fetching positions, and whether Mermaid and dot, whose comments still echo the repo name verbatim,
want the same treatment (they are outside the frozen test that made this a bug). One more, not worth
a question: §9's "3-line hook wrapper" is prose — the wrapper is 4 lines of code and 32 lines of
header explaining what the engine is and why a missing one is fatal.
