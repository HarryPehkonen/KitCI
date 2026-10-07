// The libFuzzer entry point for the gate.toml parser (SPEC.md §6).
//
// The parser's contract is total (SPEC.md §5): every byte string is either a Config or an
// error with a 1-based line number. This target asserts the half of that contract a crash
// would not: a rejected input must name a line, and must carry a message. Anything else is
// a bug in the parser, not an acceptable input.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "kitci/parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);
    const kitci::ParseResult result = kitci::parse_gate_toml(text);
    if (!result.ok) {
        if (result.error.line < 1 || result.error.message.empty()) {
            std::abort();
        }
    }
    return 0;
}
