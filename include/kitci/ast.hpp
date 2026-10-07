// The canonical AST (--ast): the parsed config as canonical JSON (SPEC.md §7).
//
// One reading of one parse, for machines: a fixed key order, every default materialised, and
// no line numbers — the grammar appendix in SPEC.md carries the positional truth. It is a
// pure function of the same Config the runner runs, so a runner and an --ast that disagree
// are one bug seen twice.
#ifndef KITCI_AST_HPP
#define KITCI_AST_HPP

#include <string>

#include "kitci/parser.hpp"

namespace kitci {

// The canonical JSON rendering of a parsed config: byte-stable for a given config, and total
// over every config the parser accepts (no input is refused here that the parser accepted).
std::string ast_json(const Config& config);

}  // namespace kitci

#endif  // KITCI_AST_HPP
