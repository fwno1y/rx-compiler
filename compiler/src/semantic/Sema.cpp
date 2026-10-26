#include "Sema.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_set>

namespace rx::sema {
    namespace {
        // 整数字面量后缀只有 i32/u32/isize/usize（Lexer.g4:127）
        std::string intLiteralSuffix(const std::string& text) {
            static const char* kSuffixes[] = { "isize", "usize", "i32", "u32" };
            for (auto* s : kSuffixes) {
                std::size_t n = std::strlen(s);
                if (text.size() > n && text.compare(text.size() - n, n, s) == 0)
                    return s;
            }
            return {};
        }
    }
    bool Sema::run() {
        declareBuiltins();
        collectItems();
        checkConstants();
        checkLayout();
        for (auto& item : crate_.items) {
            if (auto* f = dynamic_cast<const ast::FunctionDef*>(item.get())) {
                checkFunction(f);
            }
            else if (auto* impl = dynamic_cast<const ast::Impl*>(item.get())) {
                for (auto& it : impl->items) {
                    if (auto* m = dynamic_cast<const ast::FunctionDef*>(it.get())) {
                        checkFunction(m);
                    }
                }
            }
        }
        checkMain();
        return !diags_.hasError();
    }

    void Sema::declareBuiltins() {
        auto add = [this](const std::string& name, std::vector<TyPtr> params, TyPtr ret) {
            Symbol s;
            s.kind = Symbol::Kind::Builtin;
            s.type = tyFn(std::move(params), std::move(ret));
            symbols_.defineGlobal(name, std::move(s));
        };
        add("println_i32", { tyInt("i32")}, tyUnit());
        add("print_i32", {tyInt("i32")}, tyUnit());
        add("get_i32", {}, tyInt("i32"));
    }
    // 收集，先登记结构体名字，让所有类型名可见；
    // 再解析类型，解析字段类型、函数签名、常量类型、impl方法
    void Sema::collectItems() {
        for (auto& item : crate_.items) {
            if (auto* s = dynamic_cast<const ast::StructDef*>(item.get())) {
                collectStruct(s);
            }
        }
        for (auto& item : crate_.items) {
            if (auto* s = dynamic_cast<const ast::StructDef*>(item.get())) {
                resolveStruct(s);
            } else if (auto* f = dynamic_cast<const ast::FunctionDef*>(item.get())) {
                collectFunction(f);
            } else if (auto* c = dynamic_cast<const ast::ConstItem*>(item.get())) {
                collectConst(c);
            } else if (auto* impl = dynamic_cast<const ast::Impl*>(item.get())) {
                collectImpl(impl);
            }
        }
    }
    // 登记一个结构体：名字、字段名和占位类型
    void Sema::collectStruct(const ast::StructDef *s) {
        if (symbols_.hasStruct(s->name)) {
            error(s->loc, "duplicate definition of type '" + s->name + "'");
            return;
        }
        auto& info = symbols_.defineStruct(s->name);
        info.def = s;
        for (auto& fld : s->fields) {
            if (info.fieldTypes.count(fld.name)) {
                error(fld.loc, "duplicate field '" + fld.name + "'");
            }
            info.fieldOrder.push_back(fld.name);
            info.fieldTypes[fld.name] = nullptr;
        }
    }

    void Sema::resolveStruct(const ast::StructDef *s) {
        auto info = symbols_.findStruct(s->name);
        if (!info || info->def != s) {
            return;
        }
        for (auto& fld : s->fields) {
            info->fieldTypes[fld.name] = resolveType(fld.type.get());
        }
        for (auto& attr : s->attributes) {
            for (auto& d : attr.derives) {
                if (d == "Copy") {
                    info->deriveCopy = true;
                } else if (d == "Clone") {
                    info->deriveClone = true;
                } else if (d == "PartialEq") {
                    info->derivePartialEq = true;
                } else if (d == "Eq") {
                    info->deriveEq = true;
                }
            }
        }
    }

    void Sema::collectFunction(const ast::FunctionDef *f) {
        if (symbols_.hasGlobal(f->name)) {
            error(f->loc, "duplicate definition of '" + f->name + "'");
            return;
        }
        Symbol s;
        s.kind = Symbol::Kind::Function;
        s.type = resolveFunctionType(f);
        s.fn = f;
        symbols_.defineGlobal(f->name, std::move(s));
    }

    void Sema::collectConst(const ast::ConstItem *c) {
        if (symbols_.hasGlobal(c->name)) {
            error(c->loc, "duplicate definition of '" + c->name + "'");
            return;
        }
        Symbol s;
        s.kind = Symbol::Kind::Const;
        s.type = resolveType(c->type.get());
        s.constItem = c;
        symbols_.defineGlobal(c->name, std::move(s));
    }

    void Sema::collectImpl(const ast::Impl *impl) {
        auto* tp = dynamic_cast<const ast::TypePath*>(impl->target.get());
        if (!tp || tp->segments.empty()) {
            return;
        }
        const std::string& name = tp->segments.back().ident.text;
        auto* info = symbols_.findStruct(name);
        if (!info) {
            error(impl->loc, "impl of unknown type '" + name + "'");
            return;
        }
        for (auto& it : impl->items) {
            if (auto* m = dynamic_cast<const ast::FunctionDef*>(it.get())) {
                if (info->methods.count(m->name)) {
                    error(m->loc, "duplicate method '" + m->name + "'");
                    continue;
                }
                info->methods[m->name] = resolveFunctionType(m);
            } else if (auto* c = dynamic_cast<const ast::ConstItem*>(it.get())) {
                if (info->assocConsts.count(c->name)) {
                    error(c->loc, "duplicate associated constant '" + c->name + "'");
                    continue;
                }
                AssocConst rec;
                rec.def = c;
                rec.type = resolveType(c->type.get());
                info->assocConsts[c->name] = rec;
            }
        }
    }
    // 类型解析：AST的语法类型 → 语义类型 Ty
    TyPtr Sema::resolveType(const ast::Type *t) {
        if (!t) {
            return tyUnit();
        }
        if (dynamic_cast<const ast::UnitType*>(t)) {
            return tyUnit();
        }
        if (auto* p = dynamic_cast<const ast::ParenType*>(t)) {
            return resolveType(p->inner.get());
        }
        if (auto* r = dynamic_cast<const ast::RefType*>(t)) {
            TyPtr inner = resolveType(r->target.get());
            if (r->isDouble) {
                inner = tyRef(inner, r->isMut);
                return tyRef(inner, false);
            }
            return tyRef(inner, r->isMut);
        }
        if (auto* a = dynamic_cast<const ast::ArrayType*>(t)) {
            auto len = constEval_.evalUsize(a->length.get(), a->loc);
            return tyArray(resolveType(a->element.get()),
                           len ? static_cast<std::size_t>(*len) : 0);
        }
        if (auto* tp = dynamic_cast<const ast::TypePath*>(t)) {
            return resolveNamedType(tp);
        }
        return tyError();
    }
    // 解析命名类型：内建整型 → bool → Self → 结构体 → 报错
    TyPtr Sema::resolveNamedType(const ast::TypePath *tp) {
        if (!tp || tp->segments.empty()) {
            return tyError();
        }
        const std::string& name = tp->segments.back().ident.text;
        if (auto t = builtinIntTy(name)) {
            return t;
        }
        if (name == "bool") {
            return tyBool();
        }
        if (name == "Self") {
            return tyError();
        }
        if (name == "Box" || name == "Vec") {
            const auto& seg = tp->segments.back();
            TyPtr elemTy;
            if (seg.generics) {
                for (const auto& ga : seg.generics->args) {
                    if (auto* ta = dynamic_cast<const ast::TypeArg*>(ga.get())) {
                        if (elemTy) {
                            error(tp->loc, name + " accepts exactly one type argument");
                            return tyError();
                        }
                        elemTy = resolveType(ta->value.get());
                    }
                }
            }
            if (!elemTy) {
                error(tp->loc, name + " requires a type argument");
                return tyError();
            }
            return name == "Box" ? tyBox(elemTy) : tyVec(elemTy);
        }
        if (symbols_.hasStruct(name)) {
            return tyStruct(name);
        }
        error(tp->loc, "unknown type '" + name + "'");
        return tyError();
    }
    // 由函数定义构造 Ty::Fn。self 不计入 params（方法调用单独处理接收者）
    TyPtr Sema::resolveFunctionType(const ast::FunctionDef *f) {
        std::vector<TyPtr> params;
        for (auto& p : f->params.params) {
            params.push_back(resolveType(p.type.get()));
        }
        TyPtr ret = f->returnType ? resolveType(f->returnType.get()) : tyUnit();
        return tyFn(std::move(params), ret);
    }

    void Sema::checkFunction(const ast::FunctionDef *f) {
        symbols_.pushScope();
        TyPtr savedReturn = curReturn_;
        int savedLoop = loopDepth_;
        curReturn_ = f->returnType ? resolveType(f->returnType.get()) : tyUnit();
        loopDepth_ = 0;
        for (auto& p : f->params.params) {
            Symbol s;
            s.kind = Symbol::Kind::Param;
            s.type = resolveType(p.type.get());
            s.isMut = p.binding.isMut;
            symbols_.declareLocal(p.binding.name, std::move(s));
        }
        checkBlock(f->body.get(), false);
        loopDepth_ = savedLoop;
        curReturn_ = savedReturn;
        symbols_.popScope();
    }

    TyPtr Sema::checkBlock(const ast::Block *b, bool createsScope) {
        if (!b) {
            return tyUnit();
        }
        if (createsScope) {
            symbols_.pushScope();
        }
        for (auto& s : b->stmts) {
            checkStmt(s.get());
        }
        TyPtr res = b->tail ? checkExpr(b->tail.get()) : tyUnit();
        if (createsScope) {
            symbols_.popScope();
        }
        return res;
    }

    void Sema::checkStmt(const ast::Stmt *s) {
        if (!s) {
            return;
        }
        if (auto* l = dynamic_cast<const ast::LetStmt*>(s)) {
            TyPtr declared = l->type ? resolveType(l->type.get()) : nullptr;
            TyPtr init = l->init ? checkExpr(l->init.get()) : tyUnit();
            if (declared && l->init && !typeEquals(declared, init)) {
                error(l->loc, "type mismatch in let initializer");
            }
            Symbol sym;
            sym.kind = Symbol::Kind::Local;
            sym.isMut = l->binding.isMut;
            sym.type = declared ? declared : init;
            symbols_.declareLocal(l->binding.name, std::move(sym));
            return;
        }
        if (auto* es = dynamic_cast<const ast::ExprStmt*>(s)) {
            checkExpr(es->expr.get());
            return;
        }
    }

    void Sema::checkMain() {
        Symbol* main = symbols_.findGlobal("main");
        if (!main || main->kind != Symbol::Kind::Function) {
            error(crate_.loc, "an executable needs a top-level 'main' function");
            return;
        }
        const ast::FunctionDef* f = main->fn;
        if (f->generics.has_value()) {
            error(f->loc, "'main' cannot have generic parameters");
        }
        if (f->params.selfParam.has_value() || !f->params.params.empty()) {
            error(f->loc, "'main' cannot have parameters");
        }
        TyPtr ret = (main->type && main->type->kind == Ty::Kind::Fn) ? main->type->ret : tyError();
        if (!isUnit(ret) && !isNever(ret)) {
            error(f->loc, "'main' must return '()'");
        }
    }
    TyPtr Sema::checkExpr(const ast::Expr* e) {
        if (!e) {
            return tyUnit();
        }
        if (auto* n = dynamic_cast<const ast::Literal*>(e)) {
            return checkLiteral(n);
        }
        if (auto* n = dynamic_cast<const ast::Path*>(e)) {
            return checkPath(n);
        }
        if (auto* n = dynamic_cast<const ast::Binary*>(e)) {
            return checkBinary(n);
        }
        if (auto* n = dynamic_cast<const ast::Unary*>(e)) {
            return checkUnary(n);
        }
        if (auto* n = dynamic_cast<const ast::Assign*>(e)) {
            return checkAssign(n);
        }
        if (auto* n = dynamic_cast<const ast::Cast*>(e)) {
            return checkCast(n);
        }
        if (auto* n = dynamic_cast<const ast::Call*>(e)) {
            return checkCall(n);
        }
        if (auto* n = dynamic_cast<const ast::MethodCall*>(e)) {
            return checkMethodCall(n);
        }
        if (auto* n = dynamic_cast<const ast::Field*>(e)) {
            return checkField(n);
        }
        if (auto* n = dynamic_cast<const ast::Index*>(e)) {
            return checkIndex(n);
        }
        if (auto* n = dynamic_cast<const ast::Block*>(e)) {
            return checkBlock(n, true);
        }
        if (auto* n = dynamic_cast<const ast::If*>(e)) {
            return checkIf(n);
        }
        if (auto* n = dynamic_cast<const ast::Loop*>(e)) {
            return checkLoop(n);
        }
        if (auto* n = dynamic_cast<const ast::While*>(e)) {
            return checkWhile(n);
        }
        if (auto* n = dynamic_cast<const ast::Return*>(e)) {
            return checkReturn(n);
        }
        if (auto* n = dynamic_cast<const ast::Break*>(e)) {
            return checkBreak(n);
        }
        if (dynamic_cast<const ast::Continue*>(e)) {
            if (loopDepth_ == 0) {
                error(e->loc, "`continue` outside loop");
            }
            return tyNever();
        }
        if (auto* n = dynamic_cast<const ast::StructLit*>(e)) {
            return checkStructLit(n);
        }
        if (auto* n = dynamic_cast<const ast::ArrayLit*>(e)) {
            return checkArrayLit(n);
        }
        if (auto* n = dynamic_cast<const ast::ArrayRepeat*>(e)) {
            return checkArrayRepeat(n);
        }
        if (dynamic_cast<const ast::UnitExpr*>(e)) {
            return tyUnit();
        }
        return tyError();
    }

    TyPtr Sema::checkLiteral(const ast::Literal* n) {
        if (n->kind == ast::LiteralKind::Bool) {
            return tyBool();
        }
        std::string suffix = intLiteralSuffix(n->text);
        if (!suffix.empty()) {
            if (auto t = builtinIntTy(suffix)) {
                return t;
            }
        }
        return tyInt("i32");
    }

    TyPtr Sema::checkPath(const ast::Path* n) {
        if (n->segments.empty()) {
            return tyError();
        }
        if (n->segments.size() == 1) {
            Symbol* sym = symbols_.lookup(n->segments[0].ident.text);
            if (!sym) {
                error(n->loc, "unresolved name `" + n->segments[0].ident.text + "`");
                return tyError();
            }
            return sym->type ? sym->type : tyError();
        }
        if (n->segments.size() == 2) {
            const std::string& typeName = n->segments[0].ident.text;
            const std::string& name = n->segments[1].ident.text;
            auto* info = symbols_.findStruct(typeName);
            if (!info) {
                error(n->loc, "unknown type `" + typeName + "`");
                return tyError();
            }
            auto it = info->assocConsts.find(name);
            if (it == info->assocConsts.end()) {
                error(n->loc, "no associated constant `" + name + "`");
                return tyError();
            }
            return it->second.type ? it->second.type : tyError();
        }
        error(n->loc, "qualified path is not supported yet");
        return tyError();
    }

    TyPtr Sema::checkBinary(const ast::Binary* n) {
        TyPtr l = checkExpr(n->lhs.get());
        TyPtr r = checkExpr(n->rhs.get());
        if (isError(l) || isError(r)) {
            return tyError();
        }
        using BO = ast::BinaryOp;
        switch (n->op) {
            case BO::Add: case BO::Sub: case BO::Mul:
            case BO::Div: case BO::Rem:
            case BO::BitAnd: case BO::BitOr: case BO::BitXor:
                if (!isInt(l) || !isInt(r)) {
                    error(n->loc, "arithmetic/bitwise operator requires integers");
                    return tyError();
                }
                if (l->name != r->name) {
                    error(n->loc, "operand types must match");
                    return tyError();
                }
                return l;

            case BO::Shl: case BO::Shr:
                if (!isInt(l) || !isInt(r)) {
                    error(n->loc, "shift requires integers");
                    return tyError();
                }
                return l;

            case BO::And: case BO::Or:
                if (!isBool(l) || !isBool(r)) {
                    error(n->loc, "logical operator requires bool");
                    return tyError();
                }
                return tyBool();

            case BO::Eq: case BO::Ne:
                if (!typeEquals(l, r)) {
                    error(n->loc, "comparison operand types must match");
                    return tyError();
                }
                return tyBool();

            case BO::Lt: case BO::Le: case BO::Gt: case BO::Ge:
                if (!isInt(l) || !isInt(r)) {
                    error(n->loc, "ordering comparison requires integers");
                    return tyError();
                }
                if (l->name != r->name) {
                    error(n->loc, "comparison operand types must match");
                    return tyError();
                }
                return tyBool();
        }
        return tyError();
    }

    TyPtr Sema::checkUnary(const ast::Unary* n) {
        TyPtr t = checkExpr(n->operand.get());
        if (isError(t)) {
            return tyError();
        }
        switch (n->op) {
            case ast::UnaryOp::Neg:
                if (!isInt(t) || !t->isSigned) {
                    error(n->loc, "unary `-` requires a signed integer");
                    return tyError();
                }
                return t;
            case ast::UnaryOp::Not:
                if (isBool(t) || isInt(t)) return t;
                error(n->loc, "invalid operand for `!`");
                return tyError();
            case ast::UnaryOp::DeRef:
                if (t->kind != Ty::Kind::Ref) {
                    error(n->loc, "cannot dereference a non-reference");
                    return tyError();
                }
                return t->refTo ? t->refTo : tyError();
            case ast::UnaryOp::Ref:
            case ast::UnaryOp::RefMut:
            case ast::UnaryOp::RefRef:
            case ast::UnaryOp::RefRefMut: {
                bool isMut = n->op == ast::UnaryOp::RefMut
                          || n->op == ast::UnaryOp::RefRefMut;
                bool isDouble = n->op == ast::UnaryOp::RefRef
                             || n->op == ast::UnaryOp::RefRefMut;
                if (isDouble) {
                    TyPtr inner = tyRef(t, isMut);
                    return tyRef(inner, /*isMut=*/false);
                }
                return tyRef(t, isMut);
            }
        }
        return tyError();
    }

    TyPtr Sema::checkAssign(const ast::Assign* n) {
        TyPtr l = checkExpr(n->lhs.get());
        TyPtr r = checkExpr(n->rhs.get());
        if (!isPlace(n->lhs.get())) {
            error(n->loc, "left-hand side is not an assignment place");
            return tyUnit();
        }
        if (!isMutablePlace(n->lhs.get()))
            error(n->loc, "cannot assign to an immutable place");
        if (isError(l) || isError(r)) {
            return tyUnit();
        }
        if (n->op == ast::AssignOp::Assign) {
            if (!typeEquals(l, r))
                error(n->loc, "assignment type mismatch");
        } else {
            if (!isInt(l) && !isBool(l))
                error(n->loc, "compound assignment requires an integer or bool");
            else if (!typeEquals(l, r))
                error(n->loc, "compound assignment type mismatch");
        }
        return tyUnit();
    }

    TyPtr Sema::checkCast(const ast::Cast* n) {
        TyPtr from = checkExpr(n->operand.get());
        TyPtr to = resolveType(n->target.get());
        if (isError(from) || isError(to)) {
            return to;
        }
        if (isInt(to)) {
            if (isInt(from) || isBool(from)) return to;
            error(n->loc, "invalid cast to integer");
            return tyError();
        }
        if (isBool(to)) {
            if (isBool(from)) return to;
            error(n->loc, "integer-to-bool cast is unsupported");
            return tyError();
        }
        error(n->loc, "unsupported cast target");
        return tyError();
    }

    TyPtr Sema::checkCall(const ast::Call* n) {
        TyPtr ft = checkExpr(n->target.get());
        std::vector<TyPtr> args;
        args.reserve(n->args.size());
        for (auto& a : n->args) {
            args.push_back(checkExpr(a.get()));
        }
        if (isError(ft)) {
            return tyError();
        }
        if (ft->kind != Ty::Kind::Fn) {
            error(n->loc, "callee is not callable");
            return tyError();
        }
        if (args.size() != ft->params.size()) {
            error(n->loc, "wrong number of arguments");
            return ft->ret ? ft->ret : tyUnit();
        }
        for (std::size_t i = 0; i < args.size(); ++i)
            if (!typeEquals(args[i], ft->params[i]))
                error(n->loc, "argument type mismatch");
        return ft->ret ? ft->ret : tyUnit();
    }

    TyPtr Sema::checkMethodCall(const ast::MethodCall* n) {
        TyPtr base = checkExpr(n->target.get());
        std::vector<TyPtr> args;
        args.reserve(n->args.size());
        for (auto& a : n->args) {
            args.push_back(checkExpr(a.get()));
        }
        TyPtr recv = base;
        while (recv && recv->kind == Ty::Kind::Ref) recv = recv->refTo;
        if (isError(recv) || !recv || recv->kind != Ty::Kind::Struct) {
            error(n->loc, "method call on a non-struct value");
            return tyError();
        }
        auto* info = symbols_.findStruct(recv->name);
        if (!info) {
            return tyError();
        }
        auto it = info->methods.find(n->method.ident.text);
        if (it == info->methods.end()) {
            error(n->loc, "no method `" + n->method.ident.text + "`");
            return tyError();
        }
        TyPtr ft = it->second;
        if (!ft || ft->kind != Ty::Kind::Fn) {
            return tyError();
        }
        if (args.size() != ft->params.size()) {
            error(n->loc, "wrong number of arguments");
        }
        else {
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (!typeEquals(args[i], ft->params[i])) {
                    error(n->loc, "argument type mismatch");
                }
            }
        }
        return ft->ret ? ft->ret : tyUnit();
    }

    TyPtr Sema::checkField(const ast::Field* n) {
        TyPtr base = checkExpr(n->target.get());
        while (base && base->kind == Ty::Kind::Ref) {
            base = base->refTo;
        }
        if (isError(base)) {
            return tyError();
        }
        if (!base || base->kind != Ty::Kind::Struct) {
            error(n->loc, "field access on a non-struct value");
            return tyError();
        }
        auto* info = symbols_.findStruct(base->name);
        if (!info) {
            return tyError();
        }
        auto it = info->fieldTypes.find(n->name);
        if (it == info->fieldTypes.end()) {
            error(n->loc, "unknown field `" + n->name + "`");
            return tyError();
        }
        return it->second ? it->second : tyError();
    }

    TyPtr Sema::checkIndex(const ast::Index* n) {
        TyPtr base = checkExpr(n->target.get());
        TyPtr idx = checkExpr(n->index.get());
        while (base && base->kind == Ty::Kind::Ref) {
            base = base->refTo;
        }
        if (!isInt(idx) && !isError(idx)) {
            error(n->index->loc, "array index must be an integer");
        }
        if (base && base->kind == Ty::Kind::Array) {
            return base->element ? base->element : tyError();
        }
        if (!isError(base)) {
            error(n->loc, "cannot index a non-array value");
        }
        return tyError();
    }

    TyPtr Sema::checkIf(const ast::If* n) {
        TyPtr cond = checkExpr(n->condition.get());
        if (!isBool(cond) && !isError(cond)) {
            error(n->condition->loc, "if condition must be bool");
        }
        TyPtr thenTy = checkBlock(n->thenBlock.get(), true);
        if (n->elseBranch) {
            TyPtr elseTy = checkExpr(n->elseBranch.get());
            if (isNever(thenTy)) {
                return elseTy;
            }
            if (isNever(elseTy)) {
                return thenTy;
            }
            if (!typeEquals(thenTy, elseTy)) {
                error(n->loc, "if/else branch types differ");
            }
            return thenTy;
        }
        if (!isUnit(thenTy) && !isNever(thenTy)) {
            error(n->loc, "if without else must have unit type");
        }
        return tyUnit();
    }

    TyPtr Sema::checkLoop(const ast::Loop* n) {
        ++loopDepth_;
        TyPtr body = checkBlock(n->body.get(), true);
        --loopDepth_;
        return body;
    }

    TyPtr Sema::checkWhile(const ast::While* n) {
        TyPtr cond = checkExpr(n->condition.get());
        if (!isBool(cond) && !isError(cond)) {
            error(n->condition->loc, "while condition must be bool");
        }
        ++loopDepth_;
        checkBlock(n->body.get(), true);
        --loopDepth_;
        return tyUnit();
    }

    TyPtr Sema::checkReturn(const ast::Return* n) {
        TyPtr t = n->value ? checkExpr(n->value.get()) : tyUnit();
        if (curReturn_ && !isNever(t) && !typeEquals(t, curReturn_)) {
            error(n->loc, "return type mismatch");
        }
        return tyNever();
    }

    TyPtr Sema::checkBreak(const ast::Break* n) {
        if (loopDepth_ == 0) {
            error(n->loc, "`break` outside loop");
        }
        if (n->value) {
            checkExpr(n->value.get());
        }
        return tyNever();
    }

    TyPtr Sema::checkStructLit(const ast::StructLit* n) {
        if (n->path.empty()) {
            return tyError();
        }
        const std::string& name = n->path.back().ident.text;
        auto* info = symbols_.findStruct(name);
        if (!info) {
            error(n->loc, "unknown struct `" + name + "`");
            return tyError();
        }
        std::unordered_set<std::string> seen;
        for (auto& fi : n->fields) {
            if (!seen.insert(fi.name).second) {
                error(fi.loc, "duplicate initializer field `" + fi.name + "`");
            }
            TyPtr vt = checkExpr(fi.value.get());
            auto it = info->fieldTypes.find(fi.name);
            if (it == info->fieldTypes.end()) {
                error(fi.loc, "unknown initializer field `" + fi.name + "`");
                continue;
            }
            if (it->second && !typeEquals(it->second, vt)) {
                error(fi.loc, "field `" + fi.name + "` has the wrong type");
            }
        }
        for (auto& fname : info->fieldOrder) {
            if (!seen.count(fname)) {
                error(n->loc, "missing initializer field `" + fname + "`");
            }
        }
        return tyStruct(name);
    }

    TyPtr Sema::checkArrayLit(const ast::ArrayLit* n) {
        if (n->elements.empty()) {
            return tyError();
        }
        TyPtr elem = checkExpr(n->elements.front().get());
        for (std::size_t i = 1; i < n->elements.size(); ++i) {
            TyPtr t = checkExpr(n->elements[i].get());
            if (!typeEquals(elem, t)) {
                error(n->elements[i]->loc, "array elements have different types");
            }
        }
        return tyArray(elem, static_cast<std::int64_t>(n->elements.size()));
    }

    TyPtr Sema::checkArrayRepeat(const ast::ArrayRepeat* n) {
        TyPtr elem = checkExpr(n->element.get());
        auto len = constEval_.evalUsize(n->count.get(), n->count->loc);
        return tyArray(elem, len ? static_cast<std::size_t>(*len) : 0);
    }

    void Sema::checkConstants() {
        // 先确定所有全局常量（前向引用靠 evalRef 按需递归）
        std::vector<std::string> globalNames;
        for (auto& entry : symbols_.globals()) {
            if (entry.second.kind == Symbol::Kind::Const) {
                globalNames.push_back(entry.first);
            }
        }
        for (auto& name : globalNames) {
            constEval_.evalGlobal(name);
        }
        // 再逐个 impl 设置 Self 上下文并检查关联常量
        for (auto& entry : symbols_.structs()) {
            constEval_.setSelfType(entry.first);
            std::vector<std::string> names;
            for (auto& c : entry.second.assocConsts) {
                names.push_back(c.first);
            }
            for (auto& name : names) {
                constEval_.evalAssoc(entry.first, name);
            }
        }
        constEval_.setSelfType("");
    }

    void Sema::checkLayout() {
        Layout layout(symbols_, diags_);
        layout.check();
    }

    bool Sema::isPlace(const ast::Expr* e) {
        if (!e) {
            return false;
        }
        if (auto* p = dynamic_cast<const ast::Path*>(e)) {
            if (p->segments.size() != 1) return false;
            Symbol* s = symbols_.lookup(p->segments[0].ident.text);
            return s && (s->kind == Symbol::Kind::Local || s->kind == Symbol::Kind::Param);
        }
        if (auto* f = dynamic_cast<const ast::Field*>(e)) {
            return isPlace(f->target.get());
        }
        if (auto* i = dynamic_cast<const ast::Index*>(e)) {
            return isPlace(i->target.get());
        }
        if (auto* u = dynamic_cast<const ast::Unary*>(e)) {
            return u->op == ast::UnaryOp::DeRef;
        }
        return false;
    }

    bool Sema::isMutablePlace(const ast::Expr* e) {
        if (!e) {
            return false;
        }
        if (auto* p = dynamic_cast<const ast::Path*>(e)) {
            if (p->segments.size() != 1) return false;
            Symbol* s = symbols_.lookup(p->segments[0].ident.text);
            return s
                && (s->kind == Symbol::Kind::Local || s->kind == Symbol::Kind::Param)
                && s->isMut;
        }
        if (auto* f = dynamic_cast<const ast::Field*>(e)) {
            return isMutablePlace(f->target.get());
        }
        if (auto* i = dynamic_cast<const ast::Index*>(e)) {
            return isMutablePlace(i->target.get());
        }
        if (auto* u = dynamic_cast<const ast::Unary*>(e)) {
            if (u->op != ast::UnaryOp::DeRef) {
                return false;
            }
            TyPtr t = checkExpr(u->operand.get());
            return t && t->kind == Ty::Kind::Ref && t->refMut;
        }
        return false;
    }
}