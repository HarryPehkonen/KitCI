# For agents — and for anyone who wants to see how little it takes

KitCI is built so that a gate is a file a person (or an agent) can read and edit. That claim is
easy to write down and worth measuring, so it was: **an agent that had never seen this system was
given a converted repo, denied every `*.md` file and the web, and asked to add a stage to the
gate.** It did, in a minute, in the repo's own conventions, and the result was verified by the
repo's owner.

This page is that measurement, the three properties that produce it, the guidance for writing a
`gate.toml` that has the property for *your* repo, and the same exercise a human can run in ten
minutes. If you are an agent reading this: everything here is also reachable from `gate.toml` and
`kit-ci --help`, which is the point. If you are here to adopt the gate, read
`GETTING-STARTED.md` instead — this page is about what you can get away with not reading.

## The experiment (2026-10-07)

A subagent with zero context was placed in a throwaway clone of a converted fleet repo and banned
from every `*.md` file and from the network. It was told three things: the repo path, that the
gate is driven by a config file called `gate.toml`, and that the engine is a binary called
`kit-ci`. Its whole task: a stage named `filesize` that fails if any file tracked by git is larger
than 2 MB (2097152 bytes) and passes otherwise, run in the FAST tier, with the detection actually
proved and the gate left green.

It did not need a fourth thing. In **64.6 seconds and twelve tool calls** (transcript, timed) it:

1. read `gate.toml` in full — in its own words, "the only permitted doc-style source";
2. read the scripts `gate.toml` names (`gate-env.sh`, `tree.sh`, `docs.sh`, `gate.sh`) and
   `.githooks/pre-commit`;
3. ran `kit-ci --help` and `kit-ci --list`;
4. wrote `scripts/filesize.sh` in the repo's conventions — sourced `gate-env.sh`, used
   `git ls-files -z` so a filename with a newline cannot slip past, and used the repo's own
   `fail()` helper;
5. added one `[stage.filesize]` table and one name in `[tier.fast]`;
6. proved the detection path with a 3 MB file, then ran the tier and left it green.

Two details are worth more than the summary. First, it added a guard nobody asked for — if
`git ls-files` returns nothing, the stage fails with
`filesize: git ls-files found no tracked files — this stage would check nothing and still pass`.
That is the rule the config's own comments state — *"A stage whose verdict is accidental is
the exact failure mode this whole gate exists to prevent."* Second, it noticed unasked that
`[tier.full] stages = ["*"]` would pick its stage up for pre-push, so the stage did not need a
second edit to reach the push tier.

**Verified by the spec owner on the agent's tree, not by the agent:** the fast tier printed
`GATE PASSED — 4 passed, 0 failed, 0 skipped`; a staged 3 MB file made the same tier fail with the
exact byte count and filename in the output; `kit-ci --list` showed `filesize` in both tiers. The
agent's own list of things it could not figure out was empty of anything a measurement could not
settle.

## Why it works — three properties, each one a repo author's decision

### 1. The config file is its own manual

`gate.toml` is tracked, so its comments travel with every clone — and those comments are the only
thing an agent with no docs is allowed to read. The fleet writes them as *why*, not *what*. Two
examples from converted repos:

```toml
# pytest's exit 5 is "no tests collected": a green suite of zero tests is the most
# expensive failure mode there is, so it fails here (kit-ci adds nonzero on top).
[stage.tests]
cmd = "scripts/py-tool.sh pytest -q -p no:cacheprovider"
fail_on = "exit:5"
```

```toml
# Not the whole tree: 10 of this repo's 25 .py files are unformatted (measured
# 2026-10-06), and checking pre-existing debt every run buries the file you just
# edited in noise nobody wrote. Same touched-file list the kit's python gate used.
[stage.format]
cmd = "scripts/format-changed.sh"
```

**For your repo:** every stage gets a line saying *why it exists and what failure it is guarding
against*, and any stage that departs from the whole-tree/happy-path default gets the measurement
that justifies the departure. When a check cannot be expressed in the vocabulary, say where it
went instead (Permuto's `gate.toml` ends with a section on a stage that did *not* survive the
conversion, with the probe results that killed it). An agent reading that file is reading the
maintenance notes of the person who wrote it.

### 2. The binary answers the questions the docs would

`kit-ci --help`, `--list`, `--ast` and `--graph` are readings of the *same parse the run uses* —
so they cannot describe a gate that does not exist, and they cannot go stale against it.

```
$ kit-ci --list
stages: tree docs format build tests version asan tsan fuzz tidy pristine package
tier fast: format build tests
tier full: tree docs format build tests version asan tsan fuzz tidy pristine package
```

The cold agent ran `--help` and `--list` and never needed a document to learn the vocabulary: what
a stage is, what `fail_on` accepts, which tier names exist, and what exit codes mean are all in the
binary's own surface. `--ast` materialises the defaults, so a key the config never wrote still
appears with the value the runner will use.

**For your repo:** nothing to do — this is the engine's property, and it holds for a config with
no comments at all. But it is why a stage can be added without reading `SPEC.md`, and why a reader
should be told to run `--help` before believing any prose, page included.

### 3. The vocabulary is small enough that one rich config demonstrates every attribute

This is the frozen-vocabulary dividend. A stage is a `cmd` plus six optional keys — `tier`,
`fail_on`, `timeout`, `files`, `when`, `summary`; the whole policy is `[gate]`, `[tier.*]` and
`[stage.*]`. Two files teach most of that language by example:

- `docsum/gate.toml` (49 lines) shows `cmd` naming a repo script, a `fail_on = "exit:5"`, one
  raised `timeout`, and a `[tier.fast]` that deliberately leaves a stage out;
- `Permuto/gate.toml` (122 lines) adds `when = "tool:<name>"`, twelve stages in declaration order,
  `--require-clean` moved into the environment, and the `"*"` full tier.

No converted config exercises all of it: `files` (with `--changed`) and `summary` are written in
no fleet `gate.toml` today — they live in `kit-ci --help`, `SPEC.md` and the README's example. So
the honest rule for a reader is: **copy from a config like yours, and run `kit-ci --help` for the
rest.**

**For your repo:** keep the shape familiar. Stages in declaration order, a comment per stage,
`[tier.fast]` small enough to be seconds, `[tier.full]` a `["*"]` unless you have a measured
reason. A repo that invents its own layout teaches the next reader (agent or human) nothing.

## Do it yourself — the same exercise, by hand

Pick any converted repo (`Permuto`, `docsum`, `jsonTools`, `Notes`, `TNGPlaylists`, …) and clone it
into a scratch directory. The policy comes with the clone; the engine is installed once per machine
(`GETTING-STARTED.md` §1).

```bash
git clone <a converted repo> scratch && cd scratch
kit-ci --list                    # what stages exist, and which tier runs which

# 1. add "filesize" to [tier.fast]:  stages = ["format", "filesize", "build", "tests"]
# 2. and add this stage at the end of gate.toml:
#
# no tracked file may exceed 2 MB (2097152 bytes): a build artifact committed by
# accident bloats every clone forever. A stage that checks nothing must not pass,
# so zero tracked files is a failure here.
[stage.filesize]
cmd = "n=$(git ls-files | wc -l) && [ $n -gt 0 ] && git ls-files -z | xargs -0 stat -c '%s %n' | awk '$1 > 2097152 {print; bad=1} END {exit bad+0}'"

kit-ci --tier fast               # GATE PASSED — 4 passed, 0 failed, 0 skipped  (~25 s: the build)

# now break it on purpose and watch the same command catch it
head -c 3145728 /dev/zero > big.bin && git add -f big.bin
kit-ci --tier fast
#   ==> filesize
#       cmd: n=$(git ls-files | wc -l) && …
#       FAIL (exit 1)
#       --- output, first 40 line(s) ---
#         3145728 big.bin
#   …
#   GATE FAILED — 3 passed, 1 failed (filesize), 0 skipped

rm big.bin && git reset          # put the clone back on its feet
```

Measured on a scratch clone of `Permuto`, 2026-10-07. The cold agent took the same route with one
difference worth copying: it wrote `scripts/filesize.sh` and let `gate.toml` name the script,
which is what a stage that needs more than one command should look like — `cmd` is any shell
string, and a 150-character line in a policy file is a stage nobody will want to edit.

## What this does not establish

- **One task, one model, one repo family.** A capable model, one stage, and repos whose gates were
  already written the way the fleet writes them. Edges were left untouched: `when`, `files` with
  `--changed`, `strict`/`--no-strict`, the error contract, a stage that needs the file list as
  arguments, and a repo with no gate at all.
- **It was not a cold start from nothing.** The agent was handed two names (`gate.toml`,
  `kit-ci`) and a config with comments. Nobody has yet run the inverse test: the same task in a
  repo whose `gate.toml` carries no comments, or a request to *design* a gate rather than extend
  one.
- The honest claim is therefore **agent-sufficient on the demonstrated surface** — add a stage to
  an existing, commented gate, cold, without reading prose. Not "agents never need docs."

## Can another CI system do this?

The direction of travel is real; KitCI's version of it is unusual. GitHub shipped **GitHub Agentic
Workflows** into technical preview in February 2026: repository automation authored in plain
Markdown and *executed by a coding agent* inside GitHub Actions, with the agent sandboxed and
read-only by default. That makes the CI *run* an agent, on a service, billed per run. KitCI's
property is the inverse and needs no service: nothing here is run by an agent — the config and the
binary are legible enough that an agent (or a person) can **extend the gate cold**, and the
evidence is the experiment above rather than an analogy. `pre-commit` is the nearest local
equivalent and is learnable, but through its documentation and its hook ecosystem — a hook is a
`repo` + `rev` cloned on first run, and knowing which hooks exist and how each fails is reading.
A `Makefile` or `justfile` target carries no machine-checkable semantics of its own: nothing
distinguishes a checkable stage from a helper target, declares a tier, or fixes what a failure
means. This is not a claim that KitCI is unique — `README.md` says what it borrowed from
`pre-commit`, by name. It is the measured difference on the one axis this page is about.
