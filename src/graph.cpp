// The graph outputs: the parsed config as text (SPEC.md §7, stage C).
//
// Three emit-only formats — Mermaid, a standalone HTML page and GraphViz dot. Nothing here
// renders, spawns a process or touches the network: every format is a pure function of the
// parsed config and byte-stable for a given config, so the output can be golden-tested and
// diffed. The HTML page carries an inline SVG and no JavaScript, so it opens from a file://
// URL with zero network — which is also why it contains no absolute URL at all, not even the
// SVG namespace an inline <svg> in an HTML5 document does not need.
//
// Layout is deterministic: nodes sit in longest-path columns, ties broken by declaration
// order, and a cycle (one tier runs a then b, another runs b then a) puts its trapped nodes
// in one extra column instead of sending the layout round forever.
#include "kitci/graph.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace kitci {
namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// ------------------------------------------------------------------ helpers

template <typename T>
std::string Number(T value) {
    return std::to_string(value);
}

std::string NodeId(std::size_t index) {
    std::string id = "s";
    id += Number(index);
    return id;
}

// A label is the one place a stray byte could break a viewer, so all three formats escape.
// The vocabulary restricts names to [A-Za-z0-9_-], which makes this belt and braces.
std::string XmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

// Self-containment (SPEC.md §7, §8) is a byte-level property: the page carries no "http://" or
// "https://" at all. Most of the page's text is the vocabulary's own ([A-Za-z0-9_-] names, see
// above), but `repo` is a free display string (§3), so a page echoing a repo named after a URL
// used to carry one. Display text therefore goes through this: a colon followed by "//" is
// written as an entity, which a browser renders as ':' — the reader sees the name unchanged and
// the bytes hold no absolute URL, whatever the config says. Found by the gate's fuzz stage on
// 2026-10-06 (card t_5ee2ee12; input: crash-300de12c3ac55acba12edd364e9b90eacaaa41cf).
std::string HtmlDisplay(const std::string& text) {
    std::string out = XmlEscape(text);
    for (std::size_t at = out.find("://"); at != std::string::npos; at = out.find("://", at + 5)) {
        out.replace(at, 1, "&#58;");
    }
    return out;
}

std::string DotEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// One line, no control bytes: what a comment or a single-line label may carry.
std::string OneLine(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
    }
    return out;
}

// ------------------------------------------------------------------ the model

// One fill/stroke pair per tier, cycled in declaration order. All three formats use the same
// palette, so a colour read off one can be carried to another.
struct TierColor {
    const char* fill;
    const char* stroke;
};

constexpr TierColor kPalette[] = {
    {"#0b253a", "#38bdf8"},  // cyan
    {"#0f2a1e", "#34d399"},  // emerald
    {"#2a2311", "#fbbf24"},  // amber
    {"#2a1220", "#f472b6"},  // pink
    {"#221a33", "#a78bfa"},  // violet
    {"#2a1414", "#f87171"},  // red
    {"#0e2a2a", "#2dd4bf"},  // teal
    {"#241a0f", "#fb923c"},  // orange
};
constexpr std::size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

constexpr const char* kNeutralFill = "#1e293b";
constexpr const char* kNeutralStroke = "#64748b";
constexpr const char* kTextColor = "#e2e8f0";
constexpr const char* kMutedColor = "#94a3b8";

const TierColor& ColorFor(std::size_t tier_index) {
    return kPalette[tier_index % kPaletteSize];
}

// One step of a tier's run order. A step several tiers ask for is one edge carrying several
// tier indices: the graph would otherwise draw the same arrow twice.
struct Edge {
    std::size_t from = 0;
    std::size_t to = 0;
    std::vector<std::size_t> tiers;
};

struct Model {
    std::string repo;                                  // display only, optional
    std::vector<std::string> names;                    // stage names, declaration order
    std::vector<std::vector<std::size_t>> tiers;       // per tier: node indices, run order
    std::vector<std::string> tier_names;               // tier names, declaration order
    std::vector<std::vector<std::size_t>> node_tiers;  // per node: tier indices
    std::vector<Edge> edges;                           // deduplicated, first-seen order
    std::vector<std::size_t> owner;                    // per node: first tier that runs it
};

Model BuildModel(const Config& config) {
    Model model;
    model.repo = config.repo;
    model.names = config.stage_order;
    model.tier_names = config.tier_order;
    model.node_tiers.assign(model.names.size(), {});
    model.owner.assign(model.names.size(), kNone);

    std::map<std::string, std::size_t> index_of;
    for (std::size_t i = 0; i < model.names.size(); ++i) {
        index_of[model.names[i]] = i;
    }

    for (std::size_t t = 0; t < config.tier_order.size(); ++t) {
        std::vector<std::size_t> run;
        const auto tier = config.tiers.find(config.tier_order[t]);
        if (tier != config.tiers.end()) {
            for (const std::string& name : resolved_stages(config, tier->second)) {
                const auto found = index_of.find(name);
                if (found == index_of.end()) {
                    continue;  // unreachable for a parsed config; never crash on one anyway
                }
                run.push_back(found->second);
                model.node_tiers[found->second].push_back(t);
                if (model.owner[found->second] == kNone) {
                    model.owner[found->second] = t;
                }
            }
        }
        for (std::size_t i = 0; i + 1 < run.size(); ++i) {
            const std::size_t from = run[i];
            const std::size_t to = run[i + 1];
            Edge* step = nullptr;
            for (Edge& candidate : model.edges) {
                if (candidate.from == from && candidate.to == to) {
                    step = &candidate;
                    break;
                }
            }
            if (step == nullptr) {
                model.edges.push_back(Edge{from, to, {}});
                step = &model.edges.back();
            }
            step->tiers.push_back(t);
        }
        model.tiers.push_back(run);
    }
    return model;
}

// The tiers a stage belongs to, for a label: "fast, full", or "no tier".
std::string TierList(const Model& model, std::size_t node) {
    std::string text;
    for (std::size_t tier : model.node_tiers[node]) {
        if (!text.empty()) {
            text += ", ";
        }
        if (tier < model.tier_names.size()) {
            text += OneLine(model.tier_names[tier]);
        }
    }
    if (text.empty()) {
        text = "no tier";
    }
    return text;
}

// ------------------------------------------------------------------ Mermaid

std::string Mermaid(const Model& model) {
    std::string out;
    out += "flowchart LR\n";
    out += "    %% kit-ci gate flow — from the parsed gate.toml; same config, same bytes\n";
    out += "    %% one node per stage, one edge per step in a tier's run order\n";
    if (!model.repo.empty()) {
        out += "    %% repo: ";
        out += OneLine(model.repo);
        out += "\n";
    }
    for (std::size_t i = 0; i < model.names.size(); ++i) {
        out += "    ";
        out += NodeId(i);
        out += "[\"";
        out += XmlEscape(model.names[i]);
        out += "\"]\n";
    }
    for (const Edge& edge : model.edges) {
        out += "    ";
        out += NodeId(edge.from);
        out += " --> ";
        out += NodeId(edge.to);
        out += "\n";
    }
    for (std::size_t t = 0; t < model.tier_names.size(); ++t) {
        const TierColor& color = ColorFor(t);
        out += "    classDef tier";
        out += Number(t);
        out += " fill:";
        out += color.fill;
        out += ",stroke:";
        out += color.stroke;
        out += ",color:";
        out += kTextColor;
        out += "\n";
    }
    for (std::size_t t = 0; t < model.tiers.size(); ++t) {
        if (model.tiers[t].empty()) {
            continue;
        }
        out += "    class";
        for (std::size_t i = 0; i < model.tiers[t].size(); ++i) {
            if (i == 0) {
                out += " ";
            } else {
                out += ",";
            }
            out += NodeId(model.tiers[t][i]);
        }
        out += " tier";
        out += Number(t);
        out += "\n";
    }
    if (!model.tier_names.empty()) {
        out += "    %% class ids:";
        for (std::size_t t = 0; t < model.tier_names.size(); ++t) {
            out += " tier";
            out += Number(t);
            out += "=";
            out += OneLine(model.tier_names[t]);
        }
        out += "\n";
    }
    return out;
}

// ------------------------------------------------------------------ layout

constexpr int kBoxHeight = 56;
constexpr int kColumnGap = 72;
constexpr int kRowGap = 26;
constexpr int kMargin = 28;
constexpr int kMinColumnWidth = 132;
constexpr int kBoxPadding = 24;
constexpr int kCharWidth = 8;  // ~13px monospace, rounded up

struct Box {
    int x = 0;
    int y = 0;
    int width = 0;
};

struct Layout {
    int width = 0;
    int height = 0;
    std::vector<Box> boxes;           // per node index
    std::vector<std::size_t> column;  // per node index
};

std::vector<std::size_t> AssignColumns(const Model& model) {
    const std::size_t count = model.names.size();
    std::vector<std::vector<std::size_t>> incoming(count);
    for (const Edge& edge : model.edges) {
        incoming[edge.to].push_back(edge.from);
    }
    std::vector<std::size_t> column(count, kNone);
    std::size_t assigned = 0;
    bool progress = true;
    while (progress && assigned < count) {
        progress = false;
        for (std::size_t node = 0; node < count; ++node) {
            if (column[node] != kNone) {
                continue;
            }
            bool ready = true;
            std::size_t best = 0;
            for (std::size_t parent : incoming[node]) {
                if (column[parent] == kNone) {
                    ready = false;
                    break;
                }
                best = std::max(best, column[parent] + 1);
            }
            if (!ready) {
                continue;
            }
            column[node] = best;
            ++assigned;
            progress = true;
        }
    }
    // What is left is in a cycle; it goes into one extra column rather than looping here.
    std::size_t fallback = 0;
    for (std::size_t node = 0; node < count; ++node) {
        if (column[node] != kNone) {
            fallback = std::max(fallback, column[node] + 1);
        }
    }
    for (std::size_t node = 0; node < count; ++node) {
        if (column[node] == kNone) {
            column[node] = fallback;
        }
    }
    return column;
}

Layout BuildLayout(const Model& model) {
    Layout layout;
    layout.boxes.assign(model.names.size(), Box{});
    layout.column = AssignColumns(model);

    std::size_t columns = 0;
    for (std::size_t value : layout.column) {
        columns = std::max(columns, value + 1);
    }
    if (columns == 0) {
        layout.width = kMargin * 2;
        layout.height = kMargin * 2 + kBoxHeight;
        return layout;
    }

    std::vector<std::vector<std::size_t>> rows(columns);
    for (std::size_t node = 0; node < model.names.size(); ++node) {
        rows[layout.column[node]].push_back(node);
    }

    // A column is as wide as its longest label needs, so no box has text spilling out of it.
    std::vector<int> column_width(columns, kMinColumnWidth);
    for (std::size_t c = 0; c < columns; ++c) {
        for (std::size_t node : rows[c]) {
            const std::size_t longest =
                std::max(model.names[node].size(), TierList(model, node).size());
            const int needed = kBoxPadding + kCharWidth * static_cast<int>(longest);
            column_width[c] = std::max(column_width[c], needed);
        }
    }

    std::vector<int> column_x(columns, kMargin);
    int x = kMargin;
    for (std::size_t c = 0; c < columns; ++c) {
        column_x[c] = x;
        x += column_width[c] + kColumnGap;
    }
    layout.width = x - kColumnGap + kMargin;

    std::size_t tallest = 1;
    for (const std::vector<std::size_t>& row_set : rows) {
        tallest = std::max(tallest, row_set.size());
    }
    layout.height = kMargin * 2 + static_cast<int>(tallest) * kBoxHeight +
                    (static_cast<int>(tallest) - 1) * kRowGap;

    for (std::size_t c = 0; c < columns; ++c) {
        for (std::size_t row = 0; row < rows[c].size(); ++row) {
            Box box;
            box.x = column_x[c];
            box.width = column_width[c];
            box.y = kMargin + static_cast<int>(row) * (kBoxHeight + kRowGap);
            layout.boxes[rows[c][row]] = box;
        }
    }
    return layout;
}

// ------------------------------------------------------------------ HTML

constexpr const char* kStyle = R"css(
  :root { color-scheme: dark; }
  body { margin: 0; padding: 24px; background: #020617; color: #e2e8f0;
         font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
  h1 { margin: 0 0 4px; font-size: 18px; }
  p { margin: 0 0 20px; color: #64748b; font-size: 12px; }
  .card { padding: 16px; overflow-x: auto; background: #0f172a; border: 1px solid #1e293b;
          border-radius: 12px; }
  svg { display: block; max-width: 100%; height: auto; }
  .legend { display: flex; flex-wrap: wrap; gap: 8px 20px; margin-top: 16px; font-size: 12px; }
  .legend span { display: inline-flex; align-items: center; gap: 8px; }
  .swatch { width: 12px; height: 12px; border-radius: 3px; display: inline-block; }
  footer { margin-top: 16px; font-size: 10px; color: #475569; }
)css";

void AddAttribute(std::string* out, const char* name, const std::string& value) {
    *out += " ";
    *out += name;
    *out += "=\"";
    *out += value;
    *out += "\"";
}

void AddNumberAttribute(std::string* out, const char* name, int value) {
    AddAttribute(out, name, Number(value));
}

std::string Html(const Model& model) {
    const Layout layout = BuildLayout(model);

    std::string out;
    out += "<!DOCTYPE html>\n";
    out += "<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<title>kit-ci — gate flow</title>\n";
    out += "<style>";
    out += kStyle;
    out += "</style>\n</head>\n<body>\n";

    out += "<h1>kit-ci — gate flow</h1>\n<p>";
    out += Number(model.names.size());
    out += " stage(s), ";
    out += Number(model.tier_names.size());
    out += " tier(s)";
    if (!model.repo.empty()) {
        out += " — repo ";
        out += HtmlDisplay(model.repo);
    }
    out += " · read off the parsed gate.toml, no JavaScript, no external requests</p>\n";

    out += "<div class=\"card\">\n<svg";
    AddAttribute(&out, "viewBox",
                 std::string("0 0 ") + Number(layout.width) + " " + Number(layout.height));
    AddNumberAttribute(&out, "width", layout.width);
    AddNumberAttribute(&out, "height", layout.height);
    AddAttribute(&out, "role", "img");
    AddAttribute(&out, "aria-label", "kit-ci gate flow, one box per stage, arrows in run order");
    AddAttribute(&out, "font-family", "ui-monospace, monospace");
    out += ">\n";

    out += "<defs>\n";
    out +=
        "  <marker id=\"kitci-arrow\" markerWidth=\"9\" markerHeight=\"7\" refX=\"8\" "
        "refY=\"3.5\" orient=\"auto\">\n";
    out += "    <polygon points=\"0 0, 9 3.5, 0 7\" fill=\"#64748b\"/>\n";
    out += "  </marker>\n</defs>\n";

    // Boxes first, edges over them: an arrowhead lands on the target box's edge and stays
    // visible instead of being painted over.
    for (std::size_t node = 0; node < model.names.size(); ++node) {
        const Box& box = layout.boxes[node];
        const bool owned =
            model.owner[node] != kNone && model.owner[node] < model.tier_names.size();
        const TierColor* color = owned ? &ColorFor(model.owner[node]) : nullptr;
        const int centre = box.x + box.width / 2;

        out += "<g>\n  <rect";
        AddNumberAttribute(&out, "x", box.x);
        AddNumberAttribute(&out, "y", box.y);
        AddNumberAttribute(&out, "width", box.width);
        AddNumberAttribute(&out, "height", kBoxHeight);
        AddAttribute(&out, "rx", "6");
        AddAttribute(&out, "fill", color == nullptr ? kNeutralFill : color->fill);
        AddAttribute(&out, "stroke", color == nullptr ? kNeutralStroke : color->stroke);
        AddAttribute(&out, "stroke-width", "1.5");
        out += "/>\n";

        out += "  <text";
        AddNumberAttribute(&out, "x", centre);
        AddNumberAttribute(&out, "y", box.y + 24);
        AddAttribute(&out, "fill", kTextColor);
        AddAttribute(&out, "font-size", "13");
        AddAttribute(&out, "text-anchor", "middle");
        out += ">";
        out += XmlEscape(model.names[node]);
        out += "</text>\n";

        out += "  <text";
        AddNumberAttribute(&out, "x", centre);
        AddNumberAttribute(&out, "y", box.y + 42);
        AddAttribute(&out, "fill", kMutedColor);
        AddAttribute(&out, "font-size", "9");
        AddAttribute(&out, "text-anchor", "middle");
        out += ">";
        out += XmlEscape(TierList(model, node));
        out += "</text>\n</g>\n";
    }

    for (const Edge& edge : model.edges) {
        const Box& from = layout.boxes[edge.from];
        const Box& to = layout.boxes[edge.to];
        // Rightwards when the target starts after the source ends; the same column (a cycle
        // the layout had to break) is drawn right-to-left rather than nowhere.
        const bool rightwards = to.x >= from.x + from.width;
        out += "<line";
        AddNumberAttribute(&out, "x1", rightwards ? from.x + from.width : from.x);
        AddNumberAttribute(&out, "y1", from.y + kBoxHeight / 2);
        AddNumberAttribute(&out, "x2", rightwards ? to.x : to.x + to.width);
        AddNumberAttribute(&out, "y2", to.y + kBoxHeight / 2);
        AddAttribute(&out, "stroke", "#475569");
        AddAttribute(&out, "stroke-width", "1.5");
        AddAttribute(&out, "marker-end", "url(#kitci-arrow)");
        out += "/>\n";
    }

    out += "</svg>\n</div>\n<div class=\"legend\">\n";
    for (std::size_t t = 0; t < model.tier_names.size(); ++t) {
        const TierColor& color = ColorFor(t);
        out += "  <span><i class=\"swatch\" style=\"background:";
        out += color.fill;
        out += ";border:1px solid ";
        out += color.stroke;
        out += "\"></i>tier ";
        out += XmlEscape(OneLine(model.tier_names[t]));
        out += " — ";
        out += Number(model.tiers[t].size());
        out += " stage(s)</span>\n";
    }
    bool any_untiered = false;
    for (std::size_t node = 0; node < model.names.size(); ++node) {
        if (model.owner[node] == kNone) {
            any_untiered = true;
            break;
        }
    }
    if (any_untiered) {
        out += "  <span><i class=\"swatch\" style=\"background:";
        out += kNeutralFill;
        out += ";border:1px solid ";
        out += kNeutralStroke;
        out += "\"></i>no tier</span>\n";
    }
    out += "</div>\n";
    out +=
        "<footer>kit-ci --graph (--format html) · same config, same bytes · opens from a "
        "file:// URL with no network</footer>\n";
    out += "</body>\n</html>\n";
    return out;
}

// ------------------------------------------------------------------ dot

std::string Dot(const Model& model) {
    std::string out;
    out += "digraph kitci_gate {\n";
    out +=
        "  graph [rankdir=\"LR\", bgcolor=\"#020617\", fontname=\"monospace\", "
        "fontcolor=\"#e2e8f0\"];\n";
    out +=
        "  node [shape=\"box\", style=\"filled\", fontname=\"monospace\", "
        "fontcolor=\"#e2e8f0\", color=\"#64748b\"];\n";
    out += "  edge [color=\"#475569\", fontname=\"monospace\"];\n";
    if (!model.repo.empty()) {
        out += "  // repo: ";
        out += OneLine(model.repo);
        out += "\n";
    }

    // A stage in no tier is declared in the body; the clusters below own the rest.
    for (std::size_t node = 0; node < model.names.size(); ++node) {
        if (model.owner[node] != kNone) {
            continue;
        }
        out += "  ";
        out += NodeId(node);
        out += " [label=\"";
        out += DotEscape(model.names[node]);
        out += "\", fillcolor=\"";
        out += kNeutralFill;
        out += "\", color=\"";
        out += kNeutralStroke;
        out += "\"];\n";
    }

    for (std::size_t t = 0; t < model.tiers.size(); ++t) {
        const TierColor& color = ColorFor(t);
        out += "  subgraph \"cluster_tier";
        out += Number(t);
        out += "\" {\n    label=\"tier ";
        out += DotEscape(model.tier_names[t]);
        out += "\";\n    color=\"";
        out += color.stroke;
        out += "\";\n    style=\"dashed\";\n";
        for (std::size_t node : model.tiers[t]) {
            // The first tier that runs a stage owns its node declaration; a later tier
            // mentions it bare, which GraphViz reads as the same node and not a second one.
            if (model.owner[node] == t) {
                out += "    ";
                out += NodeId(node);
                out += " [label=\"";
                out += DotEscape(model.names[node]);
                out += "\", fillcolor=\"";
                out += color.fill;
                out += "\", color=\"";
                out += color.stroke;
                // Cluster membership is ambiguous for a stage several tiers run (the owning
                // cluster is the one that declares it), so the node says its own answer.
                out += "\", comment=\"tiers: ";
                out += DotEscape(TierList(model, node));
                out += "\"];\n";
            } else {
                out += "    ";
                out += NodeId(node);
                out += ";\n";
            }
        }
        out += "  }\n";
    }

    for (const Edge& edge : model.edges) {
        out += "  ";
        out += NodeId(edge.from);
        out += " -> ";
        out += NodeId(edge.to);
        out += ";\n";
    }
    out += "}\n";
    return out;
}

}  // namespace

bool parse_graph_format(const std::string& name, GraphFormat* format) {
    if (name == "mermaid") {
        *format = GraphFormat::kMermaid;
        return true;
    }
    if (name == "html") {
        *format = GraphFormat::kHtml;
        return true;
    }
    if (name == "dot") {
        *format = GraphFormat::kDot;
        return true;
    }
    return false;
}

std::string graph_mermaid(const Config& config) {
    return Mermaid(BuildModel(config));
}

std::string graph_html(const Config& config) {
    return Html(BuildModel(config));
}

std::string graph_dot(const Config& config) {
    return Dot(BuildModel(config));
}

std::string graph_render(const Config& config, GraphFormat format) {
    const Model model = BuildModel(config);
    switch (format) {
        case GraphFormat::kMermaid:
            return Mermaid(model);
        case GraphFormat::kHtml:
            return Html(model);
        case GraphFormat::kDot:
            return Dot(model);
    }
    return {};
}

}  // namespace kitci
