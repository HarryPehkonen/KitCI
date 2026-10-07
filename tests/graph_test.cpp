// The frozen graph tests of SPEC.md §8 plus the stage-C amendment (--format dot):
// byte-stability per format, every stage listed, the HTML self-contained, the dot
// well-formed, and tier membership visible in the Mermaid classes and the HTML legend.
#include "kitci/graph.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "kitci/parser.hpp"

namespace {

// The SPEC.md §3 example: two tiers, three stages, and one stage joined to a tier by its
// `tier = "..."` key rather than by a tier's `stages` list.
const char* const kExampleToml = R"toml([gate]
repo = "example"

[tier.fast]
stages = ["format", "lint", "tests"]

[tier.full]
stages = ["*"]

[stage.format]
cmd = "clang-format --dry-run --Werror ."

[stage.lint]
cmd = "ruff check ."
tier = "fast"

[stage.tests]
cmd = "pytest -q"
)toml";

kitci::Config ParseExample() {
    const kitci::ParseResult result = kitci::parse_gate_toml(kExampleToml);
    EXPECT_TRUE(result.ok) << result.error.message;
    return result.config;
}

std::vector<std::string> RenderAll(const kitci::Config& config) {
    return {kitci::graph_mermaid(config), kitci::graph_html(config), kitci::graph_dot(config)};
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// The stage count and the tier count the example declares, so a test that walks them cannot
// silently pass by walking nothing.
constexpr std::size_t kStageCount = 3;
constexpr std::size_t kTierCount = 2;

}  // namespace

TEST(GraphTest, GraphIsStableForSameConfig) {
    const kitci::Config first = ParseExample();
    const kitci::Config second = ParseExample();
    const std::vector<std::string> a = RenderAll(first);
    const std::vector<std::string> b = RenderAll(second);
    ASSERT_EQ(a.size(), 3U);
    // Each format is a pure function of the config: same config parsed twice, same bytes.
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FALSE(a[i].empty()) << "format " << i << " produced nothing";
        EXPECT_EQ(a[i], b[i]) << "format " << i << " is not byte-stable";
    }
}

TEST(GraphTest, GraphListsEveryStage) {
    const kitci::Config config = ParseExample();
    ASSERT_EQ(config.stage_order.size(), kStageCount);
    const std::vector<std::string> outputs = RenderAll(config);
    for (const std::string& output : outputs) {
        EXPECT_FALSE(output.empty());
        for (const std::string& stage : config.stage_order) {
            EXPECT_TRUE(Contains(output, stage)) << "stage '" << stage << "' is missing";
        }
    }
}

TEST(GraphTest, GraphMermaidColorsTiersByClass) {
    const kitci::Config config = ParseExample();
    const std::string mermaid = kitci::graph_mermaid(config);
    EXPECT_TRUE(Contains(mermaid, "flowchart")) << mermaid;
    // SPEC.md §7: one node per stage, coloured by tier membership. In Mermaid that is a
    // classDef per tier plus a class statement naming every stage the tier runs.
    ASSERT_EQ(config.tier_order.size(), kTierCount);
    EXPECT_TRUE(Contains(mermaid, "classDef tier0 fill:#")) << mermaid;
    EXPECT_TRUE(Contains(mermaid, "classDef tier1 fill:#")) << mermaid;
    // Declaration order is format, lint, tests; tier fast runs all three.
    EXPECT_TRUE(Contains(mermaid, "class s0,s1,s2 tier0")) << mermaid;
    // One node declaration per stage, and every declaration is a node with a label.
    EXPECT_TRUE(Contains(mermaid, "s0[\"format\"]")) << mermaid;
    EXPECT_TRUE(Contains(mermaid, "s1[\"lint\"]")) << mermaid;
    EXPECT_TRUE(Contains(mermaid, "s2[\"tests\"]")) << mermaid;
    // Run order is an edge: format -> lint -> tests.
    EXPECT_TRUE(Contains(mermaid, "s0 --> s1")) << mermaid;
    EXPECT_TRUE(Contains(mermaid, "s1 --> s2")) << mermaid;
}

TEST(GraphTest, GraphHtmlIsSelfContained) {
    const std::string html = kitci::graph_html(ParseExample());
    // SPEC.md §7: opens from a file:// URL with zero network. Any absolute URL is a request
    // waiting to happen — including the SVG namespace, which an inline <svg> in an HTML5
    // document does not need.
    EXPECT_TRUE(Contains(html, "<!DOCTYPE html>")) << html;
    EXPECT_TRUE(Contains(html, "<svg")) << html;
    EXPECT_FALSE(Contains(html, "http://")) << html;
    EXPECT_FALSE(Contains(html, "https://")) << html;
    EXPECT_FALSE(Contains(html, "<script")) << html;
    // No JavaScript is also "no way to fetch something behind the reader's back".
    EXPECT_FALSE(Contains(html, "javascript:")) << html;
}

TEST(GraphTest, GraphHtmlNamesEveryStageAndTier) {
    const kitci::Config config = ParseExample();
    const std::string html = kitci::graph_html(config);
    ASSERT_EQ(config.stage_order.size(), kStageCount);
    for (const std::string& stage : config.stage_order) {
        EXPECT_TRUE(Contains(html, ">" + stage + "</text>"))
            << "no stage box for '" << stage << "'";
    }
    ASSERT_EQ(config.tier_order.size(), kTierCount);
    for (const std::string& tier : config.tier_order) {
        EXPECT_TRUE(Contains(html, "tier " + tier)) << "no legend entry for tier '" << tier << "'";
    }
    EXPECT_TRUE(Contains(html, "<svg viewBox=")) << html;
}

TEST(GraphTest, GraphDotIsWellFormed) {
    const kitci::Config config = ParseExample();
    const std::string dot = kitci::graph_dot(config);
    // Canonical GraphViz text: a digraph, one node per stage, one cluster per tier.
    EXPECT_EQ(dot.rfind("digraph", 0), 0U) << dot;
    ASSERT_EQ(config.stage_order.size(), kStageCount);
    for (const std::string& stage : config.stage_order) {
        EXPECT_TRUE(Contains(dot, "label=\"" + stage + "\""))
            << "no node for stage '" << stage << "'";
    }
    ASSERT_EQ(config.tier_order.size(), kTierCount);
    EXPECT_TRUE(Contains(dot, "subgraph \"cluster_tier0\"")) << dot;
    EXPECT_TRUE(Contains(dot, "subgraph \"cluster_tier1\"")) << dot;
    EXPECT_TRUE(Contains(dot, "label=\"tier fast\"")) << dot;
    EXPECT_TRUE(Contains(dot, "label=\"tier full\"")) << dot;
    // A stage both tiers run is declared in the first cluster that runs it, and the node
    // still names every tier it belongs to.
    EXPECT_TRUE(Contains(dot, "comment=\"tiers: fast, full\"")) << dot;
    // Run order is an edge (format=0, lint=1, tests=2).
    EXPECT_TRUE(Contains(dot, "s0 -> s1;")) << dot;
    EXPECT_TRUE(Contains(dot, "s1 -> s2;")) << dot;
    EXPECT_EQ(dot.back(), '\n');
}

TEST(GraphTest, GraphFormatNamesAreExact) {
    kitci::GraphFormat format = kitci::GraphFormat::kHtml;
    EXPECT_TRUE(kitci::parse_graph_format("mermaid", &format));
    EXPECT_EQ(format, kitci::GraphFormat::kMermaid);
    EXPECT_TRUE(kitci::parse_graph_format("html", &format));
    EXPECT_EQ(format, kitci::GraphFormat::kHtml);
    EXPECT_TRUE(kitci::parse_graph_format("dot", &format));
    EXPECT_EQ(format, kitci::GraphFormat::kDot);
    // Anything else is refused, and the caller's value is the caller's business: the CLI
    // turns a refusal into exit 2 with the three valid names.
    EXPECT_FALSE(kitci::parse_graph_format("svg", &format));
    EXPECT_FALSE(kitci::parse_graph_format("", &format));
    EXPECT_FALSE(kitci::parse_graph_format("Mermaid", &format));
}
