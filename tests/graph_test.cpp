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

// Found by the fuzz stage on 2026-10-06 (card t_5ee2ee12, the campaign that found
// `.ci-logs/fuzz-artifacts/crash-300de12c…`). The page echoes the config's repo name, and
// `repo` is a free display string, so a repo whose name spells out a URL produced
// `<p>3 stage(s), 2 tier(s) — repo kit-chttp://i …</p>` and tripped the byte-level
// self-containment check above. A page that carries no absolute URL whatever the config says
// is worth more than one that carries none for the configs we happened to try.
TEST(GraphTest, GraphHtmlHidesAUrlInTheRepoName) {
    const kitci::ParseResult result = kitci::parse_gate_toml(R"toml([gate]
repo = "kit-chttp://i"

[tier.fast]
stages = ["lint"]

[stage.lint]
cmd = "ruff check ."
)toml");
    ASSERT_TRUE(result.ok) << result.error.message;
    const std::string html = kitci::graph_html(result.config);
    EXPECT_FALSE(Contains(html, "http://")) << html;
    EXPECT_FALSE(Contains(html, "https://")) << html;
    // Still displayed, though: the scheme's colon is written as an entity, and a browser
    // renders that as ':' — the name is not mangled for the reader, only for a grep.
    EXPECT_TRUE(Contains(html, "kit-chttp&#58;//i")) << html;
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

namespace {

// The v1.1 fixture: one stage that deviates on every attribute that has a default, and one
// that deviates on none of them.
const char* const kAnnotatedToml = R"toml([gate]
repo = "annotated"

[tier.full]
stages = ["plain", "deviant"]

[stage.plain]
cmd = "true"

[stage.deviant]
cmd = "pytest -q"
fail_on = "exit:5"
timeout = 60
files = "*.py"
when = "tool:ruff"
)toml";

}  // namespace

// The v1.1 amendment (SPEC.md §7): a node annotates only the settings a stage deviates on.
// Defaults are what NO annotation means, so a reader sees exactly how a stage leaves them.
TEST(GraphTest, GraphAnnotatesNonDefaultAttributes) {
    const kitci::ParseResult result = kitci::parse_gate_toml(kAnnotatedToml);
    ASSERT_TRUE(result.ok) << result.error.message;
    const std::string mermaid = kitci::graph_mermaid(result.config);
    const std::string html = kitci::graph_html(result.config);
    const std::string dot = kitci::graph_dot(result.config);
    for (const std::string* text : {&mermaid, &html, &dot}) {
        EXPECT_TRUE(Contains(*text, "fail_on exit:5")) << *text;
        EXPECT_TRUE(Contains(*text, "timeout 60")) << *text;
        EXPECT_TRUE(Contains(*text, "files *.py")) << *text;
        EXPECT_TRUE(Contains(*text, "when tool:ruff")) << *text;
    }
    // And the defaults are silent: `plain` declares none of the four, so it carries no
    // annotation in any format. The Mermaid node label is the bare name.
    EXPECT_TRUE(Contains(mermaid, "s0[\"plain\"]")) << mermaid;
    // dot: the node's comment is the tier list and nothing else.
    EXPECT_TRUE(Contains(dot, "comment=\"tiers: full\"]")) << dot;
    // HTML: the detail panel names only the stage that deviates.
    const std::size_t details = html.find("<div class=\"details\">");
    ASSERT_NE(details, std::string::npos) << html;
    EXPECT_NE(html.find("deviant", details), std::string::npos) << html;
    EXPECT_EQ(html.find("plain", details), std::string::npos) << html;
}

TEST(GraphTest, GraphAnnotationsAreStable) {
    const kitci::ParseResult first = kitci::parse_gate_toml(kAnnotatedToml);
    const kitci::ParseResult second = kitci::parse_gate_toml(kAnnotatedToml);
    ASSERT_TRUE(first.ok) << first.error.message;
    ASSERT_TRUE(second.ok) << second.error.message;
    const std::vector<std::string> a = RenderAll(first.config);
    const std::vector<std::string> b = RenderAll(second.config);
    ASSERT_EQ(a.size(), 3U);
    // An annotation is not a licence to become a function of something else: same config,
    // same bytes, still a whole document per format.
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FALSE(a[i].empty()) << "format " << i << " produced nothing";
        EXPECT_EQ(a[i], b[i]) << "format " << i << " is not byte-stable with annotations";
    }
    EXPECT_TRUE(Contains(a[0], "flowchart")) << a[0];
    EXPECT_TRUE(Contains(a[1], "<!DOCTYPE html>")) << a[1];
    EXPECT_EQ(a[2].back(), '\n');
}
