// The graph outputs: the parsed config rendered as text (SPEC.md §7, stage C).
//
// Three emit-only formats — Mermaid, a standalone HTML page, and GraphViz dot. None of them
// renders anything, runs a subprocess, or touches the network: each is a pure function of
// the parsed config, and each is byte-stable for a given config, so the output can be
// golden-tested and diffed. That "generate, do not render" rule is the invariant of this
// surface: kit-ci emits text, and whatever viewer the human points at it does the drawing.
#ifndef KITCI_GRAPH_HPP
#define KITCI_GRAPH_HPP

#include <cstdint>
#include <string>

#include "kitci/parser.hpp"

namespace kitci {

// The three text formats of --graph, chosen by --format.
enum class GraphFormat : std::uint8_t { kMermaid, kHtml, kDot };

// The --format value: exactly "mermaid", "html" or "dot". False for anything else,
// including the empty string and a correct name in the wrong case.
bool parse_graph_format(const std::string& name, GraphFormat* format);

// The three renderings. Equal configs produce equal bytes.
std::string graph_mermaid(const Config& config);
std::string graph_html(const Config& config);
std::string graph_dot(const Config& config);

// One of the three, chosen by an already-validated format.
std::string graph_render(const Config& config, GraphFormat format);

}  // namespace kitci

#endif  // KITCI_GRAPH_HPP
