// The runner (SPEC.md §4, §9 stage B): run a tier's stages, accumulate every failure, and
// answer with one verdict line and one exit code.
//
// Three properties this file exists to keep:
//   * every stage runs — a failure never stops the run, so one run names every culprit;
//   * a stage cannot outlive its `timeout` — each stage gets its own process group, so the
//     kill reaches the stage's children and not only the shell;
//   * nothing here exits the process or prints. run_tier() returns what main.cpp reports,
//     which is why the stage semantics are testable without a subprocess.
#include "kitci/runner.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace kitci {
namespace {

constexpr int kPollMillis = 50;

bool IsBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsBlank(text[begin])) {
        ++begin;
    }
    while (end > begin && IsBlank(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t newline = text.find('\n', pos);
        const std::string line =
            newline == std::string::npos ? text.substr(pos) : text.substr(pos, newline - pos);
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (newline == std::string::npos) {
            break;
        }
        pos = newline + 1;
    }
    return lines;
}

double SecondsSince(const std::chrono::steady_clock::time_point& started) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

// ---------------------------------------------------------------- globs (SPEC.md §4)

// fnmatch without FNM_PATHNAME: '*', '?' and '**' all cross '/'. The documented example,
// files = "*.py", has to match a source that lives in a subdirectory (QUESTIONS.md Q11).
bool MatchPattern(const char* pattern, const char* text) {
    while (*pattern != '\0') {
        const char c = *pattern;
        if (c == '*') {
            while (*pattern == '*') {
                ++pattern;
            }
            if (*pattern == '\0') {
                return true;
            }
            for (const char* candidate = text;; ++candidate) {
                if (MatchPattern(pattern, candidate)) {
                    return true;
                }
                if (*candidate == '\0') {
                    return false;
                }
            }
        }
        if (*text == '\0') {
            return false;
        }
        if (c != '?' && c != *text) {
            return false;
        }
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

bool AnyChangedFileMatches(const std::string& pattern, const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        if (MatchPattern(pattern.c_str(), path.c_str())) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- the environment

// SPEC.md §4: `when = "tool:<name>"` asks whether <name> is an executable on PATH.
bool ToolOnPath(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name.find('/') != std::string::npos) {
        return ::access(name.c_str(), X_OK) == 0;
    }
    const char* raw = ::getenv("PATH");
    if (raw == nullptr) {
        return false;
    }
    const std::string path(raw);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t colon = path.find(':', start);
        const std::string dir =
            colon == std::string::npos ? path.substr(start) : path.substr(start, colon - start);
        if (!dir.empty()) {
            std::string candidate = dir;
            candidate += '/';
            candidate += name;
            if (::access(candidate.c_str(), X_OK) == 0) {
                return true;
            }
        }
        if (colon == std::string::npos) {
            break;
        }
        start = colon + 1;
    }
    return false;
}

// ---------------------------------------------------------------- running a shell string

struct Executed {
    int exit_code = 0;
    bool timed_out = false;
    std::string output;
};

void AppendErrno(std::string* text, const char* what) {
    *text += what;
    *text += ": ";
    *text += std::strerror(errno);
    *text += "\n";
}

// /bin/sh -c <cmd> with stdout+stderr captured together (SPEC.md §4's output:<regex> is a
// statement about the combined output) and a timeout measured from the fork.
Executed RunShell(const std::string& command, long timeout_seconds) {
    Executed executed;
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        executed.exit_code = 127;
        AppendErrno(&executed.output, "cannot create a pipe");
        return executed;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        executed.exit_code = 127;
        AppendErrno(&executed.output, "cannot fork");
        return executed;
    }
    if (pid == 0) {
        // The child: in its own process group, so the timeout kill reaches the whole tree.
        ::setpgid(0, 0);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        const char* message = "kit-ci: cannot run /bin/sh\n";
        const ssize_t written = ::write(STDERR_FILENO, message, std::strlen(message));
        static_cast<void>(written);
        _exit(127);
    }
    // Both sides set the group: whichever gets there first, the group exists before any
    // signal is sent, and a failing setpgid here only means the child already did it.
    ::setpgid(pid, pid);
    ::close(pipe_fds[1]);
    const int flags = ::fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    const auto started = std::chrono::steady_clock::now();
    int status = 0;
    bool killed = false;
    for (;;) {
        int child_status = 0;
        if (::waitpid(pid, &child_status, WNOHANG) == pid) {
            status = child_status;
            break;
        }
        bool read_something = false;
        for (;;) {
            char buffer[4096];
            const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            executed.output.append(buffer, static_cast<std::size_t>(count));
            read_something = true;
        }
        if (read_something) {
            continue;
        }
        if (!killed && timeout_seconds > 0 &&
            SecondsSince(started) > static_cast<double>(timeout_seconds)) {
            ::kill(-pid, SIGKILL);
            killed = true;
            executed.timed_out = true;
        }
        // Nothing to read and the stage is still running: wait a moment rather than spin.
        struct pollfd waiting{};
        waiting.fd = pipe_fds[0];
        waiting.events = POLLIN;
        ::poll(&waiting, 1, kPollMillis);
    }
    // Drain what the pipe still holds: the stage is gone, so nothing more arrives.
    for (;;) {
        char buffer[4096];
        const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        executed.output.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(pipe_fds[0]);

    if (WIFEXITED(status)) {
        executed.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        executed.exit_code = 128 + WTERMSIG(status);
        executed.output += "killed by signal " + std::to_string(WTERMSIG(status)) + "\n";
    }
    if (executed.timed_out) {
        executed.output += "timed out after " + std::to_string(timeout_seconds) + "s\n";
    }
    return executed;
}

// ---------------------------------------------------------------- fail_on (SPEC.md §4)

enum class FailOn : std::uint8_t { kNonzero, kOutputRegex, kExitCode };

struct FailRule {
    FailOn kind = FailOn::kNonzero;
    int exit_code = 0;
    std::string pattern;
    std::regex regex;
};

bool MakeFailRule(const Stage& stage, FailRule* rule, std::string* fault) {
    if (stage.fail_on.rfind("output:", 0) == 0) {
        rule->kind = FailOn::kOutputRegex;
        rule->pattern = stage.fail_on.substr(7);
        try {
            rule->regex = std::regex(rule->pattern, std::regex::ECMAScript | std::regex::multiline);
        } catch (const std::regex_error&) {
            // Not a config the runner can honour, and not one the parser could see.
            *fault = "fail_on = \"" + stage.fail_on + "\" is not a valid regex in [stage." +
                     stage.name + "]";
            return false;
        }
        return true;
    }
    if (stage.fail_on.rfind("exit:", 0) == 0) {
        rule->kind = FailOn::kExitCode;
        rule->exit_code = std::atoi(stage.fail_on.substr(5).c_str());
        return true;
    }
    return true;  // "nonzero"
}

// The reason a stage failed, or an empty string when it passed (SPEC.md §4). A non-zero
// exit fails under every fail_on: a stage that crashed must never read as green, which is
// what "combined implicitly with nonzero" means for exit:<n>.
std::string FailureReason(const FailRule& rule, int exit_code, const std::string& output) {
    if (rule.kind == FailOn::kOutputRegex && std::regex_search(output, rule.regex)) {
        return "output matched /" + rule.pattern + "/";
    }
    if (rule.kind == FailOn::kExitCode && exit_code == rule.exit_code) {
        return "exit " + std::to_string(exit_code) +
               " (fail_on = \"exit:" + std::to_string(rule.exit_code) + "\")";
    }
    return exit_code == 0 ? std::string() : "exit " + std::to_string(exit_code);
}

// ---------------------------------------------------------------- the vocabulary the parser cannot
// check

bool CheckWhen(const Stage& stage, std::string* fault) {
    if (stage.when.empty()) {
        return true;
    }
    if (stage.when.rfind("tool:", 0) == 0 && stage.when.size() > 5) {
        return true;
    }
    *fault = "when = \"" + stage.when + "\" is not \"tool:<name>\" in [stage." + stage.name + "]";
    return false;
}

bool CheckSummary(const Stage& stage, std::string* fault) {
    const std::string& value = stage.summary;
    if (value.rfind("head:", 0) == 0 && value.size() > 5) {
        bool digits = true;
        for (std::size_t i = 5; i < value.size(); ++i) {
            if (value[i] < '0' || value[i] > '9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            return true;
        }
    }
    *fault = "summary = \"" + value + "\" is not \"head:<n>\" in [stage." + stage.name + "]";
    return false;
}

int SummaryLineCount(const std::string& summary) {
    if (summary.rfind("head:", 0) != 0) {
        return 40;
    }
    return std::atoi(summary.substr(5).c_str());
}

// ---------------------------------------------------------------- one stage

StageResult RunStage(const Stage& stage, const FailRule& rule, const RunOptions& options) {
    StageResult result;
    result.name = stage.name;
    result.cmd = stage.cmd;
    result.summary_lines = SummaryLineCount(stage.summary);

    if (options.strict && Trim(stage.cmd).empty()) {
        // SPEC.md §2's --strict, and QUESTIONS.md Q10: a stage that checks nothing is a
        // gate that lies, and the frozen vocabulary has no other meaning for strictness.
        result.status = StageStatus::kFailed;
        result.fail_reason =
            "empty cmd (--strict: a stage that checks nothing is a gate that lies)";
        return result;
    }
    if (!stage.files.empty() && options.changed &&
        !AnyChangedFileMatches(stage.files, options.changed_files)) {
        result.status = StageStatus::kSkipped;
        result.skip_reason = "no changed file matches '" + stage.files + "'";
        return result;
    }
    if (!stage.when.empty() && !ToolOnPath(stage.when.substr(5))) {
        result.status = StageStatus::kSkipped;
        result.skip_reason = "tool '" + stage.when.substr(5) + "' is not on PATH";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    const Executed executed = RunShell(stage.cmd, stage.timeout);
    result.seconds = SecondsSince(started);
    result.exit_code = executed.exit_code;
    result.timed_out = executed.timed_out;
    result.output = executed.output;

    const std::string reason =
        executed.timed_out ? std::string("timed out after " + std::to_string(stage.timeout) + "s")
                           : FailureReason(rule, executed.exit_code, executed.output);
    if (reason.empty()) {
        result.status = StageStatus::kPassed;
    } else {
        result.status = StageStatus::kFailed;
        result.fail_reason = reason;
    }
    return result;
}

std::string Verdict(bool passed, int passed_count, int failed_count, int skipped_count,
                    const std::vector<std::string>& culprits) {
    std::string text = passed ? "GATE PASSED — " : "GATE FAILED — ";
    text += std::to_string(passed_count) + " passed, " + std::to_string(failed_count) + " failed";
    if (!culprits.empty()) {
        text += " (";
        for (std::size_t i = 0; i < culprits.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += culprits[i];
        }
        text += ")";
    }
    text += ", " + std::to_string(skipped_count) + " skipped";
    return text;
}

// ---------------------------------------------------------------- git

std::string ShellQuote(const std::string& text) {
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string RunCapture(const std::string& command, int* status) {
    std::string output;
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        *status = -1;
        return output;
    }
    char buffer[4096];
    std::size_t count = 0;
    while ((count = ::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, count);
    }
    const int raw = ::pclose(pipe);
    *status = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
    return output;
}

std::string MergeBase(const std::string& quoted_dir, const std::string& reference, int* status) {
    return Trim(RunCapture(
        "git -C " + quoted_dir + " merge-base HEAD " + reference + " 2>/dev/null", status));
}

}  // namespace

bool glob_matches(const std::string& pattern, const std::string& path) {
    return MatchPattern(pattern.c_str(), path.c_str());
}

std::string head_lines(const std::string& text, int lines) {
    if (lines <= 0) {
        return {};
    }
    std::size_t pos = 0;
    int seen = 0;
    while (pos < text.size()) {
        const std::size_t newline = text.find('\n', pos);
        if (newline == std::string::npos) {
            pos = text.size();
            break;
        }
        ++seen;
        pos = newline + 1;
        if (seen >= lines) {
            break;
        }
    }
    return text.substr(0, pos);
}

ChangedFiles changed_files_from_git(const std::string& repo_dir) {
    ChangedFiles answer;
    const std::string quoted_dir = ShellQuote(repo_dir);
    int status = 0;
    std::string base = MergeBase(quoted_dir, "main", &status);
    if (status != 0 || base.empty()) {
        base = MergeBase(quoted_dir, "origin/main", &status);
    }
    if (status != 0 || base.empty()) {
        // Not a repository, or no main to compare against. Skipping every globbed stage
        // would be the one answer that lies, so the caller is told and runs unscoped.
        answer.note = "no merge base with main in " + repo_dir + " — every stage runs unscoped";
        return answer;
    }
    const std::string diff = RunCapture(
        "git -C " + quoted_dir + " diff --name-only " + ShellQuote(base) + " 2>/dev/null", &status);
    if (status != 0) {
        answer.note = "git diff failed in " + repo_dir + " — every stage runs unscoped";
        return answer;
    }
    answer.known = true;
    answer.files = SplitLines(diff);
    return answer;
}

RunResult run_tier(const Config& config, const Tier& tier, const RunOptions& options) {
    RunResult result;
    const std::vector<std::string> names = resolved_stages(config, tier);

    // Exit 2 means "no stage ran" (SPEC.md §2), so a tier with nothing in it is exit 2 and
    // never a pass: a gate that checks nothing is a gate that lies.
    if (names.empty()) {
        result.exit_code = 2;
        result.error = "no stages configured for tier '" + tier.name + "'";
        result.verdict = "GATE FAILED — no stages configured for tier '" + tier.name + "'";
        return result;
    }

    // Every fault the parser cannot see is found BEFORE the first stage runs, so exit 2
    // keeps its meaning: nothing ran, and the message carries a line number.
    std::map<std::string, FailRule> rules;
    for (const std::string& name : names) {
        const auto it = config.stages.find(name);
        if (it == config.stages.end()) {
            result.exit_code = 2;
            result.error =
                "[tier." + tier.name + "] names stage '" + name + "' which is not declared";
            return result;
        }
        const Stage& stage = it->second;
        std::string fault;
        FailRule rule;
        const bool ok = MakeFailRule(stage, &rule, &fault) && CheckWhen(stage, &fault) &&
                        CheckSummary(stage, &fault);
        if (!ok) {
            result.exit_code = 2;
            result.error = fault + " (line " + std::to_string(stage.line) + ")";
            return result;
        }
        rules[name] = rule;
    }

    int passed = 0;
    int failed = 0;
    int skipped = 0;
    std::vector<std::string> culprits;
    for (const std::string& name : names) {
        const StageResult stage = RunStage(config.stages.at(name), rules.at(name), options);
        switch (stage.status) {
            case StageStatus::kPassed:
                ++passed;
                break;
            case StageStatus::kFailed:
                ++failed;
                culprits.push_back(name);
                break;
            case StageStatus::kSkipped:
                ++skipped;
                break;
        }
        result.stages.push_back(stage);
    }

    result.exit_code = failed > 0 ? 1 : 0;
    result.verdict = Verdict(failed == 0, passed, failed, skipped, culprits);
    return result;
}

}  // namespace kitci
