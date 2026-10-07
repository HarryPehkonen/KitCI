Gate verdict line (verbatim):
    all 6 stage(s) passed in 267s
    GATE PASSED
    (the default tier, run by hand on the staged tree this report describes: tree format build
    lint tests fuzz, for 267s. The push that followed ran it again as the pre-push tier with
    `--require-clean` and passed; its own line is not pasted here, because pasting it would
    mean amending the commit it gated. Same tier, same six stages.)
Test count: 56 — the runner's own line is "100% tests passed, 0 tests failed out of 56".
Nothing was added: this card is documentation, and it changes no engine behaviour, so no test
was written (the missing test is the README recipe, in "What I could not do"). The suite also
retired a claim: the README's build block said "53 tests", which was stale by three.
Fuzz stage: 1 invocation / 60 seconds / 0 artifact(s) / 294877 inputs executed. Unchanged
from the stage-D sitting; this card touched no C++.
Stage reached: none — no stage of SPEC.md. The card is the documentation Harri asked for after
the CMake-workflow question, and it is two deliverables: the five-minute guide and two README
fixes.
Deliverables:
  1. docs/GETTING-STARTED.md — NEW, 77 lines (56 of them non-blank): the policy/engine split,
     the once-per-machine install, the once-per-clone hook arming, the three callers, how to
     read the gate, what is tracked, and the three exit codes. One claim per line, and no
     command in it that was not run first.
  2. README.md — GETTING-STARTED.md is now linked under the intro (line 9); the install recipe
     is a Release configure in its own build dir (`build-release`), because the recipe it
     replaces installed whatever `build` happened to hold — measured, that was a 3,008,096-byte
     Debug engine; "53 tests" → "the whole suite" (renders itself stale-proof); and
     "Converting a repo" item 1 now points at the Release recipe instead of restating a Debug
     `--install`.
Evidence — every command in GETTING-STARTED, run by hand in scratch clones under
/home/harri/.hermes/kanban/workspaces/t_ca1aa5eb/scratch (KitCI clone at 72eb1cb, docsum clone,
a docsum clone left deliberately unarmed), output verbatim:
  §1 (install, run twice, second time byte-identical):
    $ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release
    $ cmake --install build-release --prefix ~/.local
      -- Install configuration: "Release"
      -- Installing: /home/harri/.local/bin/kit-ci
      -rwxr-xr-x 351456  (sha256 1c7334a348a61867dcd9c3b865e3eda094afa63a0c9df8eaceaea6003eaa6b20)
    the binary it replaced: 3,008,096 bytes, sha256 13f49dca… (a Debug build — the gap the card
    named). Sizes are the measured difference; a speed difference is NOT: 300 runs of `--list`
    and 200 of `--graph` under each binary give Debug 1.60/1.60 ms median against Release
    1.69/1.45 ms — process startup dominates a 1.5 ms parse, so the honest reason to install
    Release is bytes on disk and no -O0 Debug code, not a benchmark I can show.
  §2 (arming, and the clone that never armed):
    $ git config core.hooksPath .githooks      # armed clone
    $ git commit --allow-empty -m "…"          # -> 5 stages, GATE PASSED — 5 passed, 0 failed
    $ git commit --allow-empty -m "…"          # unarmed clone -> NOTHING printed: no hook ran
    $ git commit --no-verify --allow-empty     # -> [main fefa54e], no hook output
  §3 (the three callers, timed in the docsum clone):
    $ time ./scripts/gate.sh                   # GATE PASSED — 6 passed, 0 failed, 0 skipped
                                               # real 14.528 s
    $ time ./scripts/gate.sh --tier fast       # GATE PASSED — 5 passed, 0 failed, 0 skipped
                                               # real 3.965 s
    the commit above is the hook caller; the clone is the nightly caller (a fresh `git clone`,
    no .venv, no dev tooling).
    the clean-tree rule, checked as the card asked rather than assumed: Permuto's .githooks/
    pre-push does `export CI_REQUIRE_CLEAN=1 && exec scripts/gate.sh --tier full` and
    scripts/tree.sh reads it; docsum's pre-push runs `--tier full` with no such line and docsum
    has no tree stage; neither wrapper passes `--require-clean` (that flag is tools/ci.sh's,
    the C++ gate's); `kit-ci --help` has no such option. So the rule is per repo, and the doc
    says that.
  §4:
    $ kit-ci --list     -> stages: lint format tests types cleanenv identity / tier fast: … 5 /
                           tier full: … 6
    $ kit-ci --graph    -> Mermaid, 20 lines (--format dot: "digraph kitci_gate {"; --format
                           html: 4,325 bytes, 0 occurrences of "http")
  §5 (all three codes, and the missing engine):
    $ kit-ci --gate fixtures/unknown-key.toml
      kit-ci: unknown key 'colur' in [stage.oops] (line 6)                      exit 2
    $ kit-ci --gate fixtures/failing-stage.toml
      ==> carefully-fails
          cmd: printf 'the check said no\n'; exit 1
          FAIL (exit 1)
      GATE FAILED — 1 passed, 1 failed (carefully-fails), 0 skipped             exit 1
    $ env PATH=/usr/bin:/bin ./scripts/gate.sh
      gate: kit-ci is not installed. Build KitCI, then: cmake --install build --prefix ~/.local
                                                                                exit 1
    $ kit-ci --list  (from docsum/docsum, a subdirectory)
      kit-ci: cannot read gate.toml                                             exit 2
    — the last one is not in the card's draft and is now in the doc: gate.toml is read from the
    current directory, with no upward search, which is exactly why the wrapper cd's to the repo
    root (README's "default: gate.toml in the repo root" describes intent, not a search).
What I could not do:
  - Cover any of it with a test. The repo's suite tests the engine; a doc is not executable, and
    the README recipe is three shell lines with no fixture. The recipe's evidence is the two
    installs it performed (from `build` and from `build-release`, byte-identical binaries), not
    a test that goes red if the `-DCMAKE_BUILD_TYPE` is deleted. A `docs` stage in the manner of
    Permuto's (which fails a document that quotes a test count) would have caught the stale 53;
    this repo has no such stage and adding one is not this card.
  - Show a Release speed win, because there is none to show at this size (numbers above).
  - Re-verify the docsum/Permuto conversions themselves. The hooks, gate.toml and scripts read
    here are the ones those cards (t_5ee2ee12, t_6989cec2) landed; nothing in either repo was
    edited by this card.
  - Render the HTML graph as a page. Still no renderer on this box (the stage-D note stands); it
    was checked as bytes only.
What I had to guess, and what I changed beyond the card:
  - The card's draft wrote the install as `-B build`. Both documents use `-B build-release`, so
    the machine install cannot reconfigure the dev build dir the README's own Debug loop uses.
    Same three commands, different dir name; run verbatim as documented, twice.
  - The card's draft attributed `CI_REQUIRE_CLEAN=1` to "pre-push or gate.sh --require-clean".
    Measured, it is neither in the engine nor in either wrapper: it is Permuto's pre-push
    exporting a variable its own tree stage reads (docsum has no such stage). Documented as it
    is, with the reason the flag cannot be an engine option (kit-ci has no per-run flags).
  - The exit-2 finding about `gate.toml` being read from the cwd, above. It is in the doc and
    here, and not in QUESTIONS.md: it is not an ambiguity but a documented default behaving as
    documented (`--gate <path>` is the answer). The README's `--gate` bullet still says "in the
    repo root", which reads as "the engine searches for it"; rewording that bullet was not this
    card's edit.
  - "≤ ~60 lines": the file is 77 lines, 56 non-blank, fences and table rows counted. Trimming
    further would cost the clean-tree paragraph or the exit-code evidence, both of which the
    card asked for by name.
  - The stale "53 tests" in the block I was editing anyway. Replaced with "the whole suite"
    rather than "56 tests", so the v1.1 card cannot make it stale again.
