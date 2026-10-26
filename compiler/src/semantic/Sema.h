#pragma once
#include <string>
#include "ast/AST.h"
#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"
#include "ConstEval.h"
#include "Layout.h"

namespace rx::sema {
    class Sema {
    public:
        explicit Sema(const ast::Crate& crate) : crate_(crate), constEval_(symbols_, diags_,
                         [this](const ast::Type* t) { return resolveType(t); }) {}
        bool run();
        const Diagnostics& diagnostics() const {
            return diags_;
        }
        const SymbolTable& symbols() const {
            return symbols_;
        }
    private:
        const ast::Crate& crate_;
        Diagnostics diags_;
        SymbolTable symbols_;
        TyPtr curReturn_;
        int loopDepth_ = 0;
        ConstEval constEval_;

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
        TyPtr checkExpr(const ast::Expr* e);
        TyPtr checkLiteral(const ast::Literal* n);
        TyPtr checkPath(const ast::Path* n);
        TyPtr checkBinary(const ast::Binary* n);
        TyPtr checkUnary(const ast::Unary* n);
        TyPtr checkAssign(const ast::Assign* n);
        TyPtr checkCast(const ast::Cast* n);
        TyPtr checkCall(const ast::Call* n);
        TyPtr checkMethodCall(const ast::MethodCall* n);
        TyPtr checkField(const ast::Field* n);
        TyPtr checkIndex(const ast::Index* n);
        TyPtr checkIf(const ast::If* n);
        TyPtr checkLoop(const ast::Loop* n);
        TyPtr checkWhile(const ast::While* n);
        TyPtr checkReturn(const ast::Return* n);
        TyPtr checkBreak(const ast::Break* n);
        TyPtr checkStructLit(const ast::StructLit* n);
        TyPtr checkArrayLit(const ast::ArrayLit* n);
        TyPtr checkArrayRepeat(const ast::ArrayRepeat* n);
        void checkStmt(const ast::Stmt* s);
        void checkMain();
        void checkConstants();
        void checkLayout();
        void error(const ast::SourceLoc& loc, const std::string& message) {
            diags_.error(loc, message);
        }
        // 需要查符号表，所以不是 const
        bool isPlace(const ast::Expr* e);
        bool isMutablePlace(const ast::Expr* e);
    };
}