#include <gtest/gtest.h>
#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kBinary = KITCI_BINARY;
constexpr const char* kFixtures = KITCI_FIXTURES;

// git needs an identity to commit, and a developer's ~/.gitconfig must not decide whether
// this test can run.
constexpr const char* kGitIdentity =
    "-c user.email=kit-ci@example.invalid -c user.name=kit-ci -c commit.gpgsign=false";

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

// Shell out to the real binary: the command line, stdout and the exit code are contracts,
// and a test that called the library instead would not notice any of them.
CommandResult RunCommandIn(const std::string& working_dir, const std::string& arguments) {
    const std::string command =
        "cd \"" + working_dir + "\" && \"" + std::string(kBinary) + "\" " + arguments + " 2>&1";
    CommandResult result;
    std::array<char, 4096> buffer{};
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.exit_code = -1;
        return result;
    }
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = ::pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

CommandResult RunCommand(const std::string& arguments) {
    return RunCommandIn(".", arguments);
}

// A scratch directory; the caller removes it.
std::string MakeTempDir(const std::string& label) {
    std::string pattern = (std::filesystem::temp_directory_path() / (label + "XXXXXX")).string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* made = ::mkdtemp(buffer.data());
    return made == nullptr ? std::string() : std::string(made);
}

bool WriteFile(const std::string& path, const std::string& text) {
    const std::filesystem::path target(path);
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << text;
    return out.good();
}

// Run a shell command inside a directory; the exit code, or -1 when it could not run.
int RunShellInDir(const std::string& dir, const std::string& command) {
    const std::string full = "cd \"" + dir + "\" && " + command + " >/dev/null 2>&1";
    FILE* pipe = ::popen(full.c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }
    const int status = ::pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

struct Listing {
    std::vector<std::string> stages;
    std::vector<std::pair<std::string, std::vector<std::string>>> tiers;
};

// Parse the --list output by its structure, not by matching the whole text: the point of
// the test is that the answer is readable without reading the source.
bool ParseListing(const std::string& output, Listing* listing) {
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) {
            return false;
        }
        const std::string head = line.substr(0, space);
        const std::string rest = line.substr(space + 1);
        if (head == "stages:") {
            if (!listing->stages.empty()) {
                return false;
            }
            listing->stages = SplitWords(rest);
        } else if (head == "tier") {
            const std::size_t colon = rest.find(':');
            if (colon == std::string::npos) {
                return false;
            }
            const std::string name = rest.substr(0, colon);
            const std::string names = rest.substr(colon + 1);
            listing->tiers.emplace_back(name, SplitWords(names));
        } else {
            return false;
        }
    }
    return !listing->stages.empty();
}

// The config every graph test feeds the binary: two tiers over three stages.
std::string ListGate() {
    return std::string("--gate ") + kFixtures + "/list.toml";
}

}  // namespace

TEST(CliTest, ListAnswersWithoutReadingSource) {
    const CommandResult result =
        RunCommand(std::string("--gate ") + kFixtures + "/list.toml --list");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    Listing listing;
    ASSERT_TRUE(ParseListing(result.output, &listing)) << result.output;
    EXPECT_EQ(listing.stages, (std::vector<std::string>{"format", "lint", "tests"}));
    ASSERT_EQ(listing.tiers.size(), 2U);
    EXPECT_EQ(listing.tiers[0].first, "fast");
    EXPECT_EQ(listing.tiers[0].second, (std::vector<std::string>{"format", "lint", "tests"}));
    EXPECT_EQ(listing.tiers[1].first, "full");
    EXPECT_EQ(listing.tiers[1].second, (std::vector<std::string>{"format", "lint", "tests"}));
}

TEST(CliTest, ListOnInvalidConfigExitsTwo) {
    const CommandResult result =
        RunCommand(std::string("--gate ") + kFixtures + "/invalid.toml --list");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("colur"), std::string::npos) << result.output;
}

TEST(CliTest, RunPassesExitsZero) {
    const CommandResult result = RunCommand(std::string("--gate ") + kFixtures + "/run_pass.toml");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("GATE PASSED"), std::string::npos) << result.output;
}

TEST(CliTest, RunFailureExitsOneAndNamesTheCulprits) {
    const CommandResult result = RunCommand(std::string("--gate ") + kFixtures + "/run_fail.toml");
    EXPECT_EQ(result.exit_code, 1) << result.output;
    EXPECT_NE(result.output.find("GATE FAILED"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("bad"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("worse"), std::string::npos) << result.output;
    // SPEC.md §4: a failed stage prints its whole command so the human can re-run it.
    EXPECT_NE(result.output.find("echo kitci-command-marker; exit 3"), std::string::npos)
        << result.output;
}

TEST(CliTest, ZeroStagesExitsTwo) {
    const CommandResult result =
        RunCommand(std::string("--gate ") + kFixtures + "/zero_stages.toml");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    // Exit 2 must mean "nothing was checked", not "the run is unimplemented": the answer
    // names the tier that resolved to no stages.
    EXPECT_NE(result.output.find("no stages"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("full"), std::string::npos) << result.output;
    // A gate that checks nothing is a gate that lies: it must not read as green.
    EXPECT_EQ(result.output.find("GATE PASSED"), std::string::npos) << result.output;
}

TEST(CliTest, TierSelectsTheStageSet) {
    const std::string gate = std::string("--gate ") + kFixtures + "/run_tiers.toml";
    const CommandResult fast = RunCommand(gate + " --tier fast");
    EXPECT_EQ(fast.exit_code, 0) << fast.output;
    EXPECT_NE(fast.output.find("GATE PASSED"), std::string::npos) << fast.output;
    EXPECT_EQ(fast.output.find("slow"), std::string::npos) << fast.output;

    const CommandResult full = RunCommand(gate + " --tier full");
    EXPECT_EQ(full.exit_code, 1) << full.output;
    EXPECT_NE(full.output.find("slow"), std::string::npos) << full.output;

    // A tier that is not declared is a config error, not an empty run that passes.
    const CommandResult missing = RunCommand(gate + " --tier nope");
    EXPECT_EQ(missing.exit_code, 2) << missing.output;
}

TEST(CliTest, ChangedScopesAgainstGitMain) {
    const std::string dir = MakeTempDir("kitci-changed-");
    ASSERT_FALSE(dir.empty());
    ASSERT_TRUE(WriteFile(dir + "/gate.toml", R"toml([tier.full]
stages = ["py"]

[stage.py]
cmd = "exit 1"
files = "*.py"
)toml"));
    ASSERT_TRUE(WriteFile(dir + "/notes.md", "notes\n"));
    ASSERT_TRUE(WriteFile(dir + "/src/module.py", "x = 1\n"));
    ASSERT_EQ(RunShellInDir(dir, "git init -q -b main ."), 0);
    ASSERT_EQ(RunShellInDir(dir, "git add -A"), 0);
    ASSERT_EQ(RunShellInDir(dir, std::string("git ") + kGitIdentity + " commit -q -m initial"), 0);

    // Only a file the glob does not name changed: the stage is skipped, and the run passes.
    ASSERT_TRUE(WriteFile(dir + "/notes.md", "notes, changed\n"));
    const CommandResult skipped = RunCommandIn(dir, "--changed --gate gate.toml");
    EXPECT_EQ(skipped.exit_code, 0) << skipped.output;
    EXPECT_NE(skipped.output.find("GATE PASSED"), std::string::npos) << skipped.output;

    // A file the glob names changed: the stage runs and its failure is the run's.
    ASSERT_TRUE(WriteFile(dir + "/src/module.py", "x = 2\n"));
    const CommandResult ran = RunCommandIn(dir, "--changed --gate gate.toml");
    EXPECT_EQ(ran.exit_code, 1) << ran.output;
    EXPECT_NE(ran.output.find("GATE FAILED"), std::string::npos) << ran.output;

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

TEST(CliTest, ChangedWithoutGitRunsUnscoped) {
    const std::string dir = MakeTempDir("kitci-nogit-");
    ASSERT_FALSE(dir.empty());
    ASSERT_TRUE(WriteFile(dir + "/gate.toml", R"toml([tier.full]
stages = ["py"]

[stage.py]
cmd = "true"
files = "*.py"
)toml"));
    // No repository here: the scoping cannot be answered, so nothing is silently skipped —
    // every stage runs and the run says why.
    const CommandResult result = RunCommandIn(dir, "--changed --gate gate.toml");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("GATE PASSED"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("--changed"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("py"), std::string::npos) << result.output;

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

TEST(CliTest, GraphDefaultsToMermaid) {
    const CommandResult bare = RunCommand(ListGate() + " --graph");
    const CommandResult named = RunCommand(ListGate() + " --graph --format mermaid");
    EXPECT_EQ(bare.exit_code, 0) << bare.output;
    EXPECT_EQ(named.exit_code, 0) << named.output;
    // The default format is mermaid, and it is the same output either way.
    EXPECT_EQ(bare.output, named.output);
    EXPECT_NE(bare.output.find("flowchart"), std::string::npos) << bare.output;
    for (const char* stage : {"format", "lint", "tests"}) {
        EXPECT_NE(bare.output.find(stage), std::string::npos) << bare.output;
    }
}

TEST(CliTest, GraphHtmlShorthandMatchesTheFormatFlag) {
    const CommandResult shorthand = RunCommand(ListGate() + " --graph-html");
    const CommandResult explicit_format = RunCommand(ListGate() + " --graph --format html");
    EXPECT_EQ(shorthand.exit_code, 0) << shorthand.output;
    EXPECT_NE(shorthand.output.find("<!DOCTYPE html>"), std::string::npos) << shorthand.output;
    EXPECT_EQ(shorthand.output, explicit_format.output);
}

TEST(CliTest, GraphEmitsDotWhenAsked) {
    const CommandResult result = RunCommand(ListGate() + " --graph --format dot");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_EQ(result.output.rfind("digraph", 0), 0U) << result.output;
    EXPECT_NE(result.output.find("subgraph \"cluster_tier0\""), std::string::npos) << result.output;
}

TEST(CliTest, GraphRejectsUnknownFormatWithExitTwo) {
    const CommandResult result = RunCommand(ListGate() + " --graph --format svg");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("svg"), std::string::npos) << result.output;
    // The message has to say what the answer is, not just what it is not.
    EXPECT_NE(result.output.find("mermaid"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("dot"), std::string::npos) << result.output;
}

TEST(CliTest, GraphMissingFormatValueExitsTwo) {
    const CommandResult result = RunCommand(ListGate() + " --graph --format");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("--format"), std::string::npos) << result.output;
}

TEST(CliTest, FormatWithoutGraphExitsTwo) {
    // A flag that would do nothing is not accepted quietly: --format only means something
    // with --graph.
    const CommandResult result = RunCommand(ListGate() + " --format dot");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("--format"), std::string::npos) << result.output;
}

TEST(CliTest, GraphNotesTheRunOptionsItCannotHonour) {
    // The graph is a function of the whole config, so --tier cannot narrow it. Saying so
    // beats looking like it did something.
    const CommandResult result = RunCommand(ListGate() + " --graph --tier fast");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("flowchart"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("--tier"), std::string::npos) << result.output;
}

TEST(CliTest, ListAndGraphTogetherExitTwo) {
    const CommandResult result = RunCommand(ListGate() + " --list --graph");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("--list"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("--graph"), std::string::npos) << result.output;
}

TEST(CliTest, GraphOnInvalidConfigExitsTwo) {
    const CommandResult result =
        RunCommand(std::string("--gate ") + kFixtures + "/invalid.toml --graph");
    EXPECT_EQ(result.exit_code, 2) << result.output;
    EXPECT_NE(result.output.find("colur"), std::string::npos) << result.output;
}

TEST(CliTest, GraphOnAConfigWithNoStagesStillAnswers) {
    // Exit 2 means "nothing ran", and that is a RUN's answer (SPEC.md §2). --graph answered:
    // it rendered the (empty) flow, which is the truth about a config with no stages.
    const CommandResult result =
        RunCommand(std::string("--gate ") + kFixtures + "/zero_stages.toml --graph");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("flowchart"), std::string::npos) << result.output;
}

TEST(CliTest, AstRejectsOrEscapesNothingNew) {
    // A config that does not parse is the PARSER's answer, not --ast's: exit 2, the parser's
    // own message, and no partial document — an --ast that invented its own rejection, or
    // printed half a document, would be reading the config a second time.
    const CommandResult bad =
        RunCommand(std::string("--gate ") + kFixtures + "/invalid.toml --ast");
    EXPECT_EQ(bad.exit_code, 2) << bad.output;
    EXPECT_NE(bad.output.find("colur"), std::string::npos) << bad.output;
    EXPECT_EQ(bad.output.find('{'), std::string::npos) << bad.output;

    // And it refuses nothing the parser accepted: every valid config renders.
    const CommandResult good = RunCommand(ListGate() + " --ast");
    EXPECT_EQ(good.exit_code, 0) << good.output;
    EXPECT_EQ(good.output.rfind("{\n", 0), 0U) << good.output;
    EXPECT_EQ(good.output.back(), '\n');
    EXPECT_NE(good.output.find("\"stages\""), std::string::npos) << good.output;
}

TEST(CliTest, AstIsOneOutputAtATime) {
    // --ast, --list and --graph are three answers about one config, and two of them cannot
    // both be the output. Like --list --graph, asking for two is exit 2, and the message
    // names the flags so the fix is obvious.
    for (const std::string& flags : {std::string("--ast --list"), std::string("--ast --graph"),
                                     std::string("--ast --graph-html")}) {
        const CommandResult result = RunCommand(ListGate() + " " + flags);
        EXPECT_EQ(result.exit_code, 2) << flags << ": " << result.output;
        EXPECT_NE(result.output.find("--ast"), std::string::npos) << result.output;
    }
}
