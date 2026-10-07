// The frozen --ast tests of SPEC.md §8 (the v1.1 amendment): byte-stability, the defaults
// materialised, every documented key present, and the escaping total over the free-text
// values the vocabulary allows.
#include "kitci/ast.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "kitci/parser.hpp"

namespace {

const char* const kMinimalToml = R"toml([tier.full]
stages = ["lint"]

[stage.lint]
cmd = "ruff check ."
)toml";

const char* const kEveryKeyToml = R"toml([gate]
repo = "full"
strict = false

[tier.fast]
stages = ["lint", "tests"]

[tier.full]
stages = ["*"]

[stage.lint]
cmd = "ruff check ."
tier = "fast"
fail_on = "output:^no tests ran"
timeout = 60
files = "*.py"
when = "tool:ruff"
summary = "head:10"

[stage.tests]
cmd = "pytest -q"
)toml";

kitci::Config Parse(const std::string& text) {
    const kitci::ParseResult result = kitci::parse_gate_toml(text);
    EXPECT_TRUE(result.ok) << result.error.message;
    return result.config;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(AstTest, AstIsStableForSameConfig) {
    const kitci::Config first = Parse(kEveryKeyToml);
    const kitci::Config second = Parse(kEveryKeyToml);
    const std::string a = kitci::ast_json(first);
    const std::string b = kitci::ast_json(second);
    // A pure function of the config: same config parsed twice, byte-identical output.
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b);
    // The shape a reader can rely on: one JSON document, newline-terminated.
    EXPECT_EQ(a.rfind("{\n", 0), 0U) << a;
    ASSERT_FALSE(a.empty());
    EXPECT_EQ(a.back(), '\n');
}

TEST(AstTest, AstMaterializesDefaults) {
    const kitci::Config config = Parse(kMinimalToml);
    const std::string ast = kitci::ast_json(config);
    // The runner's view of a stage that wrote nothing: what the gate will actually do.
    EXPECT_TRUE(Contains(ast, "\"fail_on\": \"nonzero\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"timeout\": 300")) << ast;
    EXPECT_TRUE(Contains(ast, "\"summary\": \"head:40\"")) << ast;
    // The three optional strings have no default action, so "not written" is null — which is
    // exactly how the runner reads them ("" means no glob, no tool gate).
    EXPECT_TRUE(Contains(ast, "\"tier\": null")) << ast;
    EXPECT_TRUE(Contains(ast, "\"files\": null")) << ast;
    EXPECT_TRUE(Contains(ast, "\"when\": null")) << ast;
    EXPECT_TRUE(Contains(ast, "\"repo\": null")) << ast;
    // And the one default a config never writes unless it means the non-default.
    EXPECT_TRUE(Contains(ast, "\"strict\": true")) << ast;
}

TEST(AstTest, AstCoversEveryDocumentedKey) {
    const kitci::Config config = Parse(kEveryKeyToml);
    const std::string ast = kitci::ast_json(config);
    // [gate]
    EXPECT_TRUE(Contains(ast, "\"repo\": \"full\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"strict\": false")) << ast;
    // The two structures, and the tier's own name plus its resolved stage list.
    EXPECT_TRUE(Contains(ast, "\"tiers\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"stages\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"name\": \"fast\"")) << ast;
    // Every [stage.X] attribute of the grammar, with the value the config wrote.
    EXPECT_TRUE(Contains(ast, "\"name\": \"lint\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"cmd\": \"ruff check .\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"tier\": \"fast\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"fail_on\": \"output:^no tests ran\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"timeout\": 60")) << ast;
    EXPECT_TRUE(Contains(ast, "\"files\": \"*.py\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"when\": \"tool:ruff\"")) << ast;
    EXPECT_TRUE(Contains(ast, "\"summary\": \"head:10\"")) << ast;
    // Both tiers' resolved lists are in the document: `full` is ["*"], i.e. declaration order.
    ASSERT_EQ(config.tier_order.size(), 2U);
    EXPECT_TRUE(Contains(ast, "\"lint\",\n        \"tests\"")) << ast;
}

// The freeze says the AST escapes nothing new and refuses nothing new: it renders exactly the
// configs the parser accepted, and the free-text values (cmd, files, when, summary, repo) go
// through JSON's own escaping rather than being rejected or emitted raw.
TEST(AstTest, AstEscapesQuotesAndBackslashes) {
    const kitci::ParseResult result = kitci::parse_gate_toml(R"toml([tier.full]
stages = ["weird"]

[stage.weird]
cmd = "echo \"hi\" \\ done"
)toml");
    ASSERT_TRUE(result.ok) << result.error.message;
    const std::string ast = kitci::ast_json(result.config);
    // The parsed cmd is: echo "hi" \ done  — emitted as a JSON string, so quote and backslash
    // are escaped and the document stays valid.
    EXPECT_TRUE(Contains(ast, "\"cmd\": \"echo \\\"hi\\\" \\\\ done\"")) << ast;
    // A control byte cannot appear in a parsed value except through a '\u<hex>' escape.
    EXPECT_EQ(ast.find('\t'), std::string::npos) << ast;
}
