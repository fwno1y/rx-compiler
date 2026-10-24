#pragma once
#include <string>
#include <vector>
#include "ast/AST.h"
#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"

namespace rx::sema {
    class Sema {
    public:
        explicit Sema(const ast::Crate& crate) : crate_(crate) {}
        bool run();
        const Diagnostic& diagnostics() const {
            return diags_;
        }
        const SymbolTable& symbols() {
            return symbols_;
        }
    private:
        const ast::Crate& crate_;
        Diagnostics diags_;
        SymbolTable symbols_;
        TyPtr curReturn_;
        int loopDepth_ = 0;

        // 收集
        void declareBuiltins();
        void collectItems();
        void collectStruct(const ast::StructDef* s);
        void resolveStruct(const ast::StructDef* s);
        void collectFunction(const ast::FunctionDef* f);
        void collectConst(const ast::ConstItem* c);
        void collectImpl(const ast::Impl* impl);
        // 类型解析
        TyPtr resolveType(const ast::Type* t);
        TyPtr resolveNamedType(const ast::TypePath* tp);
        TyPtr resolveFunctionType(const ast::FunctionDef* f);
        // 检查
        void checkFunction(const ast::FunctionDef* f);
        TyPtr checkBlock(const ast::Block* b, bool createsScope);
        void checkStmt(const ast::Stmt* s);
        void checkMain();
        void error(const ast::SourceLoc& loc, const std::string& message) {
            diags_.error(loc, message);
        }
    };
}