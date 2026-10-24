#include "ConstEval.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace rx::sema {
namespace {

    std::string intSuffix(const std::string& text) {
        static const char* kSuffixes[] = { "isize", "usize", "i32", "u32" };
        for (auto* s : kSuffixes) {
            std::size_t n = std::strlen(s);
            if (text.size() > n && text.compare(text.size() - n, n, s) == 0)
                return s;
        }
        return {};
    }

    struct ParsedInt {
        long long value = 0;
        std::string suffix;
    };

    // 解析整数字面量：支持 0x/0o/0b、_ 分隔和四种后缀
    std::optional<ParsedInt> parseInt(const std::string& text) {
        ParsedInt out;
        out.suffix = intSuffix(text);

        std::string body = text;
        if (!out.suffix.empty()) body.resize(body.size() - out.suffix.size());

        std::size_t start = 0;
        int base = 10;
        if (body.size() > 2 && body[0] == '0') {
            switch (body[1]) {
                case 'x': case 'X': start = 2; base = 16; break;
                case 'b': case 'B': start = 2; base = 2;  break;
                case 'o': case 'O': start = 2; base = 8;  break;
                default: break;
            }
        }

        std::string digits = body.substr(start);
        digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
        if (digits.empty()) return std::nullopt;

        try { out.value = std::stoll(digits, nullptr, base); }
        catch (...) { return std::nullopt; }
        return out;
    }

    } // namespace

    std::optional<ConstValue> ConstEval::evalGlobal(const std::string& name) {
        return evalRef(name);
    }

    std::optional<ConstValue> ConstEval::evalAssoc(const std::string& typeName, const std::string& name) {
        return evalRef(typeName + "::" + name);
    }

    // 用于数组长度和 repeat 次数：结果必须是 usize
    std::optional<long long> ConstEval::evalUsize(const ast::Expr* e, const ast::SourceLoc& loc) {
        auto v = eval(e, tyInt("usize"));       // 把 usize 作为期望类型传下去
        if (!v) return std::nullopt;

        if (!isInt(v->type) || v->type->name != "usize") {
            error(loc, "array length and repeat count must be usize");
            return std::nullopt;
        }
        if (v->value < 0) {
            error(loc, "array length cannot be negative");
            return std::nullopt;
        }
        return v->value;
    }

    // key 是 "N"（全局）或 "Type::N"（关联常量）
    const ast::ConstItem* ConstEval::lookupConst(const std::string& key) {
        auto pos = key.find("::");
        if (pos == std::string::npos) {
            Symbol* s = symbols_.findGlobal(key);
            if (s && s->kind == Symbol::Kind::Const) return s->constItem;
            return nullptr;
        }

        std::string typeName = key.substr(0, pos);
        std::string name = key.substr(pos + 2);
        if (typeName == "Self") typeName = selfType_;

        auto* info = symbols_.findStruct(typeName);
        if (!info) return nullptr;

        auto it = info->assocConsts.find(name);
        return it == info->assocConsts.end() ? nullptr : it->second.def;
    }

    std::optional<ConstValue> ConstEval::evalRef(const std::string& key) {
        Entry& entry = state_[key];

        // 再入 => 依赖环
        if (entry.state == State::InProgress) {
            error(ast::SourceLoc{}, "cyclic constant dependency involving `" + key + "`");
            return std::nullopt;
        }
        if (entry.state == State::Done)
            return entry.hasValue ? std::optional<ConstValue>(entry.value) : std::nullopt;

        const ast::ConstItem* def = lookupConst(key);
        if (!def) {
            error(ast::SourceLoc{}, "constant `" + key + "` cannot be resolved");
            entry.state = State::Done;
            return std::nullopt;
        }

        entry.state = State::InProgress;
        TyPtr declared = resolveType_(def->type.get());

        if (!isError(declared)) {
            auto value = eval(def->value.get(), declared);
            if (value) {
                if (!typeEquals(value->type, declared)) {
                    error(def->loc, "constant initializer does not match its declared type");
                } else {
                    entry.value = *value;
                    entry.hasValue = true;
                }
            }
        }

        entry.state = State::Done;
        return entry.hasValue ? std::optional<ConstValue>(entry.value) : std::nullopt;
    }

    std::optional<ConstValue> ConstEval::evalPath(const ast::Path* p) {
        if (p->segments.empty()) return std::nullopt;

        // 单段：全局常量；若是局部变量则明确拒绝
        if (p->segments.size() == 1) {
            const std::string& name = p->segments[0].ident.text;
            Symbol* s = symbols_.lookup(name);
            if (!s) {
                error(p->loc, "unresolved constant `" + name + "`");
                return std::nullopt;
            }
            if (s->kind == Symbol::Kind::Local || s->kind == Symbol::Kind::Param) {
                error(p->loc, "constant expression cannot refer to a local `" + name + "`");
                return std::nullopt;
            }
            if (s->kind != Symbol::Kind::Const || !s->constItem) {
                error(p->loc, "`" + name + "` is not a constant");
                return std::nullopt;
            }
            return evalRef(name);
        }

        // 两段：Type::CONST 或 Self::CONST
        if (p->segments.size() == 2) {
            std::string typeName = p->segments[0].ident.text;
            const std::string& name = p->segments[1].ident.text;
            if (typeName == "Self") typeName = selfType_;
            if (typeName.empty()) {
                error(p->loc, "`Self` is not available here");
                return std::nullopt;
            }
            return evalRef(typeName + "::" + name);
        }

        error(p->loc, "unsupported constant path");
        return std::nullopt;
    }

    std::optional<ConstValue> ConstEval::eval(const ast::Expr* e, const TyPtr& expected) {
        if (!e) return std::nullopt;

        // 字面量：后缀优先，其次期望类型，最后 i32
        if (auto* lit = dynamic_cast<const ast::Literal*>(e)) {
            if (lit->kind == ast::LiteralKind::Bool)
                return ConstValue{tyBool(), lit->text == "true" ? 1 : 0};

            auto parsed = parseInt(lit->text);
            if (!parsed) {
                error(lit->loc, "invalid integer literal");
                return std::nullopt;
            }

            TyPtr ty;
            if (!parsed->suffix.empty()) {
                ty = builtinIntTy(parsed->suffix);
                if (!ty) {
                    error(lit->loc, "unknown integer suffix");
                    return std::nullopt;
                }
            } else if (isInt(expected)) {
                ty = expected;
            } else {
                ty = tyInt("i32");
            }
            return ConstValue{ty, parsed->value};
        }

        if (auto* p = dynamic_cast<const ast::Path*>(e))
            return evalPath(p);

        // 一元运算
        if (auto* u = dynamic_cast<const ast::Unary*>(e)) {
            if (u->op == ast::UnaryOp::Neg) {
                auto v = eval(u->operand.get(), expected);
                if (!v) return std::nullopt;
                if (!isInt(v->type) || !v->type->isSigned) {
                    error(u->loc, "unary `-` requires a signed integer");
                    return std::nullopt;
                }
                return ConstValue{v->type, -v->value};
            }
            if (u->op == ast::UnaryOp::Not) {
                auto v = eval(u->operand.get(), expected);
                if (!v) return std::nullopt;
                if (isBool(v->type)) return ConstValue{tyBool(), v->value ? 0 : 1};
                if (isInt(v->type))  return ConstValue{v->type, ~v->value};
                error(u->loc, "invalid operand for `!`");
                return std::nullopt;
            }
            error(u->loc, "unsupported unary operator in constant");
            return std::nullopt;
        }

        // 二元运算：期望类型向两侧传播（const N: usize = 1 + 2 才成立）
        if (auto* b = dynamic_cast<const ast::Binary*>(e)) {
            auto l = eval(b->lhs.get(), expected);
            auto r = eval(b->rhs.get(), expected);
            if (!l || !r) return std::nullopt;

            if (!typeEquals(l->type, r->type)) {
                error(b->loc, "constant operand types must match");
                return std::nullopt;
            }

            if (isInt(l->type)) {
                switch (b->op) {
                    case ast::BinaryOp::Add: return ConstValue{l->type, l->value + r->value};
                    case ast::BinaryOp::Sub: return ConstValue{l->type, l->value - r->value};
                    case ast::BinaryOp::Mul: return ConstValue{l->type, l->value * r->value};
                    case ast::BinaryOp::Div:
                        if (r->value == 0) { error(b->loc, "division by zero"); return std::nullopt; }
                        return ConstValue{l->type, l->value / r->value};
                    case ast::BinaryOp::Rem:
                        if (r->value == 0) { error(b->loc, "division by zero"); return std::nullopt; }
                        return ConstValue{l->type, l->value % r->value};
                    default: break;
                }
            }

            if (isBool(l->type)) {
                switch (b->op) {
                    case ast::BinaryOp::And:    return ConstValue{tyBool(), (l->value && r->value) ? 1 : 0};
                    case ast::BinaryOp::Or:     return ConstValue{tyBool(), (l->value || r->value) ? 1 : 0};
                    case ast::BinaryOp::BitAnd: return ConstValue{tyBool(), (l->value & r->value) ? 1 : 0};
                    case ast::BinaryOp::BitOr:  return ConstValue{tyBool(), (l->value | r->value) ? 1 : 0};
                    case ast::BinaryOp::BitXor: return ConstValue{tyBool(), (l->value ^ r->value) ? 1 : 0};
                    default: break;
                }
            }

            error(b->loc, "unsupported binary operator in constant");
            return std::nullopt;
        }

        error(e->loc, "unsupported expression in constant");
        return std::nullopt;
    }
}