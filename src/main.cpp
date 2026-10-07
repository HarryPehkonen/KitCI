// kit-ci — a CI gate engine that reads a per-repo gate.toml (SPEC.md).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "kitci/parser.hpp"

namespace {

constexpr const char* kUsage =
    "usage: kit-ci [--gate <path>] [--tier fast|full] [--changed] [--list]\n"
    "              [--graph] [--graph-html] [--strict|--no-strict]\n"
    "\n"
    "  (no arguments)   run the default tier\n"
    "  --tier <name>    run a named tier\n"
    "  --changed        scope stage file globs to files changed vs main\n"
    "  --list           print stages, tiers, and which stages are in which tier\n"
    "  --graph          print the flow as Mermaid text\n"
    "  --graph-html     print a standalone HTML page embedding the diagram\n"
    "  --gate <path>    config path (default: gate.toml in the repo root)\n"
    "  --strict         strictness; the default (see SPEC.md §5)\n"
    "\n"
    "exit: 0 all stages passed; 1 a stage failed; 2 config unreadable or invalid\n";

std::string ReadFile(const std::string& path, bool* ok) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *ok = true;
    return buffer.str();
}

// The answer --list exists to give: what the stages and tiers are, readable without
// opening the source (SPEC.md §6).
int PrintList(const kitci::Config& config) {
    std::printf("stages:");
    for (const std::string& name : config.stage_order) {
        std::printf(" %s", name.c_str());
    }
    std::printf("\n");
    for (const std::string& tier_name : config.tier_order) {
        std::printf("tier %s:", tier_name.c_str());
        for (const std::string& stage :
             kitci::resolved_stages(config, config.tiers.at(tier_name))) {
            std::printf(" %s", stage.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string gate_path = "gate.toml";
    bool list = false;
    std::string deferred;  // a mode stage B/C owns, accepted but not answered yet

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--gate") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "kit-ci: --gate needs a path\n");
                return 2;
            }
            gate_path = argv[++i];
        } else if (arg == "--tier") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "kit-ci: --tier needs a name\n");
                return 2;
            }
            ++i;
            deferred = arg;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--changed" || arg == "--strict" || arg == "--no-strict" ||
                   arg == "--graph" || arg == "--graph-html") {
            deferred = arg;
        } else {
            std::fprintf(stderr, "kit-ci: unknown option '%s' (try --help)\n", arg.c_str());
            return 2;
        }
    }

    bool read_ok = false;
    const std::string text = ReadFile(gate_path, &read_ok);
    if (!read_ok) {
        std::fprintf(stderr, "kit-ci: cannot read %s\n", gate_path.c_str());
        return 2;
    }

    const kitci::ParseResult result = kitci::parse_gate_toml(text);
    if (!result.ok) {
        std::fprintf(stderr, "kit-ci: %s\n", result.error.message.c_str());
        return 2;
    }

    if (list) {
        return PrintList(result.config);
    }

    if (!deferred.empty()) {
        std::fprintf(stderr, "kit-ci: %s is not implemented yet\n", deferred.c_str());
        return 2;
    }
    std::fprintf(stderr, "kit-ci: running stages is not implemented yet\n");
    return 2;
}
