// The gate.toml parser: a tiny, hand-written TOML subset with a total error contract
// (SPEC.md §3, §5). Any byte string is either a Config or one ParseError carrying a
// 1-based line number — there is no input that crashes, hangs, or fails without a line.
//
// Design: one pass over the lines records every fault it can find; a second pass builds
// the Config and checks the cross-references (§3 rules 2, 3, 4, 9, 10). When more than
// one fault exists the one on the EARLIEST line is reported, so the same bytes always
// produce the same answer.
#include "kitci/parser.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace kitci {
namespace {

// ------------------------------------------------------------------ small helpers

bool IsSpaceChar(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

std::size_t SkipSpace(const std::string& text, std::size_t pos) {
    while (pos < text.size() && IsSpaceChar(text[pos])) {
        ++pos;
    }
    return pos;
}

bool IsNameChar(char c) {
    const bool lower = c >= 'a' && c <= 'z';
    const bool upper = c >= 'A' && c <= 'Z';
    const bool digit = c >= '0' && c <= '9';
    return lower || upper || digit || c == '_' || c == '-';
}

bool NameOk(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (!IsNameChar(c)) {
            return false;
        }
    }
    return true;
}

// The index of the '#' that starts a comment, or text.size(). A '#' inside a quoted
// string is part of the string, and a backslash escapes the next character.
std::size_t CommentStart(const std::string& text) {
    bool in_string = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '#') {
            return i;
        }
    }
    return text.size();
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::vector<std::string> SplitDots(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == '.') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsSpaceChar(text[begin])) {
        ++begin;
    }
    while (end > begin && IsSpaceChar(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool Contains(const std::vector<std::string>& names, const std::string& name) {
    for (const std::string& candidate : names) {
        if (candidate == name) {
            return true;
        }
    }
    return false;
}

// SPEC.md §4: the three shapes fail_on takes.
bool FailOnOk(const std::string& value) {
    if (value == "nonzero") {
        return true;
    }
    if (value.rfind("output:", 0) == 0) {
        return value.size() > 7;
    }
    if (value.rfind("exit:", 0) == 0) {
        const std::string digits = value.substr(5);
        if (digits.empty()) {
            return false;
        }
        for (char c : digits) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ value model

struct Value {
    enum class Kind : std::uint8_t { String, StringArray, Integer };

    Kind kind = Kind::String;
    std::string text;                // String
    std::vector<std::string> items;  // StringArray
    long integer = 0;                // Integer
};

struct Entry {
    Value value;
    int line = 0;
};

struct TableState {
    std::string kind;    // "gate" | "tier" | "stage"
    std::string name;    // "" for [gate]
    std::string header;  // "gate" | "tier.fast" | "stage.lint"
    int line = 0;
    std::map<std::string, Entry> values;
};

struct Fault {
    int line = 0;
    std::string message;  // without the "(line N)" suffix
};

bool KeyAllowed(const std::string& kind, const std::string& key) {
    if (kind == "gate") {
        return key == "repo";
    }
    if (kind == "tier") {
        return key == "stages";
    }
    return key == "cmd" || key == "tier" || key == "fail_on" || key == "timeout" ||
           key == "files" || key == "when" || key == "summary";
}

// ------------------------------------------------------------------ the reader

class Reader {
public:
    explicit Reader(const std::string& text) : lines_(SplitLines(text)) {
    }

    ParseResult Parse() {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const int line = static_cast<int>(i) + 1;
            const std::string content = lines_[i].substr(0, CommentStart(lines_[i]));
            const std::size_t start = SkipSpace(content, 0);
            if (start >= content.size()) {
                continue;
            }
            if (content[start] == '[') {
                ReadTableLine(content, start, line);
            } else {
                ReadKeyValueLine(content, start, line);
            }
        }

        Config config;
        CollectStages(&config);
        CollectTiers(&config);
        CheckStageTiers(&config);

        ParseResult result;
        if (faults_.empty()) {
            result.ok = true;
            result.config = config;
            return result;
        }
        const Fault* worst = &faults_.front();
        for (const Fault& fault : faults_) {
            if (fault.line < worst->line) {
                worst = &fault;
            }
        }
        result.ok = false;
        result.error.line = worst->line;
        result.error.message = worst->message + " (line " + std::to_string(worst->line) + ")";
        return result;
    }

private:
    void FaultAt(int line, const std::string& message) {
        faults_.push_back(Fault{line, message});
    }

    // --- line level ---------------------------------------------------------

    void ReadTableLine(const std::string& content, std::size_t start, int line) {
        const std::size_t close = content.find(']', start);
        if (close == std::string::npos) {
            FaultAt(line, "expected ']' to close the table header");
            return;
        }
        const std::string inner = content.substr(start + 1, close - start - 1);
        if (SkipSpace(content, close + 1) != content.size()) {
            FaultAt(line, "trailing characters after the table header");
            return;
        }
        const std::vector<std::string> parts = SplitDots(inner);
        std::string kind;
        std::string name;
        if (parts.size() == 1 && parts[0] == "gate") {
            kind = "gate";
        } else if (parts.size() == 2 && (parts[0] == "stage" || parts[0] == "tier")) {
            kind = parts[0];
            name = parts[1];
        } else {
            FaultAt(line, "unknown table '[" + inner + "]'");
            return;
        }
        if (!name.empty() && !NameOk(name)) {
            FaultAt(line, "invalid name in table '[" + inner + "]'");
            return;
        }
        if (kind != "gate" && name.empty()) {
            FaultAt(line, "invalid name in table '[" + inner + "]'");
            return;
        }
        const std::string header = kind == "gate" ? "gate" : kind + "." + name;
        if (seen_tables_.count(header) != 0) {
            FaultAt(line, "duplicate table [" + header + "]");
            return;
        }
        seen_tables_[header] = line;

        TableState table;
        table.kind = kind;
        table.name = name;
        table.header = header;
        table.line = line;
        tables_.push_back(table);
        current_ = tables_.size() - 1;
        has_current_ = true;
    }

    void ReadKeyValueLine(const std::string& content, std::size_t start, int line) {
        const std::size_t equals = content.find('=', start);
        if (equals == std::string::npos) {
            FaultAt(line, "expected key = value");
            return;
        }
        const std::string key = Trim(content.substr(start, equals - start));
        if (!NameOk(key)) {
            FaultAt(line, "invalid key '" + key + "'");
            return;
        }
        if (!has_current_) {
            FaultAt(line, "key '" + key + "' outside any table");
            return;
        }
        TableState& table = tables_[current_];
        if (!KeyAllowed(table.kind, key)) {
            FaultAt(line, "unknown key '" + key + "' in [" + table.header + "]");
            return;
        }
        if (table.values.count(key) != 0) {
            FaultAt(line, "duplicate key '" + key + "' in [" + table.header + "]");
            return;
        }

        Value value;
        std::size_t pos = SkipSpace(content, equals + 1);
        if (pos >= content.size()) {
            FaultAt(line, "expected a value after '=' for '" + key + "'");
            return;
        }
        if (!ParseValue(content, pos, line, &value, &pos)) {
            return;
        }
        if (SkipSpace(content, pos) != content.size()) {
            FaultAt(line, "trailing characters after the value of '" + key + "'");
            return;
        }

        Entry entry;
        entry.value = value;
        entry.line = line;
        table.values[key] = entry;
        CheckValue(table, key, value, line);
    }

    // --- values -------------------------------------------------------------

    bool ParseValue(const std::string& text, std::size_t pos, int line, Value* value,
                    std::size_t* end) {
        const char c = text[pos];
        if (c == '"') {
            std::string parsed;
            if (!ParseString(text, pos, line, &parsed, end)) {
                return false;
            }
            value->kind = Value::Kind::String;
            value->text = parsed;
            return true;
        }
        if (c == '[') {
            std::vector<std::string> items;
            if (!ParseArray(text, pos, line, &items, end)) {
                return false;
            }
            value->kind = Value::Kind::StringArray;
            value->items = items;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return ParseInteger(text, pos, line, value, end);
        }
        FaultAt(line, "expected a string, an array or an integer");
        return false;
    }

    bool ParseString(const std::string& text, std::size_t pos, int line, std::string* out,
                     std::size_t* end) {
        std::size_t i = pos + 1;
        std::string parsed;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                *out = parsed;
                *end = i + 1;
                return true;
            }
            if (c == '\\') {
                if (i + 1 >= text.size()) {
                    break;
                }
                parsed.push_back(text[i + 1]);
                i += 2;
                continue;
            }
            parsed.push_back(c);
            ++i;
        }
        FaultAt(line, "unterminated string");
        return false;
    }

    bool ParseArray(const std::string& text, std::size_t pos, int line,
                    std::vector<std::string>* out, std::size_t* end) {
        std::size_t i = SkipSpace(text, pos + 1);
        if (i < text.size() && text[i] == ']') {
            *end = i + 1;
            return true;
        }
        while (true) {
            i = SkipSpace(text, i);
            if (i >= text.size()) {
                FaultAt(line, "unterminated array");
                return false;
            }
            if (text[i] != '"') {
                FaultAt(line, "expected a string in the array");
                return false;
            }
            std::string item;
            if (!ParseString(text, i, line, &item, &i)) {
                return false;
            }
            out->push_back(item);
            i = SkipSpace(text, i);
            if (i < text.size() && text[i] == ',') {
                i = SkipSpace(text, i + 1);
                if (i < text.size() && text[i] == ']') {
                    *end = i + 1;
                    return true;
                }
                continue;
            }
            if (i < text.size() && text[i] == ']') {
                *end = i + 1;
                return true;
            }
            FaultAt(line, "expected ',' or ']' in the array");
            return false;
        }
    }

    bool ParseInteger(const std::string& text, std::size_t pos, int line, Value* value,
                      std::size_t* end) {
        std::size_t i = pos;
        if (text[i] == '-') {
            ++i;
        }
        const std::size_t first_digit = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
        }
        if (i == first_digit) {
            FaultAt(line, "expected an integer");
            return false;
        }
        errno = 0;
        const long parsed = std::strtol(text.substr(pos, i - pos).c_str(), nullptr, 10);
        if (errno == ERANGE) {
            FaultAt(line, "integer out of range");
            return false;
        }
        value->kind = Value::Kind::Integer;
        value->integer = parsed;
        *end = i;
        return true;
    }

    void CheckValue(const TableState& table, const std::string& key, const Value& value, int line) {
        const bool wants_string = key == "repo" || key == "cmd" || key == "tier" ||
                                  key == "fail_on" || key == "files" || key == "when" ||
                                  key == "summary";
        if (wants_string && value.kind != Value::Kind::String) {
            FaultAt(line, "expected a string for '" + key + "' in [" + table.header + "]");
            return;
        }
        if (key == "stages" && value.kind != Value::Kind::StringArray) {
            FaultAt(line, "expected an array of strings for 'stages' in [" + table.header + "]");
            return;
        }
        if (key == "timeout") {
            if (value.kind != Value::Kind::Integer) {
                FaultAt(line, "expected an integer for 'timeout' in [" + table.header + "]");
                return;
            }
            if (value.integer < 0) {
                FaultAt(line, "timeout must be an integer >= 0 in [" + table.header + "]");
                return;
            }
        }
        if (key == "fail_on" && !FailOnOk(value.text)) {
            FaultAt(line, "fail_on must be 'nonzero', 'output:<regex>' or 'exit:<n>' in [" +
                              table.header + "]");
        }
    }

    // --- cross references ---------------------------------------------------

    static const Entry* Find(const TableState& table, const std::string& key) {
        const auto it = table.values.find(key);
        return it == table.values.end() ? nullptr : &it->second;
    }

    void CollectStages(Config* config) {
        for (const TableState& table : tables_) {
            if (table.kind != "stage") {
                continue;
            }
            Stage stage;
            stage.name = table.name;
            stage.line = table.line;
            const Entry* cmd = Find(table, "cmd");
            if (cmd == nullptr) {
                FaultAt(table.line, "stage '" + table.name + "' has no cmd");
            } else {
                stage.cmd = cmd->value.text;
            }
            if (const Entry* entry = Find(table, "tier")) {
                stage.tier = entry->value.text;
            }
            if (const Entry* entry = Find(table, "fail_on")) {
                stage.fail_on = entry->value.text;
            }
            if (const Entry* entry = Find(table, "timeout")) {
                stage.timeout = entry->value.integer;
            }
            if (const Entry* entry = Find(table, "files")) {
                stage.files = entry->value.text;
            }
            if (const Entry* entry = Find(table, "when")) {
                stage.when = entry->value.text;
            }
            if (const Entry* entry = Find(table, "summary")) {
                stage.summary = entry->value.text;
            }
            config->stages[stage.name] = stage;
            config->stage_order.push_back(stage.name);
        }
    }

    void CollectTiers(Config* config) {
        for (const TableState& table : tables_) {
            if (table.kind == "gate") {
                if (const Entry* entry = Find(table, "repo")) {
                    config->repo = entry->value.text;
                }
                continue;
            }
            if (table.kind != "tier") {
                continue;
            }
            Tier tier;
            tier.name = table.name;
            tier.line = table.line;
            const Entry* stages = Find(table, "stages");
            if (stages == nullptr) {
                FaultAt(table.line, "[" + table.header + "] has no stages");
            } else {
                tier.stages = stages->value.items;
                for (const std::string& name : tier.stages) {
                    // "*" is the wildcard (SPEC.md §3), not a stage name.
                    if (name != "*" && config->stages.count(name) == 0) {
                        FaultAt(stages->line, "[" + table.header + "] names stage '" + name +
                                                  "' which is not declared");
                    }
                }
                for (std::size_t i = 0; i < tier.stages.size(); ++i) {
                    for (std::size_t j = i + 1; j < tier.stages.size(); ++j) {
                        if (tier.stages[i] == tier.stages[j]) {
                            FaultAt(stages->line, "[" + table.header + "] lists stage '" +
                                                      tier.stages[i] + "' twice");
                        }
                    }
                }
            }
            config->tiers[tier.name] = tier;
            config->tier_order.push_back(tier.name);
        }
    }

    void CheckStageTiers(Config* config) {
        for (const TableState& table : tables_) {
            if (table.kind != "stage") {
                continue;
            }
            const Entry* tier = Find(table, "tier");
            if (tier != nullptr && config->tiers.count(tier->value.text) == 0) {
                FaultAt(tier->line, "stage '" + table.name + "' is in tier '" + tier->value.text +
                                        "' but there is no [tier." + tier->value.text + "]");
            }
        }
    }

    std::vector<std::string> lines_;
    std::vector<Fault> faults_;
    std::vector<TableState> tables_;
    std::map<std::string, int> seen_tables_;
    std::size_t current_ = 0;
    bool has_current_ = false;
};

}  // namespace

ParseResult parse_gate_toml(const std::string& text) {
    return Reader(text).Parse();
}

std::vector<std::string> resolved_stages(const Config& config, const Tier& tier) {
    std::vector<std::string> names;
    for (const std::string& written : tier.stages) {
        if (written == "*") {
            for (const std::string& declared : config.stage_order) {
                if (!Contains(names, declared)) {
                    names.push_back(declared);
                }
            }
        } else if (!Contains(names, written)) {
            names.push_back(written);
        }
    }
    for (const std::string& declared : config.stage_order) {
        const auto it = config.stages.find(declared);
        if (it != config.stages.end() && it->second.tier == tier.name &&
            !Contains(names, declared)) {
            names.push_back(declared);
        }
    }
    return names;
}

}  // namespace kitci
