#pragma once
#include <string>
#include "ast/AST.h"
#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"
#include "ConstEval.h"
#include "Layout.h"
#include "Traits.h"

namespace rx::sema {
    class Sema {
    public:
        explicit Sema(const ast::Crate& crate) : crate_(crate), traits_(symbols_, diags_), constEval_(symbols_, diags_,
                         [this](const ast::Type* t) { return resolveType(t); }) {}
        bool run();
        const Diagnostics& diagnostics() const {
            return diags_;
        }
        const SymbolTable& symbols() const {
            return symbols_;
        }
    private:
        // 每个正在检查的循环一个上下文，栈顶是最近的可跳转目标
        struct LoopCtx {
            TyPtr breakTy;          // break 值类型；无值 break 记为 ()
            bool isWhile = false;   // while 不接受 break 值
            bool sawBreak = false;
            TyPtr expected;
        };

        const ast::Crate& crate_;
        Diagnostics diags_;
        SymbolTable symbols_;
        Traits traits_;
        TyPtr curReturn_;
        std::string curSelfType_;      // impl / struct 上下文里的 Self 实际类型名
        std::vector<LoopCtx> loops;
        ConstEval constEval_;

        // 容器/方法辅助
        TyPtr unref(const TyPtr& t);                                     // 去掉所有引用层
        bool canCoerce(const TyPtr& actual, const TyPtr& expected);      // &mut T -> &T 等
        TyPtr scalarView(const TyPtr& t, bool& ok);                      // 剥一层共享引用的标量视图
        TyPtr lub(const TyPtr& a, const TyPtr& b,
                  const ast::SourceLoc& loc);                            // 引用 LUB：&mut T / &T -> &T
        TyPtr typeArgOf(const ast::PathSegment& seg);                    // <T> 里的 T
        std::optional<TyPtr> tryBuiltinCall(const ast::Path* p,
                                            const ast::Call* c);         // Box/Vec 构造/关联调用
        std::optional<TyPtr> tryExplicitAssocCall(const ast::Path* p,
                                                  const ast::Call* c);   // Type::method(args)
        bool isMutableAccess(const ast::Expr* e, const TyPtr& ty);

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
        TyPtr checkBlock(const ast::Block* b, bool createsScope, TyPtr expected = nullptr);
        TyPtr checkExpr(const ast::Expr* e,  TyPtr expected = nullptr);
        TyPtr checkLiteral(const ast::Literal* n, const TyPtr& expected);
        TyPtr checkPath(const ast::Path* n);
        TyPtr checkBinary(const ast::Binary* n);
        TyPtr checkUnary(const ast::Unary* n);
        TyPtr checkAssign(const ast::Assign* n);
        TyPtr checkCast(const ast::Cast* n);
        TyPtr checkCall(const ast::Call* n);
        TyPtr checkMethodCall(const ast::MethodCall* n);
        TyPtr checkField(const ast::Field* n);
        TyPtr checkIndex(const ast::Index* n);
        TyPtr checkIf(const ast::If* n, const TyPtr& expected);
        TyPtr checkLoop(const ast::Loop* n, const TyPtr& expected);
        TyPtr checkWhile(const ast::While* n);
        TyPtr checkReturn(const ast::Return* n);
        TyPtr checkBreak(const ast::Break* n);
        TyPtr checkStructLit(const ast::StructLit* n);
        TyPtr checkArrayLit(const ast::ArrayLit* n, const TyPtr& expected);
        TyPtr checkArrayRepeat(const ast::ArrayRepeat* n, const TyPtr& expected);
        TyPtr checkStmt(const ast::Stmt* s);
        void checkMain();
        void checkConstants();
        void checkLayout();
        void error(const ast::SourceLoc& loc, const std::string& message) {
            diags_.error(loc, message);
        }
        // 需要查符号表，所以不是 const
        bool isPlace(const ast::Expr* e);
        bool isMutablePlace(const ast::Expr* e);
        bool isFrozenPlace(const ast::Expr* e);              // 是否经过共享引用投影
        bool isImmutableContainerAccess(const ast::Expr* e); // 不可变 Vec 里取非 Copy 元素
        bool isCopyType(const TyPtr& t, int depth = 0);
    };
}