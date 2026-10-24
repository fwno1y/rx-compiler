#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rx::sema {
    struct Ty;
    using TyPtr = std::shared_ptr<Ty>;
    // 语义类型
    struct Ty {
        enum class Kind {
            Error, // 错误占位：级联抑制用，与任何类型兼容
            Unit, // ()
            Never, // !（return / break / continue 等）
            Bool, // bool
            Int, // i8..i128 / isize / u8..u128 / usize
            Ref, // &T / &mut T；&&T 用嵌套 Ref 表示
            Array, // [T; N]
            Struct, // 命名结构体，含预置的 Box / Vec
            Fn, // 函数 / 方法类型（仅用于调用检查，无名字
        };

        Kind kind = Kind::Error;
        // Int：名字；Struct：类型名，二者共用
        std::string name;
        bool isSigned = true; // Int：是否有符号
        // Ref
        bool refMut = false; // 是否有mut
        TyPtr refTo; // 被引用的类型
        // Array
        TyPtr element; // 元素类型
        std::size_t len = 0; // 长度
        // Fn
        std::vector<TyPtr> params; // 参数类型
        TyPtr ret; // 返回类型
    };

    inline TyPtr makeTy(Ty::Kind k) {
        auto t = std::make_shared<Ty>();
        t->kind = k;
        return t;
    }

    inline TyPtr tyError() {
        return makeTy(Ty::Kind::Error);
    }
    inline TyPtr tyUnit() {
        return makeTy(Ty::Kind::Unit);
    }
    inline TyPtr tyNever() {
        return makeTy(Ty::Kind::Never);
    }
    inline TyPtr tyBool() {
        return makeTy(Ty::Kind::Bool);
    }
    inline TyPtr tyInt(const std::string& name) {
        auto t = makeTy(Ty::Kind::Int);
        t->name = name;
        t->isSigned = !name.empty() && name[0] == 'i';
        return t;
    }
    inline TyPtr tyStruct(const std::string& name) {
        auto t = makeTy(Ty::Kind::Struct);
        t->name = name;
        return t;
    }
    inline TyPtr tyRef(TyPtr to, bool isMut = false) {
        auto t = makeTy(Ty::Kind::Ref);
        t->refTo = std::move(to);
        t->refMut = isMut;
        return t;
    }
    inline TyPtr tyArray(TyPtr elem, std::size_t len) {
        auto t = makeTy(Ty::Kind::Array);
        t->element = elem;
        t->len = len;
        return t;
    }
    inline TyPtr tyFn(std::vector<TyPtr> params, TyPtr ret) {
        auto t = makeTy(Ty::Kind::Fn);
        t->params = std::move(params);
        t->ret = std::move(ret);
        return t;
    }
    inline bool isError(const TyPtr& t) {
        return !t || t->kind == Ty::Kind::Error;
    }
    inline bool isUnit(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Unit;
    }
    inline bool isNever(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Never;
    }
    inline bool isBool(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Bool;
    }
    inline bool isInt(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Int;
    }
    inline bool isRef(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Ref;
    }
    inline bool isArray(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Array;
    }
    inline bool isStruct(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Struct;
    }
    inline bool isFn(const TyPtr& t) {
        return t && t->kind == Ty::Kind::Fn;
    }
    // 名字是内建整型则返回对应类型，否则返回 nullptr。
    inline TyPtr builtinIntTy(const std::string& name) {
        static const char* Ints[] = {
            "i8",  "i16",  "i32",  "i64",  "i128",  "isize",
            "u8",  "u16",  "u32",  "u64",  "u128",  "usize",
        };
        for (auto* n : Ints)
            if (name == n) {
                return tyInt(name);
            }
        return nullptr;
    }
    // 类型相等
    inline bool typeEquals(const TyPtr& a, const TyPtr& b) {
        if (isError(a) || isError(b)) {
            return true;
        }
        if (a->kind != b->kind) {
            return false;
        }
        switch (a->kind) {
            case Ty::Kind::Error:
            case Ty::Kind::Unit:
            case Ty::Kind::Never:
            case Ty::Kind::Bool:
                return true;
            case Ty::Kind::Int:
            case Ty::Kind::Struct:
                return a->name == b->name;
            case Ty::Kind::Ref:
                return a->refMut == b->refMut && typeEquals(a->refTo, b->refTo);
            case Ty::Kind::Array:
                return a->len == b->len && typeEquals(a->element, b->element);
            case Ty::Kind::Fn: {
                if (a->params.size() != b->params.size()) {
                    return false;
                }
                for (std::size_t i = 0; i < a->params.size(); ++i) {
                    if (!typeEquals(a->params[i], b->params[i])) {
                        return false;
                    }
                }
                return typeEquals(a->ret, b->ret);
            }
        }
        return false;
    }
    // 调试打印，把类型转成字符串
    inline std::string tyToString(const TyPtr& t) {
        if (!t) {
            return "<null>";
        }
        switch (t->kind) {
            case Ty::Kind::Error: return "<error>";
            case Ty::Kind::Unit: return "()";
            case Ty::Kind::Never: return "!";
            case Ty::Kind::Bool: return "bool";
            case Ty::Kind::Int:
            case Ty::Kind::Struct:
                return t->name.empty() ? std::string("<anonymous>") : t->name;
            case Ty::Kind::Ref: {
                std::string s = "&";
                if (t->refMut) {
                    s += "mut ";
                }
                return s + tyToString(t->refTo);
            }
            case Ty::Kind::Array:
                return "[" + tyToString(t->element) + "; " + std::to_string(t->len) + "]";
            case Ty::Kind::Fn: {
                std::string s = "fn(";
                for (std::size_t i = 0; i < t->params.size(); ++i) {
                    if (i) {
                        s += ", ";
                    }
                    s += tyToString(t->params[i]);
                }
                s += ")";
                if (t->ret && t->ret->kind != Ty::Kind::Unit) {
                    s += " -> " + tyToString(t->ret);
                }
                return s;
            }
        }
        return "?";
    }
}