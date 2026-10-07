// The libFuzzer entry point for the gate.toml parser (SPEC.md §6).
//
// The parser's contract is total (SPEC.md §5): every byte string is either a Config or an
// error with a 1-based line number. This target asserts the half of that contract a crash
// would not: a rejected input must name a line, and must carry a message. Anything else is
// a bug in the parser, not an acceptable input.
//
// Stage C extends the same contract to the graph output, which SPEC.md §5 says is a pure
// function of the parsed config: every accepted input is rendered in all three formats, and
// a format that crashed, returned nothing, or emitted an absolute URL (the self-contained
// HTML invariant) is a finding.
//
// The v1.1 amendment extends it to the canonical AST (--ast): every accepted config is
// rendered, twice, and a rendering that is empty or differs between two calls of the same
// parse is a finding — being a pure function is what makes a golden file mean anything.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "kitci/ast.hpp"
#include "kitci/graph.hpp"
#include "kitci/parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);
    const kitci::ParseResult result = kitci::parse_gate_toml(text);
    if (!result.ok) {
        if (result.error.line < 1 || result.error.message.empty()) {
            std::abort();
        }
        return 0;
    }

    const std::string mermaid = kitci::graph_mermaid(result.config);
    const std::string html = kitci::graph_html(result.config);
    const std::string dot = kitci::graph_dot(result.config);
    const std::string ast = kitci::ast_json(result.config);
    if (mermaid.empty() || html.empty() || dot.empty() || ast.empty()) {
        std::abort();
    }
    // Determinism is part of what being a pure function means.
    if (kitci::graph_render(result.config, kitci::GraphFormat::kMermaid) != mermaid) {
        std::abort();
    }
    if (kitci::ast_json(result.config) != ast) {
        std::abort();
    }
    // SPEC.md §7: the HTML page opens from file:// with no network, so it carries no
    // absolute URL at all — not even the SVG namespace it does not need.
    if (html.find("http://") != std::string::npos || html.find("https://") != std::string::npos) {
        std::abort();
    }
    return 0;
}
