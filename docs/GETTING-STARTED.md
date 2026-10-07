# Getting started — five minutes

KitCI is two halves, and only one of them is per repo.

- **The policy is `gate.toml`, in the repo.** It names the stages, the tiers and their failure
  rules — no GitHub, no service, no account, nothing leaves the machine.
- **The engine is `kit-ci`, one binary per machine**, installed once. A repo carries policy
  only; the engine is never committed.

Adopting it in a repo means copying in `gate.toml`, `scripts/gate.sh` and `.githooks/` (from a
converted repo — `docsum`, `Permuto` — or the kit's `examples/`) and editing `gate.toml`.

## 1. Install the engine (once per machine)

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release   # in a KitCI checkout
cmake --build build-release
cmake --install build-release --prefix ~/.local          # -> ~/.local/bin/kit-ci
```

Release, not Debug: this is the binary every gate on the machine runs (351,456 bytes
installed, against the Debug build's 3,008,096 — 8.6x).

## 2. Arm the hooks (once per clone)

```bash
git config core.hooksPath .githooks
```

This is the step nothing enforces: git only runs `.git/hooks/`, so a clone without this line
commits and pushes with **no gate at all** and looks armed while doing it. Measured — the
unarmed clone's commit prints nothing; the armed clone's runs the fast tier.

`gate.toml`, `scripts/` and `.githooks/` are TRACKED, so the policy travels with the clone — a
`.githooks` line in `.gitignore` would silently disarm every clone. Untracked: the gate's own
footprint, `build/` and `.ci-logs/`.

## 3. Then just work

| you run | what happens |
|---|---|
| `git commit` | `pre-commit` runs the fast tier — docsum: 5 stages, 4.0 s |
| `git push` | `pre-push` runs the full tier — docsum: 6 stages, 14.5 s |
| `./scripts/gate.sh` | the full tier by hand — same stages, same verdict |
| `./scripts/gate.sh --tier fast` | the fast tier by hand |

Deliberate bypass, never accidental: `git commit --no-verify`, `git push --no-verify`.

Run it from the repo root: `gate.toml` is read from the current directory with no upward
search, so a subdirectory run is exit 2 (`--gate <path>` points elsewhere). The wrapper `cd`s
first, so the hook, the hand run and the nightly clone agree.

The clean-tree rule is per repo, not a `kit-ci` flag: a repo that wants it exports
`CI_REQUIRE_CLEAN=1` in its `pre-push` hook and its own tree stage reads it (Permuto does;
docsum has no such stage). A hand run is never required to be clean.

## 4. Read the gate

```bash
kit-ci --list     # stages, tiers, and which stages are in which tier
kit-ci --graph    # the flow as Mermaid text (--format dot | html for the other two)
kit-ci --ast      # the parsed config as canonical JSON (pipe it to jq)
```

Stage attributes: `cmd` (a shell string — the escape hatch), `tier`, `fail_on` (`nonzero`,
`output:<regex>`, `exit:<n>`), `timeout`, `files` (a glob, for `--changed`), `when`
(`tool:<name>` — a clean skip when the tool is missing) and `summary` (how much failed output
is shown).

## 5. Exit codes

`0` every stage passed · `1` a stage failed, and the verdict names each culprit · `2` nothing
ran — the config is unreadable or invalid, or the tier resolved to no stages.

Measured: an unknown key prints `kit-ci: unknown key 'colur' in [stage.oops] (line 6)` and
exits 2; a two-stage config whose second stage fails exits 1 with `GATE FAILED — 1 passed,
1 failed (carefully-fails), 0 skipped`; `./scripts/gate.sh` in a clone with no `kit-ci` on PATH
exits 1 with `gate: kit-ci is not installed.` — a missing engine cannot read as green.
