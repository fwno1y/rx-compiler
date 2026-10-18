#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "ast/AST.h"
#include "Ty.h"

namespace rx::sema {
    // 变量 / 参数 / 常量 / 函数 / 内建
    struct Symbol {
        enum class Kind {
            Local, // 变量
            Param, // 参数
            Const, // 常量
            Function, // 函数
            Builtin, // 内建
        };
        Kind kind = Kind::Local;
        TyPtr type;
        bool isMut = false;
        const ast::FunctionDef* fn = nullptr;
        const ast::ConstItem* constItem = nullptr;
    };
    // 一个结构体
    struct StructInfo {
        const ast::StructDef* def = nullptr;
        std::vector<std::string> fieldOrder; // 声名顺序
        std::unordered_map<std::string, TyPtr> fieldType; // 字段类型
        bool deriveCopy = false;
        bool deriveClone = false;
        bool derivePartialEq = false;
        bool deriveEq = false;
        std::unordered_map<std::string, TyPtr> methods; // impl项：方法名到函数类型
    };
    class SymbolTable {
    public:
        StructInfo& defineStruct(const std::string& name) {
            return structs_[name];
        }
        bool hasStruct(const std::string& name) const {
            return structs_.count(name) != 0;
        }
        StructInfo* findStruct(const std::string& name) {
            auto it = structs_.find(name);
            return it == structs_.end() ? nullptr : &it->second;
        }
        const StructInfo* findStruct(const std::string& name) const {
            auto it = structs_.find(name);
            return it == structs_.end() ? nullptr : &it->second;
        }
        std::unordered_map<std::string, StructInfo>& structs() {
            return structs_;
        }
        const std::unordered_map<std::string, StructInfo>& structs() const {
            return structs_;
        }

        void defineGlobal(const std::string& name, Symbol symbol) {
            globals_[name] = std::move(symbol);
        }
        bool hasGlobal(const std::string& name) {
            return globals_.count(name) != 0;
        }
        Symbol* findGlobal(const std::string& name) {
            auto it = globals_.find(name);
            return it == globals_.end() ? nullptr : &it->second;
        }
        std::unordered_map<std::string, Symbol>& globals() {
            return globals_;
        }

        void pushScope() {
            scopes_.emplace_back();
        }
        void popScope() {
            if (!scopes_.empty()) {
                scopes_.pop_back();
            }
        }
        void declareLocal(const std::string& name, Symbol symbol) {
            scopes_.back()[name] = std::move(symbol);
        }
        Symbol* lookup(const std::string& name) {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                auto f = it->find(name);
                if (f != it->end()) {
                    return &f->second;
                }
            }
            auto g = globals_.find(name);
            return g == globals_.end() ? nullptr : &g->second;
        }
        bool inScope() const {
            return !scopes_.empty();
        }

    private:
        std::unordered_map<std::string, StructInfo> structs_;
        std::unordered_map<std::string, Symbol> globals_;
        std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    };

}