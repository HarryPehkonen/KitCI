#!/usr/bin/env bash
#
# Local CI — every stage KitCI has, in one script, with no service anywhere.
#
# from AI-DEV-STARTER (plunk-in kit), adapted to this repo's stage set (SPEC.md §6):
#   format -> build -> lint -> tests -> fuzz
# if you edit this file, say why in INCIDENTS.md.
#
# No GitHub, no network, no framework: this is what the git hooks in .githooks/ run, and you
# can run it by hand at any time (it is non-destructive — nothing is committed, staged,
# reverted or reformatted for you).
#
#   tools/ci.sh                      # all default stages, in order
#   tools/ci.sh build tests          # just these stages, in the order given
#   tools/ci.sh --list               # what the stages are
#   tools/ci.sh --help
#
#   git config core.hooksPath .githooks     # one-time, per clone, enables the hooks
#
# Two tiers, because a full run includes a 60-second fuzz campaign and a commit cannot
# afford one. Both lists live below as CI_FAST_STAGES and CI_FULL_STAGES, and the hooks NAME
# a tier rather than repeating its stages, so a stage cannot be added to the gate and
# forgotten by a hook:
#
#   fast  (pre-commit)  format build tests
#   full  (pre-push)    --require-clean tree format build lint tests fuzz
#
# `tree` is the one stage SPEC.md §6 does not name: it is the kit's footprint and
# clean-worktree guard, and without it the `--require-clean` the kit's pre-push hook passes
# would mean nothing. It is listed in QUESTIONS.md.
#
# Configuration lives in .ci.env (gitignored, optional); every knob has a default here, so
# the repo works with no config at all. See .ci.env.example.
#
# Exit status: 0 only if every stage that ran passed.
#
# One deliberate reading of "report every failure": a failing stage prints EVERYTHING that
# failed inside it and then STOPS the run, because these stages are a chain — once the build
# fails, the tests and the fuzzer would only repeat the same compiler error with more noise.
# Where a run stops, the summary says so.

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$REPO_ROOT" || exit 1

# No colour from the tools: this script greps their output (warning:, error:) and ANSI
# escapes defeat the greps. The escapes this script prints itself are for the human.
export NO_COLOR=1

# git exports GIT_INDEX_FILE to a hook when the commit is made with a PATHSPEC
# (`git commit -- <path>`): it names git's TEMPORARY index for that one commit, not this
# repository's index. Every process a hook starts inherits it, and any `git` command the
# gate runs inside ANOTHER repository then reads THIS repo's index entries against that
# repository's object store and dies on the first blob it does not have. Unset it once,
# here, rather than in front of each command that shells out to git.
unset GIT_INDEX_FILE

# ---------------------------------------------------------------- defaults + config
CI_JOBS=${CI_JOBS:-$(nproc 2>/dev/null || echo 4)}
CI_BUILD_DIR=${CI_BUILD_DIR:-build}
CI_CLANG_BUILD_DIR=${CI_CLANG_BUILD_DIR:-build-clang}
CI_FUZZ_BUILD_DIR=${CI_FUZZ_BUILD_DIR:-build-fuzz}
CI_LOG_DIR=${CI_LOG_DIR:-.ci-logs}
CI_STRICT_TOOLS=${CI_STRICT_TOOLS:-0}           # 1 = a missing tool fails instead of SKIPping
CI_FAST_STAGES=${CI_FAST_STAGES:-"format build tests"}
CI_FULL_STAGES=${CI_FULL_STAGES:-"tree format build lint tests fuzz"}
CI_DEFAULT_STAGES=${CI_DEFAULT_STAGES:-$CI_FULL_STAGES}
CI_BUILD_TYPE=${CI_BUILD_TYPE:-Debug}
CI_FUZZ_SECONDS=${CI_FUZZ_SECONDS:-60}          # SPEC.md §6: the campaign runs 60 seconds
# The source set the format and lint stages own. Extend for your layout.
CI_SOURCE_GLOBS=${CI_SOURCE_GLOBS:-"'*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h'"}
# The test runner. ctest is the portable default; override with a single test binary if your
# project does not register tests with add_test().
CI_TEST_CMD=${CI_TEST_CMD:-"ctest --test-dir \$CI_BUILD_DIR --output-on-failure -j \$CI_JOBS"}

if [ -f .ci.env ]; then
    # shellcheck disable=SC1091
    . ./.ci.env
fi

REQUIRE_CLEAN=0
ALLOW_UNTRACKED=0
STAGES_REQUESTED=()

# ---------------------------------------------------------------- plumbing
RESULT_LINES=()
FAILED_STAGE=""
RAN_STAGES=()

usage() {
    # The whole leading comment block: every line from 2 to the first blank one. Derived
    # rather than hard-coded, so adding a line to the header cannot silently cut it off.
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Stages:
  tree        every file committed or ignored; .gitignore audit; the gate's own footprint
              (build dirs, logs, .ci.env) is ignored; --require-clean also fails on
              uncommitted changes to tracked files
  format      clang-format drift — dry run against the repo .clang-format
  build       cmake configure + build with gcc AND clang, zero warnings on both (-Werror,
              and the stage counts warnings itself for targets -Werror does not cover)
  lint        clang-tidy over every source in the compile database; findings are fixed in
              the code, never by widening .clang-tidy
  tests       the test suite (ctest), and the count is printed
  fuzz        the gate.toml parser as a libFuzzer target
              (-fsanitize=fuzzer,address,undefined), run CI_FUZZ_SECONDS (default 60)
              starting from the checked-in fuzz/corpus/. An artifact is a finding

Options:
  --require-clean     make the tree stage fail when tracked files have uncommitted edits
  --allow-untracked   do not fail when untracked, unignored files exist (deliberate escape)
  --strict-tools      a missing tool (clang-format/clang-tidy) fails instead of skipping
  --list              list the stages and exit
  --help              this text
EOF
}

ci_begin() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
ci_pass() { RESULT_LINES+=("pass  $1"); }
ci_skip() {
    RESULT_LINES+=("SKIP  $1 ($2)")
    printf '    SKIP: %s\n' "$2"
}

ci_fail() {
    local name="$1" reason="$2" logfile="${3:-}"
    RESULT_LINES+=("FAIL  $name")
    printf '\n\033[1;31mFAILED: %s — %s\033[0m\n' "$name" "$reason"
    if [ -n "$logfile" ] && [ -f "$logfile" ]; then
        printf '    last output from %s:\n' "$logfile"
        tail -n 25 "$logfile" | sed 's/^/      /'
        printf '    full log: %s\n' "$logfile"
    fi
    FAILED_STAGE="$name"
    summary
    printf '\nGATE FAILED\n' >&2
    exit 1
}

summary() {
    printf '\n--- ci summary ---\n'
    local line
    for line in "${RESULT_LINES[@]}"; do printf '  %s\n' "$line"; done
    if [ -n "$FAILED_STAGE" ]; then
        printf '  stopped at: %s\n' "$FAILED_STAGE"
        # A stage that never ran because an earlier one failed must not look like a stage
        # that passed: once the build fails, the test stage has nothing trustworthy to say,
        # so it is reported as blocked, never as green.
        local s r ran
        for s in "${STAGES_REQUESTED[@]:-}"; do
            [ -n "$s" ] || continue
            ran=0
            for r in "${RAN_STAGES[@]:-}"; do
                [ "$r" = "$s" ] && ran=1
            done
            if [ "$ran" = "0" ] && [ "$s" != "$FAILED_STAGE" ]; then
                printf '  BLOCK %s (did not run: the run stopped at %s)\n' "$s" "$FAILED_STAGE"
            fi
        done
    fi
}

require_tool() {  # require_tool <tool> <stage>; returns 1 when the caller should skip
    local tool="$1" stage="$2"
    if command -v "$tool" >/dev/null 2>&1; then return 0; fi
    if [ "$CI_STRICT_TOOLS" = "1" ]; then
        ci_fail "$stage" "$tool not installed (CI_STRICT_TOOLS=1) — nothing was checked"
    fi
    ci_skip "$stage" "$tool not installed — this gate was NOT exercised on this machine"
    return 1
}

# The file set the format/lint stages own. --others --exclude-standard includes new files
# that are not committed yet: while working, a new source file is not in `git ls-files`, and
# a gate that cannot see it lets it through unformatted until after it is committed.
ci_sources() {
    # shellcheck disable=SC2086
    eval "git ls-files --cached --others --exclude-standard -- $CI_SOURCE_GLOBS" | sort -u
}

# clang-tidy needs a compile database entry per translation unit, so headers are not passed
# here: diagnostics inside headers still surface through the sources that include them.
ci_tidy_sources() {
    # shellcheck disable=SC2086
    eval "git ls-files --cached --others --exclude-standard -- $CI_SOURCE_GLOBS" \
        | grep -E '\.(cpp|cc|cxx)$' | sort -u
}

run_tests() {  # run_tests <build-dir> <logfile>
    # shellcheck disable=SC2086
    eval "$CI_TEST_CMD" > "$2" 2>&1
}

count_warnings() {  # count_warnings <stage> <logfile> <label>
    local stage="$1" logfile="$2" label="$3" warns
    warns=$(grep -c 'warning:' "$logfile" || true)
    if [ "${warns:-0}" -gt 0 ]; then
        grep 'warning:' "$logfile" | head -5 | sed 's/^/      /'
        ci_fail "$stage" "$warns compiler warning(s) in the $label build — -Werror does not cover every target"
        return 1
    fi
    return 0
}

mkdir -p "$CI_LOG_DIR"

# ---------------------------------------------------------------- stages
stage_tree() {
    ci_begin "tree (every file committed or ignored)"
    # The gate creates these; a .gitignore that does not cover them makes the next run fail
    # the moment it writes a log. Create them first: git check-ignore cannot match a
    # directory pattern against a path that does not exist yet.
    mkdir -p "$CI_BUILD_DIR" "$CI_CLANG_BUILD_DIR" "$CI_FUZZ_BUILD_DIR" "$CI_LOG_DIR"

    local untracked
    untracked=$(git ls-files --others --exclude-standard)
    if [ -n "$untracked" ]; then
        printf '    not committed and not ignored:\n'
        printf '%s\n' "$untracked" | sed 's/^/      /'
        if [ "$ALLOW_UNTRACKED" = "1" ]; then
            printf '    (not failing: --allow-untracked was passed)\n'
        else
            ci_fail tree "$(printf '%s\n' "$untracked" | wc -l) file(s) are neither committed nor ignored — git add them, or add a .gitignore rule"
        fi
    else
        printf '    no untracked, unignored files\n'
    fi

    local dirty
    dirty=$(git status --porcelain --untracked-files=no)
    if [ -n "$dirty" ]; then
        printf '    uncommitted changes to tracked files:\n'
        printf '%s\n' "$dirty" | sed 's/^/      /'
        if [ "$REQUIRE_CLEAN" = "1" ]; then
            ci_fail tree "uncommitted changes to tracked files (that is the point of the push gate)"
        fi
        printf '    (not failing: --require-clean was not passed)\n'
    else
        printf '    no uncommitted changes to tracked files\n'
    fi

    local tracked_ignored
    tracked_ignored=$(git ls-files -i -c --exclude-standard)
    if [ -n "$tracked_ignored" ]; then
        printf '%s\n' "$tracked_ignored" > "$CI_LOG_DIR/tree.log"
        ci_fail tree "tracked files matched by .gitignore (stale rules) — see $CI_LOG_DIR/tree.log"
    fi

    local path missing=0
    for path in "$CI_BUILD_DIR" "$CI_CLANG_BUILD_DIR" "$CI_FUZZ_BUILD_DIR" "$CI_LOG_DIR" ".ci.env"; do
        if ! git check-ignore -q "$path" 2>/dev/null; then
            printf '    NOT ignored: %s\n' "$path"
            missing=$((missing + 1))
        fi
    done
    if [ "$missing" -gt 0 ]; then
        ci_fail tree "$missing path(s) that the gate itself creates are not in .gitignore — the next run would fail on its own log files"
    fi
    printf '    gate footprint (build dirs, %s, .ci.env) is ignored\n' "$CI_LOG_DIR"
    ci_pass tree
}

stage_format() {
    ci_begin "format (clang-format --dry-run) — the files this branch touches"
    require_tool clang-format format || return 0
    # Only the files this branch touches: a whole-tree check buries the signal in noise
    # nobody edited. On a clean checkout (nothing ahead of origin/main) fall back to HEAD's
    # own commit.
    local base touched
    base="$(git merge-base HEAD origin/main 2>/dev/null || git rev-parse HEAD)"
    touched="$(
        { git diff --name-only --diff-filter=ACMR "$base" HEAD
          git diff --name-only --diff-filter=ACMR HEAD
          git diff --cached --name-only --diff-filter=ACMR
          git ls-files --others --exclude-standard
        } | sort -u
    )"
    if [ -z "$touched" ]; then
        touched="$(git show --name-only --pretty=format: HEAD | sed '/^$/d')"
        printf '    (level with origin/main: checking the last commit instead)\n'
    fi
    local -a sources=()
    while IFS= read -r f; do
        [ -n "$f" ] && [ -f "$f" ] && sources+=("$f")
    done < <(printf '%s\n' "$touched" | grep -E '\.(cpp|cc|cxx|hpp|hh|h)$')
    if [ "${#sources[@]}" -eq 0 ]; then
        printf '    nothing to check\n'
        ci_pass format
        return 0
    fi
    if clang-format --dry-run -Werror "${sources[@]}" > "$CI_LOG_DIR/format.log" 2>&1; then
        # The check above read the WORKING TREE, and a commit records the INDEX. Stage an
        # unformatted file, then format it on disk, and the check above passes while the
        # commit still records the unformatted text. So check the staged copy too.
        local -a staged=() index_drift=()
        local f staged_f
        while IFS= read -r staged_f; do
            [ -n "$staged_f" ] && [ -f "$staged_f" ] && [[ "$staged_f" =~ \.(cpp|cc|cxx|hpp|hh|h)$ ]] && staged+=("$staged_f")
        done < <(git diff --cached --name-only --diff-filter=ACMR)
        if [ "${#staged[@]}" -gt 0 ]; then
            for f in "${staged[@]}"; do
                git show ":$f" 2>/dev/null |
                    clang-format --dry-run -Werror --assume-filename="$f" - > /dev/null 2>> "$CI_LOG_DIR/format.log" ||
                    index_drift+=("$f")
            done
            if [ "${#index_drift[@]}" -gt 0 ]; then
                printf '    the STAGED copy is not formatted (that is what the commit would record):\n'
                printf '%s\n' "${index_drift[@]}" | sed 's/^/      /'
                ci_fail format "the staged copy of the file(s) above fails clang-format -- this stage checks the working tree, and a commit records the index (fix: clang-format -i <files> && git add <files>)" "$CI_LOG_DIR/format.log"
                return 1
            fi
        fi
        printf '    %s file(s) conform to .clang-format\n' "${#sources[@]}"
        ci_pass format
        return 0
    fi
    grep -oE '^[^:]+\.(cpp|cc|cxx|hpp|hh|h)' "$CI_LOG_DIR/format.log" | sort -u | sed 's/^/      /'
    ci_fail format "clang-format drift in the files listed above (fix: clang-format -i <those files>)" "$CI_LOG_DIR/format.log"
}

stage_build() {
    ci_begin "build (gcc and clang, zero warnings — warnings are errors)"
    # An existing build dir pins its compiler, so each configuration keeps its own directory.
    # shellcheck disable=SC2086
    cmake -S . -B "$CI_BUILD_DIR" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE="$CI_BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ${CI_CMAKE_FLAGS:-} > "$CI_LOG_DIR/configure.log" 2>&1 \
        || ci_fail build "gcc configure failed" "$CI_LOG_DIR/configure.log"
    cmake --build "$CI_BUILD_DIR" -j "$CI_JOBS" > "$CI_LOG_DIR/build.log" 2>&1 \
        || ci_fail build "gcc build failed (-Werror is on: a warning is a build failure)" "$CI_LOG_DIR/build.log"
    count_warnings build "$CI_LOG_DIR/build.log" "gcc" || return 1
    printf '    gcc: built with no warnings\n'

    # The second compiler is not a second opinion for its own sake: gcc and clang do not
    # agree about warnings, which is the whole reason SPEC.md §6 asks for both.
    # shellcheck disable=SC2086
    cmake -S . -B "$CI_CLANG_BUILD_DIR" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE="$CI_BUILD_TYPE" \
        ${CI_CMAKE_FLAGS:-} > "$CI_LOG_DIR/clang-configure.log" 2>&1 \
        || ci_fail build "clang configure failed" "$CI_LOG_DIR/clang-configure.log"
    cmake --build "$CI_CLANG_BUILD_DIR" -j "$CI_JOBS" > "$CI_LOG_DIR/clang-build.log" 2>&1 \
        || ci_fail build "clang build failed (-Werror is on: a warning is a build failure)" "$CI_LOG_DIR/clang-build.log"
    count_warnings build "$CI_LOG_DIR/clang-build.log" "clang" || return 1
    printf '    clang: built with no warnings\n'
    ci_pass build
}

stage_lint() {
    ci_begin "lint (clang-tidy, no findings)"
    require_tool clang-tidy lint || return 0
    if [ ! -f "$CI_BUILD_DIR/compile_commands.json" ]; then
        ci_fail lint "no $CI_BUILD_DIR/compile_commands.json — the build stage configures with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON before lint can say anything"
    fi
    local -a sources=()
    mapfile -t sources < <(ci_tidy_sources)
    if [ "${#sources[@]}" -eq 0 ]; then
        ci_fail lint "no sources matched $CI_SOURCE_GLOBS — lint would analyse nothing and still pass"
    fi

    # A compile database that exists is not a compile database that covers this repo: CMake
    # can write one that holds a dependency's translation units and none of ours. clang-tidy
    # then analyses nothing, finds nothing, and still prints a result.
    local uncovered=0 src
    for src in "${sources[@]}"; do
        if ! grep -qF "\"$REPO_ROOT/$src\"" "$CI_BUILD_DIR/compile_commands.json"; then
            printf '    not in the compile database: %s\n' "$src"
            uncovered=$((uncovered + 1))
        fi
    done
    if [ "$uncovered" -gt 0 ]; then
        ci_fail lint "$uncovered of ${#sources[@]} sources are missing from $CI_BUILD_DIR/compile_commands.json — the result would be a lie"
    fi
    printf '    compile database covers all %s source(s)\n' "${#sources[@]}"

    printf '%s\n' "${sources[@]}" \
        | xargs -P "$CI_JOBS" -n 1 clang-tidy -p "$CI_BUILD_DIR" > "$CI_LOG_DIR/lint.log" 2>&1
    local findings
    findings=$(grep -cE 'warning:|error:' "$CI_LOG_DIR/lint.log" || true)
    if [ "${findings:-0}" -gt 0 ]; then
        grep -E 'warning:|error:' "$CI_LOG_DIR/lint.log" | sed 's/^/      /' | head -20
        ci_fail lint "$findings finding(s) — fix the code, never widen .clang-tidy" "$CI_LOG_DIR/lint.log"
    fi
    printf '    %s file(s) clean\n' "${#sources[@]}"
    ci_pass lint
}

stage_tests() {
    ci_begin "tests"
    if [ ! -f "$CI_BUILD_DIR/CMakeCache.txt" ]; then
        ci_fail tests "no build configured in $CI_BUILD_DIR — run the build stage first"
    fi
    if ! run_tests "$CI_BUILD_DIR" "$CI_LOG_DIR/tests.log"; then
        grep -E "FAILED|Failed|\*\*\*Failed|assert" "$CI_LOG_DIR/tests.log" | head -30 | sed 's/^/      /'
        ci_fail tests "test failures (all of them are above; full output in the log)" "$CI_LOG_DIR/tests.log"
    fi
    # The count is the point: a runner that collected nothing must not read as green.
    if grep -qE 'No tests were found|no tests were found' "$CI_LOG_DIR/tests.log"; then
        ci_fail tests "the test runner collected no tests — a gate that checks nothing is a gate that lies" "$CI_LOG_DIR/tests.log"
    fi
    local count_line
    count_line=$(grep -E 'tests passed' "$CI_LOG_DIR/tests.log" | tail -1)
    if [ -z "$count_line" ]; then
        ci_fail tests "the runner printed no test count — the count must be printed (SPEC.md §6)" "$CI_LOG_DIR/tests.log"
    fi
    printf '    %s\n' "$count_line"
    ci_pass tests
}

stage_fuzz() {
    ci_begin "fuzz (libFuzzer on the parser, ${CI_FUZZ_SECONDS}s, checked-in corpus)"
    require_tool clang++ fuzz || return 0
    if [ ! -d fuzz/corpus ]; then
        ci_fail fuzz "no fuzz/corpus/ — the campaign must start from the checked-in corpus"
    fi
    # shellcheck disable=SC2086
    cmake -S . -B "$CI_FUZZ_BUILD_DIR" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
        -DKITCI_BUILD_FUZZ=ON -DKITCI_BUILD_TESTS=OFF ${CI_CMAKE_FLAGS:-} \
        > "$CI_LOG_DIR/fuzz-configure.log" 2>&1 \
        || ci_fail fuzz "cmake configure failed" "$CI_LOG_DIR/fuzz-configure.log"
    cmake --build "$CI_FUZZ_BUILD_DIR" -j "$CI_JOBS" > "$CI_LOG_DIR/fuzz-build.log" 2>&1 \
        || ci_fail fuzz "the libFuzzer build failed (-fsanitize=fuzzer,address,undefined)" "$CI_LOG_DIR/fuzz-build.log"
    local binary="$CI_FUZZ_BUILD_DIR/fuzz_gate_toml"
    if [ ! -x "$binary" ]; then
        ci_fail fuzz "no fuzz target at $binary" "$CI_LOG_DIR/fuzz-build.log"
    fi

    # The corpus is COPIED into the ignored log dir before the run: libFuzzer writes the new
    # units it discovers into the first corpus directory, and the checked-in corpus must not
    # gain untracked files (the tree stage would then fail on the gate's own doing).
    local corpus="$CI_LOG_DIR/fuzz-corpus" artifacts="$CI_LOG_DIR/fuzz-artifacts"
    rm -rf "$corpus" "$artifacts"
    cp -r fuzz/corpus "$corpus"
    mkdir -p "$artifacts"

    local rc=0
    "$binary" "$corpus" -max_total_time="$CI_FUZZ_SECONDS" -artifact_prefix="$artifacts/" \
        -print_final_stats=1 > "$CI_LOG_DIR/fuzz.log" 2>&1 || rc=$?
    local artifact_count
    artifact_count=$(find "$artifacts" -type f | wc -l)
    if [ "$rc" -ne 0 ] || [ "${artifact_count:-0}" -gt 0 ]; then
        tail -n 25 "$CI_LOG_DIR/fuzz.log" | sed 's/^/      /'
        ci_fail fuzz "libFuzzer exited $rc with $artifact_count artifact(s) — an artifact is a finding to investigate, never to delete" "$CI_LOG_DIR/fuzz.log"
    fi
    local executed
    executed=$(sed -n 's/^stat::number_of_executed_units: *//p' "$CI_LOG_DIR/fuzz.log" | tail -1)
    printf '    runs: 1 invocation / %s seconds / %s artifact(s) / %s inputs executed\n' \
        "$CI_FUZZ_SECONDS" "$artifact_count" "${executed:-0}"
    ci_pass fuzz
}

# ---------------------------------------------------------------- dispatch
while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --list)
            printf 'default stages: %s\n' "$CI_DEFAULT_STAGES"
            # Derived from the defined functions, so this cannot drift from the stages the
            # script actually implements.
            printf 'stages:'
            for fn in $(declare -F | awk '{print $3}' | grep '^stage_' | sed 's/^stage_//' | sort); do
                printf ' %s' "$fn"
            done
            printf '\n'
            exit 0 ;;
        fast) STAGES_REQUESTED+=($CI_FAST_STAGES) ;;
        full) STAGES_REQUESTED+=($CI_FULL_STAGES) ;;
        --require-clean) REQUIRE_CLEAN=1 ;;
        --allow-untracked) ALLOW_UNTRACKED=1 ;;
        --strict-tools) CI_STRICT_TOOLS=1 ;;
        -*) printf 'unknown option: %s (try --help)\n' "$1" >&2; exit 2 ;;
        *) STAGES_REQUESTED+=("$1") ;;
    esac
    shift
done

if [ ${#STAGES_REQUESTED[@]} -eq 0 ]; then
    # shellcheck disable=SC2206
    STAGES_REQUESTED=($CI_DEFAULT_STAGES)
fi

printf '\033[1mlocal CI\033[0m — %s stage(s): %s\n' "${#STAGES_REQUESTED[@]}" "${STAGES_REQUESTED[*]}"
START=$(date +%s)
for stage in "${STAGES_REQUESTED[@]}"; do
    if ! declare -F "stage_$stage" > /dev/null; then
        printf 'unknown stage: %s (try --list)\n' "$stage" >&2
        exit 2
    fi
    # A stage that returns non-zero without reporting a verdict is not a pass, and a stage
    # that dies from a shell error cannot report anything at all — so neither is left to the
    # summary.
    if ! "stage_$stage"; then
        FAILED_STAGE="$stage"
        summary
        printf 'FAILED: %s exited non-zero without reporting a verdict\n' "$stage" >&2
        printf '\nGATE FAILED\n' >&2
        exit 1
    fi
    RAN_STAGES+=("$stage")
done
ELAPSED=$(( $(date +%s) - START ))

# The verdict comes from what RAN, not from what was requested: `set -u` plus an aborted
# stage can unwind out of the loop above without either guard seeing it, and a run that
# printed "all N stage(s) passed ... GATE PASSED" after executing one stage of ten would be
# the worst answer this script could give. This comparison is the backstop for that class.
if [ "${#RAN_STAGES[@]}" -ne "${#STAGES_REQUESTED[@]}" ]; then
    summary
    printf 'FAILED: %s of %s stage(s) did not run — the run ended early\n' \
        "$(( ${#STAGES_REQUESTED[@]} - ${#RAN_STAGES[@]} ))" "${#STAGES_REQUESTED[@]}" >&2
    printf '  ran: %s\n' "${RAN_STAGES[*]:-none}" >&2
    printf '\nGATE FAILED\n' >&2
    exit 1
fi

summary
printf '\nall %s stage(s) passed in %ss\nGATE PASSED\n' "${#STAGES_REQUESTED[@]}" "$ELAPSED"
