#include "kitci/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

kitci::ParseResult Parse(const std::string& text) {
    return kitci::parse_gate_toml(text);
}

// The line the failing parse reported, or -1 when the parse succeeded.
int ErrorLine(const kitci::ParseResult& result) {
    return result.ok ? -1 : result.error.line;
}

const std::string& ErrorMessage(const kitci::ParseResult& result) {
    return result.error.message;
}

}  // namespace

TEST(ParserTest, ParsesMinimalConfig) {
    const kitci::ParseResult result = Parse(R"toml([stage.tests]
cmd = "ctest --output-on-failure"
)toml");
    ASSERT_TRUE(result.ok) << ErrorMessage(result);
    const kitci::Config& config = result.config;
    ASSERT_EQ(config.stage_order.size(), 1U);
    EXPECT_EQ(config.stage_order[0], "tests");
    const auto it = config.stages.find("tests");
    ASSERT_NE(it, config.stages.end());
    EXPECT_EQ(it->second.name, "tests");
    EXPECT_EQ(it->second.cmd, "ctest --output-on-failure");
    // Every optional key has its documented default (SPEC.md §3).
    EXPECT_EQ(it->second.fail_on, "nonzero");
    EXPECT_EQ(it->second.timeout, 300);
    EXPECT_EQ(it->second.summary, "head:40");
    EXPECT_TRUE(it->second.files.empty());
    EXPECT_TRUE(it->second.when.empty());
    EXPECT_TRUE(config.repo.empty());
    EXPECT_TRUE(config.tiers.empty());
}

TEST(ParserTest, ParsesEveryDocumentedKey) {
    const kitci::ParseResult result = Parse(R"toml([gate]
repo = "name"

[tier.fast]
stages = ["format", "lint", "tests"]

[tier.full]
stages = ["*"]

[stage.format]
cmd = "clang-format --dry-run --Werror ."

[stage.lint]
cmd = "ruff check ."
tier = "fast"
fail_on = "output:^no tests ran"
timeout = 120
files = "*.py"
when = "tool:ruff"
summary = "head:10"

[stage.tests]
cmd = "pytest -q"
)toml");
    ASSERT_TRUE(result.ok) << ErrorMessage(result);
    const kitci::Config& config = result.config;
    EXPECT_EQ(config.repo, "name");
    ASSERT_EQ(config.stage_order.size(), 3U);
    EXPECT_EQ(config.stage_order[0], "format");
    EXPECT_EQ(config.stage_order[1], "lint");
    EXPECT_EQ(config.stage_order[2], "tests");
    ASSERT_EQ(config.tier_order.size(), 2U);
    EXPECT_EQ(config.tier_order[0], "fast");
    EXPECT_EQ(config.tier_order[1], "full");

    const kitci::Stage& lint = config.stages.at("lint");
    EXPECT_EQ(lint.cmd, "ruff check .");
    EXPECT_EQ(lint.fail_on, "output:^no tests ran");
    EXPECT_EQ(lint.timeout, 120);
    EXPECT_EQ(lint.files, "*.py");
    EXPECT_EQ(lint.when, "tool:ruff");
    EXPECT_EQ(lint.summary, "head:10");

    // "fast": the list as written.
    EXPECT_EQ(kitci::resolved_stages(config, config.tiers.at("fast")),
              (std::vector<std::string>{"format", "lint", "tests"}));
    // "full": "*" means every declared stage, in declaration order.
    EXPECT_EQ(kitci::resolved_stages(config, config.tiers.at("full")),
              (std::vector<std::string>{"format", "lint", "tests"}));
}

TEST(ParserTest, TierKeyAddsStageToTier) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."
tier = "fast"

[stage.tests]
cmd = "pytest -q"

[tier.fast]
stages = ["tests"]
)toml");
    ASSERT_TRUE(result.ok) << ErrorMessage(result);
    const kitci::Config& config = result.config;
    EXPECT_EQ(kitci::resolved_stages(config, config.tiers.at("fast")),
              (std::vector<std::string>{"tests", "lint"}));
}

TEST(ParserTest, RejectsUnknownKey) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."
colur = "red"
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 3);
    EXPECT_NE(ErrorMessage(result).find("colur"), std::string::npos) << ErrorMessage(result);
    EXPECT_NE(ErrorMessage(result).find("line 3"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsStageWithoutCmd) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
files = "*.py"
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 1);
    EXPECT_NE(ErrorMessage(result).find("cmd"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsUnknownStageInTier) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."

[tier.fast]
stages = ["lint", "fmt"]
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 5);
    EXPECT_NE(ErrorMessage(result).find("fmt"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsTierNotDeclared) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."
tier = "fast"
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 3);
    EXPECT_NE(ErrorMessage(result).find("fast"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsBadFailOn) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."
fail_on = "sometimes"
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 3);
    EXPECT_NE(ErrorMessage(result).find("fail_on"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsBadTimeout) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."
timeout = -1
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 3);
    EXPECT_NE(ErrorMessage(result).find("timeout"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsDuplicateTable) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."

[stage.lint]
cmd = "ruff format ."
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 4);
    EXPECT_NE(ErrorMessage(result).find("lint"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsWrongValueType) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = 5
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 2);
    EXPECT_NE(ErrorMessage(result).find("cmd"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsTierWithoutStages) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."

[tier.fast]
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 4);
    EXPECT_NE(ErrorMessage(result).find("stages"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, RejectsStageTwiceInTier) {
    const kitci::ParseResult result = Parse(R"toml([stage.lint]
cmd = "ruff check ."

[tier.fast]
stages = ["lint", "lint"]
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 5);
    EXPECT_NE(ErrorMessage(result).find("lint"), std::string::npos) << ErrorMessage(result);
}

TEST(ParserTest, ReportsTheRightLineNumber) {
    const kitci::ParseResult result = Parse(R"toml([gate]
repo = "kit-ci"

[stage.lint]
cmd = "ruff check ."
timeout = -1

[tier.fast]
stages = ["lint"]
)toml");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ErrorLine(result), 6);
    EXPECT_NE(ErrorMessage(result).find("(line 6)"), std::string::npos) << ErrorMessage(result);
}
