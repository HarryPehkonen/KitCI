// kit-ci — a CI gate engine that reads a per-repo gate.toml (SPEC.md).
//
// This file owns the command line, the tiers and everything a human reads. What a stage
// means is the runner's (include/kitci/runner.hpp); what a config may say is the parser's;
// what the config looks like is the graphs' (include/kitci/graph.hpp).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "kitci/ast.hpp"
#include "kitci/graph.hpp"
#include "kitci/parser.hpp"
#include "kitci/runner.hpp"

namespace {

constexpr const char* kUsage =
    "usage: kit-ci [--gate <path>] [--tier fast|full] [--changed] [--list] [--ast]\n"
    "              [--graph [--format mermaid|html|dot]] [--strict|--no-strict]\n"
    "\n"
    "  (no arguments)   run the default tier: 'full' when the config declares it, else the\n"
    "                   first tier it declares\n"
    "  --tier <name>    run a named tier\n"
    "  --changed        scope stage file globs to files changed vs merge-base with main\n"
    "  --list           print stages, tiers, and which stages are in which tier\n"
    "  --ast            print the parsed config as canonical JSON — one reading, for machines\n"
    "  --graph          print the gate flow as text — the config explains itself\n"
    "  --format <name>  graph format: mermaid (default), html, or dot\n"
    "  --graph-html     shorthand for --graph --format html\n"
    "  --gate <path>    config path (default: gate.toml in the repo root)\n"
    "  --strict         strictness; the default — a stage with an empty cmd is a failure\n"
    "  --no-strict      run an empty cmd as the no-op it is\n"
    "\n"
    "exit: 0 all stages passed; 1 a stage failed; 2 config unreadable, invalid, or nothing\n"
    "      ran\n";

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

void PrintIndented(const std::string& text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t newline = text.find('\n', pos);
        const std::string line =
            newline == std::string::npos ? text.substr(pos) : text.substr(pos, newline - pos);
        std::printf("      %s\n", line.c_str());
        if (newline == std::string::npos) {
            break;
        }
        pos = newline + 1;
    }
}

void PrintStage(const kitci::StageResult& stage) {
    std::printf("\n==> %s\n", stage.name.c_str());
    if (!stage.cmd.empty()) {
        std::printf("    cmd: %s\n", stage.cmd.c_str());
    }
    switch (stage.status) {
        case kitci::StageStatus::kSkipped:
            std::printf("    SKIP (%s)\n", stage.skip_reason.c_str());
            return;
        case kitci::StageStatus::kPassed:
            std::printf("    pass (%.1fs)\n", stage.seconds);
            return;
        case kitci::StageStatus::kFailed:
            break;
    }
    // SPEC.md §4: the first `summary` lines of the failed stage's combined output, indented,
    // with the command above it so the human can re-run it.
    std::printf("    FAIL (%s)\n", stage.fail_reason.c_str());
    const std::string shown = kitci::head_lines(stage.output, stage.summary_lines);
    if (!shown.empty()) {
        std::printf("    --- output, first %d line(s) ---\n", stage.summary_lines);
        PrintIndented(shown);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string gate_path = "gate.toml";
    std::string tier_name;
    bool list = false;
    bool graph = false;
    bool ast = false;
    bool format_given = false;
    bool changed = false;
    bool strict = true;
    kitci::GraphFormat format = kitci::GraphFormat::kMermaid;

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
            tier_name = argv[++i];
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--ast") {
            ast = true;
        } else if (arg == "--changed") {
            changed = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--no-strict") {
            strict = false;
        } else if (arg == "--graph") {
            graph = true;
        } else if (arg == "--graph-html") {
            // Documented shorthand for --graph --format html. Flags are read left to right,
            // so a later --format still wins.
            graph = true;
            format = kitci::GraphFormat::kHtml;
        } else if (arg == "--format") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "kit-ci: --format needs a value (mermaid, html or dot)\n");
                return 2;
            }
            const std::string value = argv[++i];
            if (!kitci::parse_graph_format(value, &format)) {
                std::fprintf(stderr, "kit-ci: unknown --format '%s' (want mermaid, html or dot)\n",
                             value.c_str());
                return 2;
            }
            format_given = true;
        } else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else {
            std::fprintf(stderr, "kit-ci: unknown option '%s' (try --help)\n", arg.c_str());
            return 2;
        }
    }

    // A flag that would do nothing is refused rather than accepted quietly: the graph is a
    // function of the whole config, and two output modes cannot both be the output.
    if (format_given && !graph) {
        std::fprintf(stderr, "kit-ci: --format needs --graph (or --graph-html)\n");
        return 2;
    }
    // Each of these three flags IS an output, and one process prints one output. Asking for
    // two is refused rather than silently resolved: printing one and dropping the other would
    // be exactly the silent-wrong-answer class this tool exists to avoid.
    const int outputs = (list ? 1 : 0) + (graph ? 1 : 0) + (ast ? 1 : 0);
    if (outputs > 1) {
        std::fprintf(stderr,
                     "kit-ci: --list, --graph and --ast ask for three different outputs — ask "
                     "for one\n");
        return 2;
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
    const kitci::Config& config = result.config;

    if (list) {
        return PrintList(config);
    }
    if (graph) {
        // The graph shows every stage and every tier, so a run option has nothing to act on.
        // Saying so is the difference between "ignored" and "silently ignored".
        if (!tier_name.empty() || changed || !strict) {
            std::fprintf(stderr,
                         "kit-ci: --graph ignores run options (--tier, --changed, --strict): "
                         "the graph is a function of the whole config\n");
        }
        std::fputs(kitci::graph_render(config, format).c_str(), stdout);
        return 0;
    }
    if (ast) {
        // Like the graph, the AST is a reading of the WHOLE config, so a run option has
        // nothing to act on — and saying so beats looking like it did something.
        if (!tier_name.empty() || changed || !strict) {
            std::fprintf(stderr,
                         "kit-ci: --ast ignores run options (--tier, --changed, --strict): the "
                         "AST is a function of the whole config\n");
        }
        std::fputs(kitci::ast_json(config).c_str(), stdout);
        return 0;
    }

    // The tier a bare `kit-ci` runs: hook tiers are named, and a human running the gate by
    // hand wants the strong one (QUESTIONS.md Q9).
    std::string wanted = tier_name;
    if (wanted.empty()) {
        if (config.tiers.count("full") != 0) {
            wanted = "full";
        } else if (!config.tier_order.empty()) {
            wanted = config.tier_order.front();
        }
    }
    if (wanted.empty()) {
        std::fprintf(stderr, "kit-ci: no tier declared in %s\n", gate_path.c_str());
        return 2;
    }
    const auto tier = config.tiers.find(wanted);
    if (tier == config.tiers.end()) {
        std::fprintf(stderr, "kit-ci: no [tier.%s] in %s\n", wanted.c_str(), gate_path.c_str());
        return 2;
    }

    kitci::RunOptions options;
    options.strict = strict;
    if (changed) {
        const kitci::ChangedFiles files = kitci::changed_files_from_git(".");
        if (files.known) {
            options.changed = true;
            options.changed_files = files.files;
            std::printf("kit-ci: --changed — %zu file(s) changed against main\n",
                        files.files.size());
        } else {
            // Skipping every globbed stage would be the one answer that lies, so the run
            // says why it is not scoping anything.
            std::fprintf(stderr, "kit-ci: --changed: %s\n", files.note.c_str());
        }
    }

    const kitci::RunResult run = kitci::run_tier(config, tier->second, options);
    if (run.stages.empty()) {
        // Nothing ran: exit 2 is the config's answer, not a stage's (SPEC.md §2).
        if (!run.error.empty()) {
            std::fprintf(stderr, "kit-ci: %s\n", run.error.c_str());
        }
        std::printf("%s\n",
                    run.verdict.empty() ? "GATE FAILED — nothing ran" : run.verdict.c_str());
        return run.exit_code;
    }

    std::printf("kit-ci: %s — tier '%s', %zu stage(s)\n", gate_path.c_str(), wanted.c_str(),
                run.stages.size());
    for (const kitci::StageResult& stage : run.stages) {
        PrintStage(stage);
    }
    std::printf("\n%s\n", run.verdict.c_str());
    return run.exit_code;
}
