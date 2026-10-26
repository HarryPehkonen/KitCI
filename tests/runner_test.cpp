// Stage B's frozen tests (SPEC.md §8, "Runner") plus the readings QUESTIONS.md records.
//
// The runner library is driven directly: what is under test is stage semantics. The exit
// codes a process hands to a hook are CliTest's business.
#include "kitci/runner.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "kitci/parser.hpp"

namespace {

kitci::Config ConfigFrom(const std::string& text) {
    const kitci::ParseResult parsed = kitci::parse_gate_toml(text);
    EXPECT_TRUE(parsed.ok) << parsed.error.message;
    return parsed.ok ? parsed.config : kitci::Config{};
}

kitci::Tier TierNamed(const kitci::Config& config, const std::string& name) {
    const auto it = config.tiers.find(name);
    return it == config.tiers.end() ? kitci::Tier{} : it->second;
}

// The trap SPEC.md §4's diagnostics block exists for, in one command: 55 lines of build-tool
// progress, then the diagnostic — line 56 of the stage's output, past the default
// `summary = "head:40"`. The compiler's own answer is one line.
std::string NoisyStageConfig() {
    return R"toml([tier.full]
stages = ["noisy"]

[stage.noisy]
cmd = "i=0; while [ $i -lt 55 ]; do i=$((i+1)); echo \"[ $i%] Building CXX object src/unit_$i.cpp.o\"; done; echo 'src/pair.cpp:8:18: error: comparison of integers of different signs' >&2; exit 1"
)toml";
}

}  // namespace

TEST(RunnerTest, RunsStageAndPassesOnZeroExit) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["ok"]

[stage.ok]
cmd = "true"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    EXPECT_EQ(result.exit_code, 0) << result.verdict;
    ASSERT_EQ(result.stages.size(), 1U);
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kPassed);
    EXPECT_EQ(result.stages[0].exit_code, 0);
    EXPECT_NE(result.verdict.find("GATE PASSED"), std::string::npos) << result.verdict;
}

TEST(RunnerTest, FailsAndKeepsGoing) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["bad", "worse"]

[stage.bad]
cmd = "exit 3"

[stage.worse]
cmd = "exit 4"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    // Both stages ran: the first failure did not stop the run (SPEC.md §4).
    ASSERT_EQ(result.stages.size(), 2U);
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(result.stages[1].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(result.stages[1].exit_code, 4) << result.stages[1].output;
    EXPECT_EQ(result.exit_code, 1);
    // The verdict names every culprit.
    EXPECT_NE(result.verdict.find("GATE FAILED"), std::string::npos) << result.verdict;
    EXPECT_NE(result.verdict.find("bad"), std::string::npos) << result.verdict;
    EXPECT_NE(result.verdict.find("worse"), std::string::npos) << result.verdict;
}

TEST(RunnerTest, ExitFiveIsFailureWithExitFailOn) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["five", "zero", "other"]

[stage.five]
cmd = "exit 5"
fail_on = "exit:5"

[stage.zero]
cmd = "exit 0"
fail_on = "exit:5"

[stage.other]
cmd = "exit 7"
fail_on = "exit:5"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    ASSERT_EQ(result.stages.size(), 3U);
    // exit:5 fails on exit 5 and, implicitly, on any other non-zero exit (SPEC.md §4).
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(result.stages[1].status, kitci::StageStatus::kPassed);
    EXPECT_EQ(result.stages[2].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(result.exit_code, 1);
}

TEST(RunnerTest, OutputRegexFailOnMatches) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["vacuous", "buried", "fine"]

[stage.vacuous]
cmd = "echo no tests ran"
fail_on = "output:^no tests ran"

[stage.buried]
cmd = "echo ok; echo no tests ran"
fail_on = "output:^no tests ran"

[stage.fine]
cmd = "echo out; echo err >&2"
fail_on = "output:^no tests ran"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    ASSERT_EQ(result.stages.size(), 3U);
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kFailed);
    // "^" is a line start, not only a buffer start: pytest prints its line after other
    // output, and the pattern still has to catch it (QUESTIONS.md Q12).
    EXPECT_EQ(result.stages[1].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(result.stages[2].status, kitci::StageStatus::kPassed);
    // Output is captured whole, stdout and stderr together (SPEC.md §4).
    EXPECT_NE(result.stages[2].output.find("out"), std::string::npos) << result.stages[2].output;
    EXPECT_NE(result.stages[2].output.find("err"), std::string::npos) << result.stages[2].output;
    EXPECT_EQ(result.exit_code, 1);
}

TEST(RunnerTest, TimeoutKillsStage) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["slow"]

[stage.slow]
cmd = "sleep 5"
timeout = 1
)toml");
    const auto started = std::chrono::steady_clock::now();
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    ASSERT_EQ(result.stages.size(), 1U);
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kFailed);
    EXPECT_TRUE(result.stages[0].timed_out);
    EXPECT_NE(result.stages[0].output.find("timed out after 1s"), std::string::npos)
        << result.stages[0].output;
    EXPECT_EQ(result.exit_code, 1);
    // The stage was killed, not waited out.
    EXPECT_LT(elapsed, 4.0) << "the run took " << elapsed << "s";
}

TEST(RunnerTest, ToolMissingSkipsStage) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["absent", "present"]

[stage.absent]
cmd = "exit 1"
when = "tool:kitci-no-such-tool-2026"

[stage.present]
cmd = "true"
when = "tool:sh"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    ASSERT_EQ(result.stages.size(), 2U);
    // Skipped, not failed — and that stage would have failed had it run.
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kSkipped);
    EXPECT_NE(result.stages[0].skip_reason.find("kitci-no-such-tool-2026"), std::string::npos)
        << result.stages[0].skip_reason;
    EXPECT_EQ(result.stages[1].status, kitci::StageStatus::kPassed);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.verdict.find("GATE PASSED"), std::string::npos) << result.verdict;
    EXPECT_NE(result.verdict.find("1 skipped"), std::string::npos) << result.verdict;
}

TEST(RunnerTest, ChangedGlobSkipsStage) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["py"]

[stage.py]
cmd = "exit 1"
files = "*.py"
)toml");
    kitci::RunOptions options;
    options.changed = true;
    options.changed_files = {"docs/README.md"};
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), options);
    ASSERT_EQ(result.stages.size(), 1U);
    EXPECT_EQ(result.stages[0].status, kitci::StageStatus::kSkipped);
    EXPECT_NE(result.stages[0].skip_reason.find("*.py"), std::string::npos)
        << result.stages[0].skip_reason;
    EXPECT_EQ(result.exit_code, 0);
}

TEST(RunnerTest, ChangedGlobRunsStageOnMatch) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["py"]

[stage.py]
cmd = "true"
files = "*.py"
)toml");
    const kitci::Tier tier = TierNamed(config, "full");

    const kitci::RunOptions changed = [] {
        kitci::RunOptions options;
        options.changed = true;
        // A nested path: the documented example, files = "*.py", has to match it.
        options.changed_files = {"src/deep/module.py"};
        return options;
    }();
    const kitci::RunResult ran = kitci::run_tier(config, tier, changed);
    ASSERT_EQ(ran.stages.size(), 1U);
    EXPECT_EQ(ran.stages[0].status, kitci::StageStatus::kPassed);
    EXPECT_EQ(ran.exit_code, 0);

    // Without --changed the stage always runs, files glob or not (SPEC.md §4).
    const kitci::RunResult unscoped = kitci::run_tier(config, tier, {});
    ASSERT_EQ(unscoped.stages.size(), 1U);
    EXPECT_EQ(unscoped.stages[0].status, kitci::StageStatus::kPassed);
}

TEST(RunnerTest, GlobCrossesPathSeparators) {
    EXPECT_TRUE(kitci::glob_matches("*.py", "src/deep/module.py"));
    EXPECT_TRUE(kitci::glob_matches("src/**/*.py", "src/deep/module.py"));
    EXPECT_TRUE(kitci::glob_matches("src/?eep/module.py", "src/deep/module.py"));
    EXPECT_TRUE(kitci::glob_matches("*", "anything/at/all"));
    EXPECT_FALSE(kitci::glob_matches("*.py", "docs/README.md"));
    EXPECT_FALSE(kitci::glob_matches("src/*.py", "docs/module.py"));
}

TEST(RunnerTest, InvalidOutputRegexIsAConfigError) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["bad"]

[stage.bad]
cmd = "true"
fail_on = "output:("
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    EXPECT_EQ(result.exit_code, 2);
    // Nothing ran, so exit 2 means what SPEC.md §2 says it means.
    EXPECT_TRUE(result.stages.empty());
    EXPECT_NE(result.error.find("fail_on"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("(line 4)"), std::string::npos) << result.error;
}

TEST(RunnerTest, UnknownWhenIsAConfigError) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["bad"]

[stage.bad]
cmd = "true"
when = "git:clean"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(result.stages.empty());
    EXPECT_NE(result.error.find("when"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("(line 4)"), std::string::npos) << result.error;
}

TEST(RunnerTest, UnknownSummaryIsAConfigError) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["bad"]

[stage.bad]
cmd = "true"
summary = "tail:5"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(result.stages.empty());
    EXPECT_NE(result.error.find("summary"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("(line 4)"), std::string::npos) << result.error;
}

TEST(RunnerTest, StrictFailsAStageWithAnEmptyCmd) {
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["empty"]

[gate]
# 2026-10-06, QUESTIONS.md Q30: an empty cmd is reachable in a Config only when the config
# itself says the no-op is deliberate -- the parser refuses it otherwise
# (ParserTest.RejectsEmptyCmd). The RUNs own strictness is what this test measures, so the
# fixture is the permissive config and RunOptions does the rest.
strict = false

[stage.empty]
cmd = ""
)toml");
    const kitci::Tier tier = TierNamed(config, "full");

    const kitci::RunResult strict = kitci::run_tier(config, tier, {});
    ASSERT_EQ(strict.stages.size(), 1U);
    EXPECT_EQ(strict.stages[0].status, kitci::StageStatus::kFailed);
    EXPECT_EQ(strict.exit_code, 1);
    EXPECT_NE(strict.stages[0].fail_reason.find("empty cmd"), std::string::npos)
        << strict.stages[0].fail_reason;

    kitci::RunOptions lenient;
    lenient.strict = false;
    const kitci::RunResult ran = kitci::run_tier(config, tier, lenient);
    ASSERT_EQ(ran.stages.size(), 1U);
    // Running an empty shell string is a no-op, and --no-strict lets it be one.
    EXPECT_EQ(ran.stages[0].status, kitci::StageStatus::kPassed);
    EXPECT_EQ(ran.exit_code, 0);
}

TEST(RunnerTest, HeadLinesLimitsTheSummary) {
    const std::string text = "one\ntwo\nthree\nfour\nfive\n";
    EXPECT_EQ(kitci::head_lines(text, 2), "one\ntwo\n");
    EXPECT_EQ(kitci::head_lines(text, 99), text);
    EXPECT_EQ(kitci::head_lines(text, 0), "");
    EXPECT_EQ(kitci::head_lines("no trailing newline", 3), "no trailing newline");

    // The knob is the stage's own `summary` key (SPEC.md §3, §4).
    const kitci::Config config = ConfigFrom(R"toml([tier.full]
stages = ["noisy"]

[stage.noisy]
cmd = "echo one; echo two; echo three; echo four; echo five"
summary = "head:2"
)toml");
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    ASSERT_EQ(result.stages.size(), 1U);
    EXPECT_EQ(result.stages[0].summary_lines, 2);
    EXPECT_EQ(kitci::head_lines(result.stages[0].output, result.stages[0].summary_lines),
              "one\ntwo\n");
}

TEST(RunnerTest, DiagnosticsAreFoundBeyondTheSummaryLines) {
    const kitci::Config config = ConfigFrom(NoisyStageConfig());
    const kitci::RunResult result = kitci::run_tier(config, TierNamed(config, "full"), {});
    ASSERT_EQ(result.stages.size(), 1U);
    const kitci::StageResult& stage = result.stages[0];
    ASSERT_EQ(stage.status, kitci::StageStatus::kFailed);

    // The trap first, so the test cannot pass by luck: the summary is 40 lines of progress and
    // the diagnostic is line 56 of the stage's output.
    const std::string shown = kitci::head_lines(stage.output, stage.summary_lines);
    EXPECT_EQ(stage.summary_lines, 40);
    EXPECT_EQ(shown.find("error:"), std::string::npos) << shown;

    // Then the answer: the payload is found wherever it sits, not only in the first `summary`
    // lines (SPEC.md §4, v1.2).
    const kitci::Diagnostics found = kitci::diagnostics_in(stage.output, shown);
    ASSERT_EQ(found.lines.size(), 1U) << found.found << " found";
    EXPECT_NE(found.lines[0].find("src/pair.cpp:8:18: error:"), std::string::npos)
        << found.lines[0];
}

TEST(RunnerTest, DiagnosticsIgnoreProseThatOnlyLooksLikeOne) {
    // Every line here has been seen in a real run of this fleet's gates.
    const std::string output =
        "pair.cpp: In function 'int checksum(const std::vector<int>&)':\n"
        "cc1plus: all warnings being treated as errors\n"
        "FAILED: build — cmake configure failed\n"
        "12:00:01: error: a clock, not a file\n"
        "==> build (-Werror, zero warnings)\n"
        "src/ok.cpp:3:1: error: comparison of integers of different signs\n";
    const kitci::Diagnostics found = kitci::diagnostics_in(output, "");
    ASSERT_EQ(found.lines.size(), 1U) << found.found << " found";
    EXPECT_NE(found.lines[0].find("src/ok.cpp:3:1: error:"), std::string::npos) << found.lines[0];
}

TEST(RunnerTest, DiagnosticsSkipLinesTheSummaryAlreadyShows) {
    const std::string output =
        "src/early.cpp:1:1: error: the summary already printed this one\n"
        "src/late.cpp:2:2: error: this one is past the summary\n";
    const std::string shown = "src/early.cpp:1:1: error: the summary already printed this one\n";
    const kitci::Diagnostics found = kitci::diagnostics_in(output, shown);
    ASSERT_EQ(found.lines.size(), 1U);
    EXPECT_NE(found.lines[0].find("src/late.cpp:2:2: error:"), std::string::npos) << found.lines[0];
    // The count is what the block's header prints, so it must not claim a line nobody will see.
    EXPECT_EQ(found.found, 1U);
}

TEST(RunnerTest, DiagnosticsAreCappedAndCounted) {
    const int cap = kitci::kDiagnosticLineCap;
    std::string output;
    for (int i = 1; i <= cap + 3; ++i) {
        output += "src/many.cpp:" + std::to_string(i) + ":1: warning: finding " +
                  std::to_string(i) + "\n";
    }
    // An exact duplicate is not a second finding: the header counts findings, not lines read.
    output += "src/many.cpp:1:1: warning: finding 1\n";

    const kitci::Diagnostics found = kitci::diagnostics_in(output, "");
    EXPECT_EQ(static_cast<int>(found.lines.size()), cap);
    EXPECT_EQ(found.found, static_cast<std::size_t>(cap + 3));
    EXPECT_NE(found.lines.front().find("finding 1"), std::string::npos) << found.lines.front();
    EXPECT_NE(found.lines.back().find("finding " + std::to_string(cap)), std::string::npos)
        << found.lines.back();
}
