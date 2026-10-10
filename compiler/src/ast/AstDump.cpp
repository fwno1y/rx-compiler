#include "ast/AstDump.h"

#include <ostream>
#include <string>

namespace rx::ast {
namespace {

    const char* unaryName(UnaryOp op) {
        switch (op) {
            case UnaryOp::Neg: return "-";
            case UnaryOp::Not: return "!";
            case UnaryOp::DeRef: return "*";
            case UnaryOp::Ref: return "&";
            case UnaryOp::RefMut: return "&mut";
            case UnaryOp::RefRef: return "&&";
            case UnaryOp::RefRefMut: return "&&mut";
        }
        return "?";
    }

    const char* binaryName(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add: return "+";
            case BinaryOp::Sub: return "-";
            case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";
            case BinaryOp::Rem: return "%";
            case BinaryOp::Eq: return "==";
            case BinaryOp::Ne: return "!=";
            case BinaryOp::Lt: return "<";
            case BinaryOp::Le: return "<=";
            case BinaryOp::Gt: return ">";
            case BinaryOp::Ge: return ">=";
            case BinaryOp::And: return "&&";
            case BinaryOp::Or: return "||";
            case BinaryOp::BitAnd: return "&";
            case BinaryOp::BitOr: return "|";
            case BinaryOp::BitXor: return "^";
            case BinaryOp::Shl: return "<<";
            case BinaryOp::Shr: return ">>";
        }
        return "?";
    }

    const char* assignName(AssignOp op) {
        switch (op) {
            case AssignOp::Assign: return "=";
            case AssignOp::AddAssign: return "+=";
            case AssignOp::SubAssign: return "-=";
            case AssignOp::MulAssign: return "*=";
            case AssignOp::DivAssign: return "/=";
            case AssignOp::RemAssign: return "%=";
            case AssignOp::BitAndAssign: return "&=";
            case AssignOp::BitOrAssign: return "|=";
            case AssignOp::BitXorAssign: return "^=";
            case AssignOp::ShlAssign: return "<<=";
            case AssignOp::ShrAssign: return ">>=";
        }
        return "?";
    }

    std::string typeStr(const Type* t);

    std::string identPathStr(const std::vector<IdentSegment>& segs) {
        std::string s;
        for (size_t i = 0; i < segs.size(); ++i) {
            if (i) s += "::";
            s += segs[i].text;
        }
        return s;
    }

    std::string genericArgsStr(const GenericArgs& ga) {
        std::string s = "<";
        for (size_t i = 0; i < ga.args.size(); ++i) {
            if (i) s += ", ";
            if (auto* l = dynamic_cast<const LifetimeArg*>(ga.args[i].get())) {
                s += l->value.name;
            } else if (auto* t = dynamic_cast<const TypeArg*>(ga.args[i].get())) {
                s += typeStr(t->value.get());
            }
        }
        s += ">";
        return s;
    }

    std::string pathSegsStr(const std::vector<PathSegment>& segs) {
        std::string s;
        for (size_t i = 0; i < segs.size(); ++i) {
            if (i) s += "::";
            s += segs[i].ident.text;
            if (segs[i].generics) s += genericArgsStr(*segs[i].generics);
        }
        return s;
    }

    std::string constExprStr(const Expr* e) {
        if (auto* l = dynamic_cast<const Literal*>(e)) return l->text;
        if (auto* p = dynamic_cast<const Path*>(e)) return pathSegsStr(p->segments);
        if (auto* u = dynamic_cast<const Unary*>(e)) return "-" + constExprStr(u->operand.get());
        return "<const>";
    }

    std::string typeStr(const Type* t) {
        if (!t) return "()";
        if (auto* p = dynamic_cast<const TypePath*>(t)) return pathSegsStr(p->segments);
        if (auto* r = dynamic_cast<const RefType*>(t)) {
            std::string s = r->isDouble ? "&&" : "&";
            if (r->lifetime) s += r->lifetime->name + " ";
            if (r->isMut) s += "mut ";
            s += typeStr(r->target.get());
            return s;
        }
        if (auto* a = dynamic_cast<const ArrayType*>(t)) {
            return "[" + typeStr(a->element.get()) + "; " + constExprStr(a->length.get()) + "]";
        }
        if (dynamic_cast<const UnitType*>(t)) return "()";
        if (auto* p = dynamic_cast<const ParenType*>(t)) return "(" + typeStr(p->inner.get()) + ")";
        return "<type>";
    }

    std::string lifetimeBoundsStr(const LifetimeBounds& b) {
        std::string s;
        for (size_t i = 0; i < b.lifetimes.size(); ++i) {
            if (i) s += " + ";
            s += b.lifetimes[i].name;
        }
        return s;
    }

    std::string genericParamsStr(const GenericParams& gp) {
        std::string s = "<";
        for (size_t i = 0; i < gp.lifetimes.size(); ++i) {
            if (i) s += ", ";
            s += gp.lifetimes[i].lifetime.name;
            if (gp.lifetimes[i].bounds) s += ": " + lifetimeBoundsStr(*gp.lifetimes[i].bounds);
        }
        s += ">";
        return s;
    }

    std::string typeParamBoundsStr(const TypeParamBounds& b) {
        std::string s;
        for (size_t i = 0; i < b.lifetimes.size(); ++i) {
            if (i) s += " + ";
            s += b.lifetimes[i].name;
        }
        return s;
    }

    std::string whereStr(const WhereClause& wc) {
        std::string s;
        for (size_t i = 0; i < wc.items.size(); ++i) {
            if (i) s += ", ";
            auto* it = wc.items[i].get();
            if (auto* l = dynamic_cast<const LifetimeWhereItem*>(it)) {
                s += l->lifetime.name + ": " + lifetimeBoundsStr(l->bounds);
            } else if (auto* t = dynamic_cast<const TypeWhereItem*>(it)) {
                s += typeStr(t->type.get());
                if (t->bounds) s += ": " + typeParamBoundsStr(*t->bounds);
            }
        }
        return s;
    }

    std::string useTreeStr(const UseTree* t) {
        if (auto* p = dynamic_cast<const UseTreePath*>(t)) {
            std::string s = identPathStr(p->path);
            if (p->hasAlias) s += " as " + p->alias;
            if (p->aliasUnderscore) s += " as _";
            return s;
        }
        if (auto* g = dynamic_cast<const UseTreeGlobal*>(t)) {
            return (g->hasPrefix ? identPathStr(g->prefix) + "::" : std::string()) + "*";
        }
        if (auto* g = dynamic_cast<const UseTreeGroup*>(t)) {
            std::string s = g->hasPrefix ? identPathStr(g->prefix) + "::" : std::string();
            s += "{";
            for (size_t i = 0; i < g->items.size(); ++i) {
                if (i) s += ", ";
                s += useTreeStr(g->items[i].get());
            }
            s += "}";
            return s;
        }
        return "<use>";
    }

    std::string selfStr(const SelfParam& s) {
        std::string r;
        if (s.isRef) {
            r += s.isDoubleRef ? "&&" : "&";
            if (s.lifetime) r += s.lifetime->name + " ";
        }
        if (s.isMut) r += "mut ";
        r += "self";
        return r;
    }

    // 负责遍历并打印的类。只关心“结构”，类型/路径一律内联成字符串。
    class Dumper {
    public:
        explicit Dumper(std::ostream& os) : os_(os) {}

        void run(const Crate& c) {
            line("Crate");
            indent_++;
            for (auto& it : c.items) item(it.get());
            indent_--;
        }

    private:
        std::ostream& os_;
        int indent_ = 0;

        void pad() { for (int i = 0; i < indent_; ++i) os_ << "  "; }
        void line(const std::string& s) { pad(); os_ << s << "\n"; }

        // ---------------- 内联表达式 ----------------

        bool isInlineExpr(const Expr* e) {
            return dynamic_cast<const Literal*>(e) || dynamic_cast<const Path*>(e)
                || dynamic_cast<const UnitExpr*>(e) || dynamic_cast<const Continue*>(e);
        }

        std::string inlineExprStr(const Expr* e) {
            if (auto* l = dynamic_cast<const Literal*>(e))
                return std::string(l->kind == LiteralKind::Integer ? "Int " : "Bool ") + l->text;
            if (auto* p = dynamic_cast<const Path*>(e)) return "Path " + pathSegsStr(p->segments);
            if (dynamic_cast<const UnitExpr*>(e)) return "()";
            if (dynamic_cast<const Continue*>(e)) return "continue";
            return "<expr>";
        }

        // 简单表达式内联到同一行；复合表达式换行后递归。
        void labeledExpr(const std::string& label, const Expr* e) {
            if (!e) {
                line(label + ": <none>");
                return;
            }
            if (isInlineExpr(e)) {
                line(label + ": " + inlineExprStr(e));
                return;
            }
            line(label + ":");
            indent_++;
            expr(e);
            indent_--;
        }

        // ---------------- Item ----------------

        void item(const Item* it) {
            if (!it) {
                line("<item-null>");
                return;
            }
            if (auto* u = dynamic_cast<const UseDecl*>(it)) {
                line("UseDecl " + useTreeStr(u->tree.get()));
                return;
            }
            if (auto* f = dynamic_cast<const FunctionDef*>(it)) {
                std::string h = "FunctionDef " + f->name;
                if (f->generics) h += genericParamsStr(*f->generics);
                line(h);
                indent_++;
                if (f->params.selfParam) line(selfStr(*f->params.selfParam));
                for (auto& p : f->params.params)
                    line("param " + std::string(p.binding.isMut ? "mut " : "") + p.binding.name + ": " + typeStr(p.type.get()));
                line("return: " + typeStr(f->returnType.get()));
                if (f->where) line("where " + whereStr(*f->where));
                block(f->body.get());
                indent_--;
                return;
            }
            if (auto* s = dynamic_cast<const StructDef*>(it)) {
                std::string h = "StructDef " + s->name;
                if (s->generics) h += genericParamsStr(*s->generics);
                line(h);
                indent_++;
                for (auto& a : s->attributes) {
                    std::string d = "derive(";
                    for (size_t i = 0; i < a.derives.size(); ++i) {
                        if (i) d += ", ";
                        d += a.derives[i];
                    }
                    d += ")";
                    line(d);
                }
                for (auto& fld : s->fields) line("field " + fld.name + ": " + typeStr(fld.type.get()));
                if (s->where) line("where " + whereStr(*s->where));
                indent_--;
                return;
            }
            if (auto* c = dynamic_cast<const ConstItem*>(it)) {
                line("ConstItem " + c->name + ": " + typeStr(c->type.get()));
                indent_++;
                labeledExpr("value", c->value.get());
                indent_--;
                return;
            }
            if (auto* i = dynamic_cast<const Impl*>(it)) {
                std::string h = "Impl ";
                if (i->generics) h += genericParamsStr(*i->generics) + " ";
                h += typeStr(i->target.get());
                line(h);
                indent_++;
                if (i->where) line("where " + whereStr(*i->where));
                for (auto& sub : i->items) item(sub.get());
                indent_--;
                return;
            }
            line("<item>");
        }

        // ---------------- Stmt / Block ----------------

        void stmt(const Stmt* s) {
            if (!s) {
                line("<stmt-null>");
                return;
            }
            if (auto* l = dynamic_cast<const LetStmt*>(s)) {
                std::string h = "LetStmt " + std::string(l->binding.isMut ? "mut " : "") + l->binding.name;
                if (l->type) h += ": " + typeStr(l->type.get());
                line(h);
                indent_++;
                labeledExpr("init", l->init.get());
                indent_--;
                return;
            }
            if (auto* e = dynamic_cast<const ExprStmt*>(s)) {
                line(std::string("ExprStmt") + (e->hasSemi ? " ;" : ""));
                indent_++;
                labeledExpr("expr", e->expr.get());
                indent_--;
                return;
            }
            line("<stmt>");
        }

        void block(const Block* b) {
            if (!b) {
                line("<block-null>");
                return;
            }
            line("Block");
            indent_++;
            for (auto& s : b->stmts) stmt(s.get());
            if (b->tail) labeledExpr("tail", b->tail.get());
            indent_--;
        }

        // ---------------- Expr ----------------

        void expr(const Expr* e) {
            if (!e) {
                line("<expr-null>");
                return;
            }
            if (isInlineExpr(e)) {
                line(inlineExprStr(e));
                return;
            }
            if (auto* b = dynamic_cast<const Block*>(e)) {
                block(b);
                return;
            }
            if (auto* u = dynamic_cast<const Unary*>(e)) {
                line(std::string("Unary ") + unaryName(u->op));
                indent_++;
                labeledExpr("operand", u->operand.get());
                indent_--;
                return;
            }
            if (auto* b = dynamic_cast<const Binary*>(e)) {
                line(std::string("Binary ") + binaryName(b->op));
                indent_++;
                labeledExpr("lhs", b->lhs.get());
                labeledExpr("rhs", b->rhs.get());
                indent_--;
                return;
            }
            if (auto* a = dynamic_cast<const Assign*>(e)) {
                line(std::string("Assign ") + assignName(a->op));
                indent_++;
                labeledExpr("lhs", a->lhs.get());
                labeledExpr("rhs", a->rhs.get());
                indent_--;
                return;
            }
            if (auto* c = dynamic_cast<const Cast*>(e)) {
                line("Cast");
                indent_++;
                labeledExpr("operand", c->operand.get());
                line("as: " + typeStr(c->target.get()));
                indent_--;
                return;
            }
            if (auto* c = dynamic_cast<const Call*>(e)) {
                line("Call");
                indent_++;
                labeledExpr("callee", c->target.get());
                for (size_t i = 0; i < c->args.size(); ++i) labeledExpr("arg" + std::to_string(i), c->args[i].get());
                indent_--;
                return;
            }
            if (auto* m = dynamic_cast<const MethodCall*>(e)) {
                std::string h = "MethodCall ." + m->method.ident.text;
                if (m->method.generics) h += genericArgsStr(*m->method.generics);
                line(h);
                indent_++;
                labeledExpr("target", m->target.get());
                for (size_t i = 0; i < m->args.size(); ++i) labeledExpr("arg" + std::to_string(i), m->args[i].get());
                indent_--;
                return;
            }
            if (auto* f = dynamic_cast<const Field*>(e)) {
                line("Field ." + f->name);
                indent_++;
                labeledExpr("target", f->target.get());
                indent_--;
                return;
            }
            if (auto* idx = dynamic_cast<const Index*>(e)) {
                line("Index");
                indent_++;
                labeledExpr("target", idx->target.get());
                labeledExpr("index", idx->index.get());
                indent_--;
                return;
            }
            if (auto* s = dynamic_cast<const StructLit*>(e)) {
                line("StructLit " + pathSegsStr(s->path));
                indent_++;
                for (auto& f : s->fields) labeledExpr("field " + f.name, f.value.get());
                indent_--;
                return;
            }
            if (auto* a = dynamic_cast<const ArrayLit*>(e)) {
                line("ArrayLit");
                indent_++;
                for (size_t i = 0; i < a->elements.size(); ++i) labeledExpr("elem" + std::to_string(i), a->elements[i].get());
                indent_--;
                return;
            }
            if (auto* a = dynamic_cast<const ArrayRepeat*>(e)) {
                line("ArrayRepeat");
                indent_++;
                labeledExpr("element", a->element.get());
                labeledExpr("count", a->count.get());
                indent_--;
                return;
            }
            if (auto* i = dynamic_cast<const If*>(e)) {
                line("If");
                indent_++;
                labeledExpr("cond", i->condition.get());
                labeledExpr("then", i->thenBlock.get());
                if (i->elseBranch) labeledExpr("else", i->elseBranch.get());
                indent_--;
                return;
            }
            if (auto* l = dynamic_cast<const Loop*>(e)) {
                line("Loop");
                indent_++;
                labeledExpr("body", l->body.get());
                indent_--;
                return;
            }
            if (auto* w = dynamic_cast<const While*>(e)) {
                line("While");
                indent_++;
                labeledExpr("cond", w->condition.get());
                labeledExpr("body", w->body.get());
                indent_--;
                return;
            }
            if (auto* b = dynamic_cast<const Break*>(e)) {
                line("Break");
                if (b->value) {
                    indent_++;
                    labeledExpr("value", b->value.get());
                    indent_--;
                }
                return;
            }
            if (auto* r = dynamic_cast<const Return*>(e)) {
                line("Return");
                if (r->value) {
                    indent_++;
                    labeledExpr("value", r->value.get());
                    indent_--;
                }
                return;
            }
            line("<expr>");
        }
    };

} // namespace

void dumpCrate(std::ostream& os, const Crate& crate) {
    Dumper d(os);
    d.run(crate);
}

} // namespace rx::ast
