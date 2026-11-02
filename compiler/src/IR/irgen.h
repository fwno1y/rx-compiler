#pragma once

#include <ostream>
#include <string>
#include "ast/AST.h"

namespace rx::ir {
    class IrGen {
    public:
        explicit IrGen(std::ostream& out) : out_(out) {}
        void emitCrate(const ast::Crate& crate);
    private:
        std::ostream& out_;
        int tmp_ = 0;

        std::string tmp() {
            return "%t" + std::to_string(tmp_++);
        }
        void line(const std::string& s) {
            out_ << s << "\n";
        }
        void emitFunction(const ast::FunctionDef& fn);
        void emitStmt(const ast::Stmt& s);
        bool emitBuiltinCall(const ast::Call& call);
    };
}