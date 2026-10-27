#include "Traits.h"

namespace rx::sema {

// 对外查询：每次调用带一份新的 visiting
bool Traits::isCopy(const TyPtr& t) {
    std::unordered_set<std::string> visiting;
    return isCopyImpl(t, visiting);
}

bool Traits::isClone(const TyPtr& t) {
    std::unordered_set<std::string> visiting;
    return isCloneImpl(t, visiting);
}

bool Traits::isPartialEq(const TyPtr& t) {
    std::unordered_set<std::string> visiting;
    return isPartialEqImpl(t, visiting);
}

bool Traits::isEq(const TyPtr& t) {
    std::unordered_set<std::string> visiting;
    return isEqImpl(t, visiting);
}

// Copy
bool Traits::isCopyImpl(const TyPtr& t,
                        std::unordered_set<std::string>& visiting) {
    if (isError(t)) return true; // 错误类型：抑制级联报错

    switch (t->kind) {
        case Ty::Kind::Int:
        case Ty::Kind::Bool:
        case Ty::Kind::Unit:
        case Ty::Kind::Never:
            return true;

        case Ty::Kind::Ref:
            return !t->refMut; // &T: Copy；&mut T: 不是

        case Ty::Kind::Array:
            return isCopyImpl(t->element, visiting);

        case Ty::Kind::Struct: {
            auto* info = symbols_.findStruct(t->name);
            if (!info || !info->deriveCopy) {
                return false;
            }
            // 直接递归结构体不可 Copy；用 visiting 防止死循环
            if (!visiting.insert(t->name).second) {
                return false;
            }
            bool ok = true;
            for (auto& f : info->fieldTypes) {
                if (!isCopyImpl(f.second, visiting)) {
                    ok = false;
                    break;
                }
            }
            visiting.erase(t->name);
            return ok;
        }

        case Ty::Kind::Box:
        case Ty::Kind::Vec: // 拥有堆存储：不 Copy
        case Ty::Kind::Fn:
        case Ty::Kind::Error:
            return false;
    }
    return false;
}

// Clone
bool Traits::isCloneImpl(const TyPtr& t,
                         std::unordered_set<std::string>& visiting) {
    if (isError(t)) return true;

    switch (t->kind) {
        case Ty::Kind::Int:
        case Ty::Kind::Bool:
        case Ty::Kind::Unit:
        case Ty::Kind::Never:
            return true;

        case Ty::Kind::Ref:
            return !t->refMut; // 共享引用可 Clone；&mut 不可

        case Ty::Kind::Array:
            return isCloneImpl(t->element, visiting);

        case Ty::Kind::Box:
        case Ty::Kind::Vec:
            return isCloneImpl(t->element, visiting);

        case Ty::Kind::Struct: {
            auto* info = symbols_.findStruct(t->name);
            if (!info || !info->deriveClone) {
                return false;
            }
            // 递归类型：环上先假设成立（协归纳）
            if (!visiting.insert(t->name).second) {
                return true;
            }
            bool ok = true;
            for (auto& f : info->fieldTypes) {
                if (!isCloneImpl(f.second, visiting)) {
                    ok = false;
                    break;
                }
            }
            visiting.erase(t->name);
            return ok;
        }
        case Ty::Kind::Fn:
        case Ty::Kind::Error:
            return false;
    }
    return false;
}

// PartialEq

bool Traits::isPartialEqImpl(const TyPtr& t,
                             std::unordered_set<std::string>& visiting) {
    if (isError(t)) {
        return true;
    }
    switch (t->kind) {
        case Ty::Kind::Int:
        case Ty::Kind::Bool:
        case Ty::Kind::Unit:
        case Ty::Kind::Never:
            return true;

        case Ty::Kind::Ref:
            // &T / &mut T 比较的是目标
            return isPartialEqImpl(t->refTo, visiting);

        case Ty::Kind::Array:
        case Ty::Kind::Box:
        case Ty::Kind::Vec:
            return isPartialEqImpl(t->element, visiting);

        case Ty::Kind::Struct: {
            auto* info = symbols_.findStruct(t->name);
            if (!info || !info->derivePartialEq) {
                return false;
            }
            if (!visiting.insert(t->name).second) {
                return true;
            }
            bool ok = true;
            for (auto& f : info->fieldTypes) {
                if (!isPartialEqImpl(f.second, visiting)) {
                    ok = false;
                    break;
                }
            }
            visiting.erase(t->name);
            return ok;
        }

        case Ty::Kind::Fn:
        case Ty::Kind::Error:
            return false;
    }
    return false;
}

// Eq

bool Traits::isEqImpl(const TyPtr& t,
                      std::unordered_set<std::string>& visiting) {
    if (isError(t)) {
        return true;
    }
    switch (t->kind) {
        case Ty::Kind::Int:
        case Ty::Kind::Bool:
        case Ty::Kind::Unit:
        case Ty::Kind::Never:
            return true;

        case Ty::Kind::Ref:
            return isEqImpl(t->refTo, visiting);

        case Ty::Kind::Array:
        case Ty::Kind::Box:
        case Ty::Kind::Vec:
            return isEqImpl(t->element, visiting);

        case Ty::Kind::Struct: {
            auto* info = symbols_.findStruct(t->name);
            if (!info || !info->deriveEq) {
                return false;
            }
            if (!visiting.insert(t->name).second) {
                return true;
            }
            bool ok = true;
            for (auto& f : info->fieldTypes) {
                if (!isEqImpl(f.second, visiting)) {
                    ok = false;
                    break;
                }
            }
            visiting.erase(t->name);
            return ok;
        }

        case Ty::Kind::Fn:
        case Ty::Kind::Error:
            return false;
    }
    return false;
}

// derive 校验

void Traits::checkDerives() {
    for (auto& entry : symbols_.structs()) {
        auto& info = entry.second;
        if (!info.def) continue;

        // 重复 derive：同一属性内或跨属性都算（#[derive(Clone, Clone)]）
        std::unordered_set<std::string> seen;
        for (auto& attr : info.def->attributes) {
            for (auto& d : attr.derives) {
                if (!seen.insert(d).second) {
                    diags_.error(info.def->loc,
                                 "duplicate derive `" + d + "`");
                }
            }
        }

        // 依赖关系
        if (info.deriveCopy && !info.deriveClone) {
            diags_.error(info.def->loc, "Copy requires Clone");
        }
        if (info.deriveEq && !info.derivePartialEq) {
            diags_.error(info.def->loc, "Eq requires PartialEq");
        }

        // 每个字段都要支持所请求的能力
        for (auto& f : info.fieldTypes) {
            const std::string& fname = f.first;
            const TyPtr& ft = f.second;

            if (info.deriveCopy && !isCopy(ft)) {
                diags_.error(info.def->loc,
                             "field `" + fname + "` does not implement Copy");
            }
            if (info.deriveClone && !isClone(ft)) {
                diags_.error(info.def->loc,
                             "field `" + fname + "` does not implement Clone");
            }
            if (info.derivePartialEq && !isPartialEq(ft)) {
                diags_.error(info.def->loc,
                             "field `" + fname + "` does not implement PartialEq");
            }
            if (info.deriveEq && !isEq(ft)) {
                diags_.error(info.def->loc,
                             "field `" + fname + "` does not implement Eq");
            }
        }
    }
}

}