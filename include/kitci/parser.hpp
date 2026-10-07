// gate.toml — the tiny TOML subset this tool accepts (SPEC.md §3).
//
// Total contract (SPEC.md §5): any byte string is either a Config or a ParseError that
// carries a 1-based line number. There is no input that produces a crash, a hang, or an
// error without a line.
#ifndef KITCI_PARSER_HPP
#define KITCI_PARSER_HPP

#include <map>
#include <string>
#include <vector>

namespace kitci {

// One [stage.X] table.
struct Stage {
    std::string name;
    std::string cmd;                  // required
    std::string tier;                 // optional; adds this stage to that tier's list
    std::string fail_on = "nonzero";  // "nonzero" | "output:<regex>" | "exit:<n>"
    long timeout = 300;               // seconds; 0 = no timeout
    std::string files;                // optional glob, only meaningful with --changed
    std::string when;                 // optional "tool:<name>"
    std::string summary = "head:40";  // how much failed output to show
    int line = 0;                     // 1-based line of the [stage.X] header
};

// One [tier.X] table.
struct Tier {
    std::string name;
    std::vector<std::string> stages;  // as written; "*" is expanded by resolved_stages()
    int line = 0;                     // 1-based line of the [tier.X] header
};

struct Config {
    std::string repo;                      // optional, display only
    std::vector<std::string> stage_order;  // declaration order
    std::map<std::string, Stage> stages;
    std::vector<std::string> tier_order;  // declaration order
    std::map<std::string, Tier> tiers;
};

struct ParseError {
    int line = 0;         // 1-based; always >= 1
    std::string message;  // short, lowercase, names the fault, ends with "(line N)"
};

struct ParseResult {
    bool ok = false;
    Config config;
    ParseError error;

    explicit operator bool() const {
        return ok;
    }
};

// Parse gate.toml. Never throws, never reads files: the caller owns the bytes.
ParseResult parse_gate_toml(const std::string& text);

// The stage names a tier runs, in run order: its `stages` list order, with "*" expanding
// to every declared stage in declaration order, plus every stage whose `tier = "<name>"`
// names this tier (declaration order, no duplicates). SPEC.md §4.
std::vector<std::string> resolved_stages(const Config& config, const Tier& tier);

}  // namespace kitci

#endif  // KITCI_PARSER_HPP
