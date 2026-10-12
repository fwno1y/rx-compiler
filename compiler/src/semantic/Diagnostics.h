#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>
#include "ast/SourceLoc.h"

namespace rx::sema {
    // 一条语义诊断（错误）
    struct Diagnostic {
        ast::SourceLoc loc; // 出错位置
        std::string message; // 出错原因
    };

    class Diagnostics {
    private:
        std::vector<Diagnostic> diags;
    public:
        void error(const ast::SourceLoc& loc, const std::string& message) {
            diags.push_back(Diagnostic{loc, message});
        }
        bool hasError() const {
            return !diags.empty();
        }
        std::size_t errorCount() const {
            return diags.size();
        }
        const std::vector<Diagnostic>& self() const {
            return diags;
        }
        // 按照出现顺序输出，格式设了“line:col: message”
        void report(std::ostream& os) const {
            for (const auto& d: diags) {
                if (d.loc.line != 0) {
                    os << d.loc.line << ":" << d.loc.col << ": ";
                }
                os << d.message << "\n";
            }
        }
    };
}