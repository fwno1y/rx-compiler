#pragma once

#include <string>
#include <unordered_map>
#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"

namespace rx::sema {
    // 递归布局检查：不能有“无限大小”的结构体。
    // Box/Vec/Ref 视为间接，打断内联环；Array 与内联 struct 继续深入。
    class Layout {
    public:
        Layout(SymbolTable& symbols, Diagnostics& diags) : symbols_(symbols), diags_(diags) {}
        void check();   // 检查所有结构体
    private:
        enum class Color {
            White,
            Gray,
            Black
        };
        SymbolTable& symbols_;
        Diagnostics& diags_;
        std::unordered_map<std::string, Color> color_;

        void visit(const std::string& name);
    };

}