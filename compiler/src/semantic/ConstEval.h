#pragma once
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "ast/AST.h"
#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"

namespace rx::sema {
    // 常量求值结果：值与它的类型
    struct ConstValue {
        TyPtr type;
        long long value = 0;
    };
    class ConstEval {
    public:
        using TypeResolver = std::function<TyPtr(const ast::Type*)>;
        ConstEval(SymbolTable& symbols, Diagnostics& diags, TypeResolver resolveType) : symbols_(symbols), diags_(diags), resolveType_(std::move(resolveType)) {}
        // 当前 impl 的类型名，用于解析 Self::CONST
        void setSelfType(const std::string& typeName) { selfType_ = typeName; }
        // 求值全局常量 / 关联常量
        std::optional<ConstValue> evalGlobal(const std::string& name);
        std::optional<ConstValue> evalAssoc(const std::string& typeName, const std::string& name);
        // 数组长度 / repeat 次数：必须是 usize
        std::optional<long long> evalUsize(const ast::Expr* e, const ast::SourceLoc& loc);
    private:
        enum class State {
            Unresolved,
            InProgress,
            Done
        };
        struct Entry {
            State state = State::Unresolved;
            bool hasValue = false;
            ConstValue value{};
        };
        SymbolTable& symbols_;
        Diagnostics& diags_;
        TypeResolver resolveType_;
        std::string selfType_; // impl 上下文：Self 的实际类型名
        std::unordered_map<std::string, Entry> state_;  // key: "N" 或 "Type::N"
        std::optional<ConstValue> eval(const ast::Expr* e, const TyPtr& expected);
        std::optional<ConstValue> evalPath(const ast::Path* p);
        std::optional<ConstValue> evalRef(const std::string& key);
        const ast::ConstItem* lookupConst(const std::string& key);
        void error(const ast::SourceLoc& loc, const std::string& msg) {
            diags_.error(loc, msg);
        }
    };
}