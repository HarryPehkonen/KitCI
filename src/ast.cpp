// The canonical AST (--ast): the parsed config as canonical JSON (SPEC.md §7).
//
// A pure function of the parsed Config: no I/O, no clock, no ordering that depends on a map's
// iteration order. Every list here is one the config itself ordered — stages and tiers in
// declaration order, a tier's stages in the run order resolved_stages() gives a run — so the
// document, the graphs and a run are three readings of the same parse.
#include "kitci/ast.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace kitci {
namespace {

constexpr int kIndent = 2;

std::string Indent(int depth) {
    return std::string(static_cast<std::size_t>(depth) * kIndent, ' ');
}

// JSON's own escaping (RFC 8259 §7). The values that pass through here are the vocabulary's
// free text (cmd, repo, files, when, summary, fail_on); each byte is either emitted as itself
// or as an escape, so the rendering cannot fail on a config the parser accepted. That is what
// "escapes nothing new" means: no value is dropped, quoted differently, or refused.
std::string JsonString(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char escape[8];
                    std::snprintf(escape, sizeof escape, "\\u%04x", static_cast<unsigned int>(c));
                    out += escape;
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

// An optional string the config did not write is JSON null. The parser stores "not written"
// as "", and the runner reads that same "" as "no glob" and "no tool gate" — so null is the
// runner's own view of the key, not a second reading of it.
std::string JsonOptional(const std::string& text) {
    return text.empty() ? std::string("null") : JsonString(text);
}

// A list of strings, one per line, or `[]` when there is nothing in it.
std::string JsonStringArray(const std::vector<std::string>& items, int depth) {
    if (items.empty()) {
        return "[]";
    }
    std::string out = "[\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += Indent(depth + 1);
        out += JsonString(items[i]);
        out += i + 1 < items.size() ? ",\n" : "\n";
    }
    out += Indent(depth);
    out += "]";
    return out;
}

}  // namespace

std::string ast_json(const Config& config) {
    std::string out = "{\n";

    // 1. gate — the config's own settings, with the defaults materialised.
    out += Indent(1) + "\"gate\": {\n";
    out += Indent(2) + "\"repo\": " + JsonOptional(config.repo) + ",\n";
    out += Indent(2) + "\"strict\": " + (config.strict ? "true" : "false") + "\n";
    out += Indent(1) + "},\n";

    // 2. tiers — declaration order, each with the stage list a run of it would use (`*`
    // expanded, `tier = "..."` membership included).
    out += Indent(1) + "\"tiers\": ";
    if (config.tier_order.empty()) {
        out += "[],\n";
    } else {
        out += "[\n";
        for (std::size_t t = 0; t < config.tier_order.size(); ++t) {
            const std::string& name = config.tier_order[t];
            const auto tier = config.tiers.find(name);
            const std::vector<std::string> stages = tier == config.tiers.end()
                                                        ? std::vector<std::string>()
                                                        : resolved_stages(config, tier->second);
            out += Indent(2) + "{\n";
            out += Indent(3) + "\"name\": " + JsonString(name) + ",\n";
            out += Indent(3) + "\"stages\": " + JsonStringArray(stages, 3) + "\n";
            out += Indent(2) + "}";
            out += t + 1 < config.tier_order.size() ? ",\n" : "\n";
        }
        out += Indent(1) + "],\n";
    }

    // 3. stages — declaration order, the same order --list prints, with every attribute the
    // grammar documents present whatever the config wrote.
    out += Indent(1) + "\"stages\": ";
    if (config.stage_order.empty()) {
        out += "[]\n";
    } else {
        out += "[\n";
        for (std::size_t i = 0; i < config.stage_order.size(); ++i) {
            const std::string& name = config.stage_order[i];
            const auto found = config.stages.find(name);
            // Unreachable for a parsed config (the two are filled together); a null record
            // rather than a crash, because "never crash on a config" is the parser's rule too.
            const Stage* stage = found == config.stages.end() ? nullptr : &found->second;
            const std::string cmd = stage == nullptr ? std::string() : stage->cmd;
            const std::string tier = stage == nullptr ? std::string() : stage->tier;
            const std::string fail_on =
                stage == nullptr ? std::string(kDefaultFailOn) : stage->fail_on;
            const long timeout = stage == nullptr ? kDefaultTimeout : stage->timeout;
            const std::string files = stage == nullptr ? std::string() : stage->files;
            const std::string when = stage == nullptr ? std::string() : stage->when;
            const std::string summary =
                stage == nullptr ? std::string(kDefaultSummary) : stage->summary;

            out += Indent(2) + "{\n";
            out += Indent(3) + "\"name\": " + JsonString(name) + ",\n";
            out += Indent(3) + "\"cmd\": " + JsonString(cmd) + ",\n";
            out += Indent(3) + "\"tier\": " + JsonOptional(tier) + ",\n";
            out += Indent(3) + "\"fail_on\": " + JsonString(fail_on) + ",\n";
            out += Indent(3) + "\"timeout\": " + std::to_string(timeout) + ",\n";
            out += Indent(3) + "\"files\": " + JsonOptional(files) + ",\n";
            out += Indent(3) + "\"when\": " + JsonOptional(when) + ",\n";
            out += Indent(3) + "\"summary\": " + JsonString(summary) + "\n";
            out += Indent(2) + "}";
            out += i + 1 < config.stage_order.size() ? ",\n" : "\n";
        }
        out += Indent(1) + "]\n";
    }

    out += "}\n";
    return out;
}

}  // namespace kitci
