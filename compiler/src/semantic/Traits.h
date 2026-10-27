#pragma once

#include <string>
#include <unordered_set>

#include "Diagnostics.h"
#include "Symbols.h"
#include "Ty.h"

namespace rx::sema {
    // 能力推导 + derive 校验。
    //
    // 四种能力都是“递归的”：
    //   - 标量：Copy/Clone/PartialEq/Eq 全有
    //   - &T：四种都有；&mut T 只有 PartialEq/Eq（不 Copy、不 Clone）
    //   - [T; N] / Box<T> / Vec<T>：取决于元素 T
    //   - struct：取决于 derive 声明 + 所有字段
    //
    // 递归类型（如 struct Node { children: Vec<Self> }）用 visiting 集合处理：
    // Clone/PartialEq/Eq 遇到环时按“假设成立”返回 true（协归纳）；
    // Copy 遇到环返回 false（直接递归的结构体本来也会被 Layout 拒绝）。
    class Traits {
    public:
        Traits(SymbolTable& symbols, Diagnostics& diags)
            : symbols_(symbols), diags_(diags) {}

        // 校验所有结构体的 #[derive(...)]：重复、依赖关系、字段能力。
        void checkDerives();

        bool isCopy(const TyPtr& t);
        bool isClone(const TyPtr& t);
        bool isPartialEq(const TyPtr& t);
        bool isEq(const TyPtr& t);

    private:
        SymbolTable& symbols_;
        Diagnostics& diags_;

        bool isCopyImpl(const TyPtr& t, std::unordered_set<std::string>& visiting);
        bool isCloneImpl(const TyPtr& t, std::unordered_set<std::string>& visiting);
        bool isPartialEqImpl(const TyPtr& t, std::unordered_set<std::string>& visiting);
        bool isEqImpl(const TyPtr& t, std::unordered_set<std::string>& visiting);
    };

}