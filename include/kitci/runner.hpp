// The runner: a parsed gate.toml in, executed stages and one verdict out (SPEC.md §4, §9).
//
// Everything here is a function of the config plus the options — no argv, no stdout, no
// git. main.cpp owns what a human sees; the tests drive run_tier() directly.
#ifndef KITCI_RUNNER_HPP
#define KITCI_RUNNER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "kitci/parser.hpp"

namespace kitci {

struct RunOptions {
    // SPEC.md §2/§4: --changed scopes every stage that has a `files` glob to the files
    // changed against the merge base with main. The list is a parameter rather than a git
    // call so the rule can be tested without a repository.
    bool changed = false;
    std::vector<std::string> changed_files;
    // SPEC.md §2/§5: --strict is the default. Strict refuses a stage whose cmd is empty,
    // because a stage that checks nothing is a gate that lies (QUESTIONS.md Q10).
    bool strict = true;
};

enum class StageStatus : std::uint8_t { kPassed, kFailed, kSkipped };

struct StageResult {
    std::string name;
    std::string cmd;
    StageStatus status = StageStatus::kPassed;
    int exit_code = 0;  // the stage command's exit code; 128+N when it died by signal
    bool timed_out = false;
    std::string fail_reason;  // why it failed, empty when it passed
    std::string skip_reason;  // why it was skipped, empty when it ran
    std::string output;       // combined stdout+stderr, complete
    int summary_lines = 40;   // SPEC.md §4's `summary = "head:<n>"`
    double seconds = 0.0;
};

struct RunResult {
    int exit_code = 0;    // 0 every stage passed, 1 a stage failed, 2 nothing ran
    std::string verdict;  // the one verdict line (SPEC.md §2)
    std::string error;    // set when the config is unusable: no stage ran
    std::vector<StageResult> stages;
};

// Run one tier: its stages in run order, every failure accumulated, none of them stopping
// the run (SPEC.md §4). A tier that resolves to no stages is exit 2, never a pass.
RunResult run_tier(const Config& config, const Tier& tier, const RunOptions& options);

// SPEC.md §4's glob: `*`, `?` and `**` all cross '/' — the documented example, files =
// "*.py", has to match sources that live in subdirectories (QUESTIONS.md Q11).
bool glob_matches(const std::string& pattern, const std::string& path);

// SPEC.md §4's `summary = "head:<n>"`: the first n lines of a failed stage's output.
std::string head_lines(const std::string& text, int lines);

// The files changed against the merge base with main (SPEC.md §2, §4). known == false when
// git cannot answer, with `note` saying why; the caller decides what that means.
struct ChangedFiles {
    bool known = false;
    std::vector<std::string> files;
    std::string note;
};

ChangedFiles changed_files_from_git(const std::string& repo_dir);

}  // namespace kitci

#endif  // KITCI_RUNNER_HPP
