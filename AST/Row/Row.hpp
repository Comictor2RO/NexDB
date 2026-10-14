#ifndef ROW_HPP
#define ROW_HPP

#include <optional>
#include <string>
#include <vector>

// A single column value. std::nullopt is SQL NULL, which is distinct from the
// four-character string "NULL" (what the literal 'NULL' produces).
using Value = std::optional<std::string>;

struct Row {
    std::vector<Value> values;
};

#endif
