#pragma once

#include <cstddef>

namespace rx::ast {
    struct SourceLoc {
        std::size_t line = 0;
        std::size_t col = 0;
    };

    // struct SourceRange {
    //     std::size_t begin = 0;
    //     std::size_t end = 0;
    // };
}
