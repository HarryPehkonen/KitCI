#include <gtest/gtest.h>
#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kBinary = KITCI_BINARY;
constexpr const char* kFixtures = KITCI_FIXTURES;

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

// Shell out to the real binary: --list is a command-line contract, and a test that called
// the library instead would not notice argv handling, stdout or the exit code.
CommandResult RunCommand(const std::string& arguments) {
    const std::string command = "\"" + std::string(kBinary) + "\" " + arguments + " 2>&1";
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
