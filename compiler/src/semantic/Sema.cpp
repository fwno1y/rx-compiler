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
        traits_.checkDerives();
        for (auto& item : crate_.items) {
            curSelfType_.clear();
            if (auto* f = dynamic_cast<const ast::FunctionDef*>(item.get())) {
                checkFunction(f);
            }
            else if (auto* impl = dynamic_cast<const ast::Impl*>(item.get())) {
                auto* tp = dynamic_cast<const ast::TypePath*>(impl->target.get());
                if (tp && !tp->segments.empty()) {
                    curSelfType_ = tp->segments.back().ident.text;
                }
                for (auto& it : impl->items) {
                    if (auto* m = dynamic_cast<const ast::FunctionDef*>(it.get())) {
                        checkFunction(m);
                    }
                }
                curSelfType_.clear();
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
        // 结构体字段里的 Self 指这个结构体
        curSelfType_ = s->name;
        for (auto& fld : s->fields) {
            info->fieldTypes[fld.name] = resolveType(fld.type.get());
        }
        curSelfType_.clear();
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
        // impl 里的 Self 指目标结构体
        std::string savedSelf = curSelfType_;
        curSelfType_ = name;

        for (auto& it : impl->items) {
            if (auto* m = dynamic_cast<const ast::FunctionDef*>(it.get())) {
                if (info->methods.count(m->name) ||
                    info->methodReceivers.count(m->name) ||
                    info->assocConsts.count(m->name)) {
                    error(m->loc, "duplicate associated item '" + m->name + "'");
                    continue;
                }
                ReceiverKind rk = ReceiverKind::None;
                if (m->params.selfParam) {
                    const auto& sp = *m->params.selfParam;
                    if (sp.isRef) {
                        rk = sp.isMut ? ReceiverKind::RefMut : ReceiverKind::Ref;
                    } else {
                        rk = ReceiverKind::Value;
                    }
                }
                info->methods[m->name] = resolveFunctionType(m);
                info->methodReceivers[m->name] = rk;
            } else if (auto* c = dynamic_cast<const ast::ConstItem*>(it.get())) {
                if (info->assocConsts.count(c->name) ||
                    info->methods.count(c->name) ||
                    info->methodReceivers.count(c->name)) {
                    error(c->loc, "duplicate associated item '" + c->name + "'");
                    continue;
                }
                AssocConst rec;
                rec.def = c;
                rec.type = resolveType(c->type.get());
                info->assocConsts[c->name] = rec;
            }
        }

        curSelfType_ = savedSelf;
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
            if (!curSelfType_.empty()) {
                return tyStruct(curSelfType_);
            }
            error(tp->loc, "`Self` is not available here");
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
        auto savedLoop = loops;
        curReturn_ = f->returnType ? resolveType(f->returnType.get()) : tyUnit();
        loops.clear();

        // 方法接收者 self
        if (f->params.selfParam) {
            const auto& sp = *f->params.selfParam;
            TyPtr base = curSelfType_.empty() ? tyError() : tyStruct(curSelfType_);
            TyPtr selfTy = base;
            if (sp.isDoubleRef) {
                selfTy = tyRef(tyRef(base, sp.isMut), false);
            } else if (sp.isRef) {
                selfTy = tyRef(base, sp.isMut);
            }
            Symbol s;
            s.kind = Symbol::Kind::Param;
            s.type = selfTy;
            s.isMut = sp.isMut && !sp.isRef;   // mut self（按值）
            symbols_.declareLocal("self", std::move(s));
        }

        for (auto& p : f->params.params) {
            Symbol s;
            s.kind = Symbol::Kind::Param;
            s.type = resolveType(p.type.get());
            s.isMut = p.binding.isMut;
            symbols_.declareLocal(p.binding.name, std::move(s));
        }
        TyPtr bodyTy = checkBlock(f->body.get(), false, curReturn_);
        // 函数体的值必须匹配返回类型；返回 ! 的函数不需要匹配
        if (!isNever(bodyTy) && !typeEquals(bodyTy, curReturn_)) {
            error(f->body->loc, "function body type does not match its return type");
        }
        loops = savedLoop;
        curReturn_ = savedReturn;
        symbols_.popScope();
    }

    TyPtr Sema::checkBlock(const ast::Block *b, bool createsScope, TyPtr expected) {
        if (!b) {
            return tyUnit();
        }
        if (createsScope) {
            symbols_.pushScope();
        }
        bool diverged = false;
        TyPtr implicitTail;

        const std::size_t n = b->stmts.size();
        for (std::size_t i = 0; i < n; ++i) {
            auto* es = dynamic_cast<const ast::ExprStmt*>(b->stmts[i].get());
            // 文法有两种“块尾”：显式 tail，或末尾一条不带分号的表达式语句
            bool isImplicitTail = (i + 1 == n) && !b->tail && es && !es->hasSemi;
            if (isImplicitTail) {
                implicitTail = checkExpr(es->expr.get(), expected);
                if (isNever(implicitTail)) {
                    diverged = true;
                }
                continue;              // 不再走 checkStmt 的“必须 unit”规则
            }
            TyPtr stmtTy = checkStmt(b->stmts[i].get());
            if (isNever(stmtTy)) {
                diverged = true;
            }
        }
        TyPtr tailTy = b->tail ? checkExpr(b->tail.get(), expected) : implicitTail;
        TyPtr result = diverged ? tyNever() : (tailTy ? tailTy : tyUnit());
        if (createsScope) {
            symbols_.popScope();
        }
        return result;
    }

    TyPtr Sema::checkStmt(const ast::Stmt *s) {
        if (!s) {
            return tyUnit();
        }
        if (auto* l = dynamic_cast<const ast::LetStmt*>(s)) {
            TyPtr declared = l->type ? resolveType(l->type.get()) : nullptr;
            TyPtr init = l->init ? checkExpr(l->init.get(), declared) : tyUnit();
            bool sharedTarget = declared && declared->kind == Ty::Kind::Ref &&
                                !declared->refMut;
            if (l->init && !sharedTarget &&
                isImmutableContainerAccess(l->init.get())) {
                error(l->init->loc, "cannot move out of an immutable container index");
            }
            if (declared && l->init && !canCoerce(init, declared)) {
                error(l->loc, "type mismatch in let initializer");
            }
            Symbol sym;
            sym.kind = Symbol::Kind::Local;
            sym.isMut = l->binding.isMut;
            sym.type = declared ? declared : init;
            symbols_.declareLocal(l->binding.name, std::move(sym));
            return tyUnit();
        }
        if (auto* es = dynamic_cast<const ast::ExprStmt*>(s)) {
            TyPtr t = checkExpr(es->expr.get());
            // return / break / continue：语句本身类型是 !
            if (isNever(t)) {
                return tyNever();
            }
            // 块内非末尾语句若没有分号，值必须是 ()
            if (!es->hasSemi && !isUnit(t) && !isError(t)) {
                error(es->loc,
                      "non-final block statement without semicolon must have unit type");
            }
            return es->hasSemi ? tyUnit() : t;
        }
        return tyUnit();
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
    TyPtr Sema::checkExpr(const ast::Expr* e, TyPtr expected) {
        if (!e) {
            return tyUnit();
        }
        if (auto* n = dynamic_cast<const ast::Literal*>(e)) {
            return checkLiteral(n, expected);
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
            return checkBlock(n, true, expected);
        }
        if (auto* n = dynamic_cast<const ast::If*>(e)) {
            return checkIf(n, expected);
        }
        if (auto* n = dynamic_cast<const ast::Loop*>(e)) {
            return checkLoop(n, expected);
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
            if (loops.empty()) {
                error(e->loc, "`continue` outside loop");
            }
            return tyNever();
        }
        if (auto* n = dynamic_cast<const ast::StructLit*>(e)) {
            return checkStructLit(n);
        }
        if (auto* n = dynamic_cast<const ast::ArrayLit*>(e)) {
            return checkArrayLit(n, expected);
        }
        if (auto* n = dynamic_cast<const ast::ArrayRepeat*>(e)) {
            return checkArrayRepeat(n, expected);
        }
        if (dynamic_cast<const ast::UnitExpr*>(e)) {
            return tyUnit();
        }
        return tyError();
    }

    TyPtr Sema::checkLiteral(const ast::Literal* n, const TyPtr& expected) {
        if (n->kind == ast::LiteralKind::Bool) {
            return tyBool();
        }
        std::string suffix = intLiteralSuffix(n->text);
        if (!suffix.empty()) {
            if (auto t = builtinIntTy(suffix)) {
                return t;
            }
        }
        // 无后缀：优先用期望的整型，否则默认 i32
        if (isInt(expected)) {
            return expected;
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
            std::string typeName = n->segments[0].ident.text;
            const std::string& name = n->segments[1].ident.text;
            if (typeName == "Self") {
                typeName = curSelfType_;
            }
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
                if (!traits_.isPartialEq(l)) {
                    error(n->loc, "type does not implement PartialEq");
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
                if (t->kind == Ty::Kind::Ref) {
                    return t->refTo ? t->refTo : tyError();
                }
                if (t->kind == Ty::Kind::Box) {
                    return t->element ? t->element : tyError();
                }
                error(n->loc, "cannot dereference a non-reference");
                return tyError();
            case ast::UnaryOp::Ref:
            case ast::UnaryOp::RefMut:
            case ast::UnaryOp::RefRef:
            case ast::UnaryOp::RefRefMut: {
                bool isMut = n->op == ast::UnaryOp::RefMut
                          || n->op == ast::UnaryOp::RefRefMut;
            bool isDouble = n->op == ast::UnaryOp::RefRef
                         || n->op == ast::UnaryOp::RefRefMut;

            // &mut 只能借可变占位；字面量等右值允许临时物化（&mut 20）
            if (isMut && isPlace(n->operand.get()) &&
                !isMutablePlace(n->operand.get())) {
                error(n->loc, "cannot mutably borrow an immutable place");
            }

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
        TyPtr r = checkExpr(n->rhs.get(), l);
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
            using AO = ast::AssignOp;

            // 复合赋值允许右值带一层共享引用：x -= &2
            TyPtr rv = r;
            if (rv && rv->kind == Ty::Kind::Ref && !rv->refMut && rv->refTo) {
                rv = rv->refTo;
            }

            bool isShift = n->op == AO::ShlAssign || n->op == AO::ShrAssign;
            bool isBitwise = n->op == AO::BitAndAssign ||
                             n->op == AO::BitOrAssign ||
                             n->op == AO::BitXorAssign;

            if (!isInt(l)) {
                if (!(isBool(l) && isBitwise)) {
                    error(n->loc, "compound assignment requires an integer (or bitwise bool)");
                } else if (!typeEquals(l, rv)) {
                    error(n->loc, "compound assignment type mismatch");
                }
            } else if (isShift) {
                if (!isInt(rv)) {
                    error(n->loc, "shift count must be an integer");
                }
            } else if (!typeEquals(l, rv)) {
                error(n->loc, "compound assignment type mismatch");
            }
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
        // Box::<T>::new / Vec::<T>::new 等内建关联调用
        if (auto* p = dynamic_cast<const ast::Path*>(n->target.get())) {
            if (auto builtin = tryBuiltinCall(p, n)) {
                return *builtin;
            }
            if (auto assoc = tryExplicitAssocCall(p, n)) {
                return *assoc;
            }
        }

        TyPtr ft = checkExpr(n->target.get());
        std::vector<TyPtr> args;
        args.reserve(n->args.size());
        for (std::size_t i = 0; i < n->args.size(); ++i) {
            TyPtr hint = (i < ft->params.size()) ? ft->params[i] : nullptr;
            bool sharedTarget = hint && hint->kind == Ty::Kind::Ref && !hint->refMut;
            if (!sharedTarget && isImmutableContainerAccess(n->args[i].get())) {
                error(n->args[i]->loc, "cannot move out of an immutable container index");
            }
            args.push_back(checkExpr(n->args[i].get(), hint));
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
            if (!canCoerce(args[i], ft->params[i]))
                error(n->loc, "argument type mismatch");
        return ft->ret ? ft->ret : tyUnit();
    }

    TyPtr Sema::checkMethodCall(const ast::MethodCall* n) {
        TyPtr recvTy = checkExpr(n->target.get());     // 保留原始类型（含引用/Box）
        const std::string& name = n->method.ident.text;

        // 方法接收者：自动解引用 & 和 Box
        TyPtr recv = recvTy;
        while (recv && (recv->kind == Ty::Kind::Ref || recv->kind == Ty::Kind::Box)) {
            recv = (recv->kind == Ty::Kind::Ref) ? recv->refTo : recv->element;
        }

        auto checkArgs = [&](const std::vector<TyPtr>& expected) -> bool {
            if (n->args.size() != expected.size()) {
                for (auto& a : n->args) checkExpr(a.get());
                error(n->loc, "wrong number of arguments");
                return false;
            }
            for (std::size_t i = 0; i < n->args.size(); ++i) {
                bool sharedTarget = expected[i] && expected[i]->kind == Ty::Kind::Ref &&
                                    !expected[i]->refMut;
                if (!sharedTarget && isImmutableContainerAccess(n->args[i].get())) {
                    error(n->args[i]->loc,
                          "cannot move out of an immutable container index");
                }
                TyPtr t = checkExpr(n->args[i].get(), expected[i]);
                if (!canCoerce(t, expected[i])) {
                    error(n->loc, "argument type mismatch");
                }
            }
            return true;
        };

        bool hasUserClone = false;
        if (recv && recv->kind == Ty::Kind::Struct) {
            auto* info = symbols_.findStruct(recv->name);
            hasUserClone = info && info->methods.count("clone") > 0;
        }

        if (name == "clone" && !hasUserClone) {
            TyPtr cloneBase = recvTy;
            while (cloneBase && cloneBase->kind == Ty::Kind::Ref) {
                cloneBase = cloneBase->refTo;       // 只剥引用，不剥 Box
            }
            if (traits_.isClone(cloneBase)) {
                return checkArgs({}) ? cloneBase : tyError();
            }
            if (recvTy && recvTy->kind == Ty::Kind::Ref && !recvTy->refMut) {
                return checkArgs({}) ? recvTy : tyError();
            }
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "type does not implement Clone");
            return tyError();
        }

        // 内建容器 / 标量方法
        if (recv && recv->kind == Ty::Kind::Vec) {
            if (name == "len") {
                return checkArgs({}) ? tyInt("usize") : tyError();
            }
            if (name == "is_empty") {
                return checkArgs({}) ? tyBool() : tyError();
            }
            if (name == "push") {
                bool ok = checkArgs({ recv->element });
                if (!isMutableAccess(n->target.get(), recvTy)) {
                    error(n->loc, "push requires mutable access");
                }
                return ok ? tyUnit() : tyError();
            }
            if (name == "remove") {
                bool ok = checkArgs({ tyInt("usize") });
                if (!isMutableAccess(n->target.get(), recvTy)) {
                    error(n->loc, "remove requires mutable access");
                }
                return ok ? recv->element : tyError();
            }
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "no method `" + name + "` on Vec");
            return tyError();
        }

        if (recv && recv->kind == Ty::Kind::Array) {
            if (name == "len") {
                return checkArgs({}) ? tyInt("usize") : tyError();
            }
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "no method `" + name + "` on array");
            return tyError();
        }

        if (recv && recv->kind == Ty::Kind::Box) {
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "no method `" + name + "` on Box");
            return tyError();
        }

        if (recv && (isInt(recv) || isBool(recv))) {
            if (name == "clone") {
                return checkArgs({}) ? recv : tyError();
            }
        }

        // 共享引用自身的 clone（目标不可 clone 时）
        if (name == "clone" && recvTy &&
            recvTy->kind == Ty::Kind::Ref && !recvTy->refMut) {
            return checkArgs({}) ? recvTy : tyError();
        }

        // 用户结构体方法
        if (isError(recv) || !recv || recv->kind != Ty::Kind::Struct) {
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "method call on a value with no methods");
            return tyError();
        }
        auto* info = symbols_.findStruct(recv->name);
        if (!info) {
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            return tyError();
        }

        auto it = info->methods.find(name);
        if (it == info->methods.end()) {
            // 派生 Clone
            if (name == "clone" && info->deriveClone) {
                return checkArgs({}) ? recv : tyError();
            }
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc, "no method `" + name + "`");
            return tyError();
        }

        ReceiverKind rk = info->methodReceivers[name];
        if (rk == ReceiverKind::None) {
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            error(n->loc,
                  "associated function `" + name +
                  "` cannot be called with dot syntax");
            return tyError();
        }

        TyPtr ft = it->second;
        if (!ft || ft->kind != Ty::Kind::Fn) {
            for (auto& a : n->args) {
                checkExpr(a.get());
            }
            return tyError();
        }

        if (rk == ReceiverKind::RefMut &&
            !isMutableAccess(n->target.get(), recvTy)) {
            error(n->loc, "mutable receiver requires a mutable place");
        }

        checkArgs(ft->params);
        return ft->ret ? ft->ret : tyUnit();
    }

    TyPtr Sema::checkField(const ast::Field* n) {
        TyPtr base = checkExpr(n->target.get());
        while (base && (base->kind == Ty::Kind::Ref || base->kind == Ty::Kind::Box)) {
            base = (base->kind == Ty::Kind::Ref) ? base->refTo : base->element;
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
        TyPtr idx = checkExpr(n->index.get(), tyInt("usize"));
        while (base && (base->kind == Ty::Kind::Ref || base->kind == Ty::Kind::Box)) {
            base = (base->kind == Ty::Kind::Ref) ? base->refTo : base->element;
        }
        // 无后缀字面量会被期望为 usize；显式 i32 变量在这里被拒绝
        if (!isError(idx) && (!isInt(idx) || idx->name != "usize")) {
            error(n->index->loc, "array index must be usize");
        }
        if (base && (base->kind == Ty::Kind::Array || base->kind == Ty::Kind::Vec)) {
            return base->element ? base->element : tyError();
        }
        if (!isError(base)) {
            error(n->loc, "cannot index this value");
        }
        return tyError();
    }

    TyPtr Sema::checkIf(const ast::If* n, const TyPtr& expected) {
        TyPtr cond = checkExpr(n->condition.get(), tyBool());
        if (!isBool(cond) && !isError(cond)) {
            error(n->condition->loc, "if condition must be bool");
        }
        TyPtr thenTy = checkBlock(n->thenBlock.get(), true, expected);
        if (n->elseBranch) {
            TyPtr elseTy = checkExpr(n->elseBranch.get(), expected);
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

    TyPtr Sema::checkLoop(const ast::Loop* n, const TyPtr& expected) {
        LoopCtx ctx;
        ctx.expected = expected;             // break 值用外层期望类型推断
        loops.push_back(std::move(ctx));     // 新循环作为最近的跳转目标
        TyPtr body = checkBlock(n->body.get(), true, nullptr);
        LoopCtx done = loops.back();
        loops.pop_back();

        if (!isUnit(body) && !isNever(body)) {
            error(n->body->loc, "loop body must be unit");
        }
        // 没有任何 break => 永不结束，类型是 !
        if (!done.sawBreak) {
            return tyNever();
        }
        return done.breakTy ? done.breakTy : tyUnit();
    }

    TyPtr Sema::checkWhile(const ast::While* n) {
        // while 的条件不属于本 while 的跳转目标，
        // 同时也不能跳到外层循环：检查条件时暂时清空循环栈。
        auto savedLoops = loops;
        loops.clear();
        TyPtr cond = checkExpr(n->condition.get(), tyBool());
        loops = savedLoops;
        if (!isBool(cond) && !isError(cond)) {
            error(n->condition->loc, "while condition must be bool");
        }
        loops.push_back(LoopCtx{nullptr, true, false});   // isWhile = true
        TyPtr body = checkBlock(n->body.get(), true);
        loops.pop_back();
        if (!isUnit(body) && !isNever(body)) {
            error(n->body->loc, "while body must be unit");
        }
        return tyUnit();                                  // while 永远返回 ()
    }

    TyPtr Sema::checkReturn(const ast::Return* n) {
        TyPtr t = n->value ? checkExpr(n->value.get(), curReturn_) : tyUnit();
        if (curReturn_ && !isNever(t) && !canCoerce(t, curReturn_)) {
            error(n->loc, "return type mismatch");
        }
        return tyNever();
    }

    TyPtr Sema::checkBreak(const ast::Break* n) {
        if (loops.empty()) {
            if (n->value) {
                checkExpr(n->value.get());
            }
            error(n->loc, "`break` outside loop");
            return tyNever();
        }

        LoopCtx& ctx = loops.back();
        TyPtr valueTy = n->value
                            ? checkExpr(n->value.get(), ctx.expected)
                            : tyUnit();

        if (ctx.isWhile && n->value) {
            error(n->loc, "a while loop cannot accept a break value");
            return tyNever();
        }
        // 把 break 的值类型并入最近的循环
        if (!ctx.sawBreak) {
            ctx.sawBreak = true;
            ctx.breakTy = valueTy;
        } else if (!typeEquals(ctx.breakTy, valueTy)) {
            error(n->loc, "break value types must match");
        }
        return tyNever();
    }

    TyPtr Sema::checkStructLit(const ast::StructLit* n) {
        if (n->path.empty()) {
            return tyError();
        }
        std::string name = n->path.back().ident.text;
        if (name == "Self") {
            name = curSelfType_;
        }
        if (name.empty()) {
            error(n->loc, "`Self` is not available here");
            return tyError();
        }
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
            auto it = info->fieldTypes.find(fi.name);
            TyPtr fieldTy = (it == info->fieldTypes.end()) ? nullptr : it->second;

            TyPtr vt = checkExpr(fi.value.get(), fieldTy);

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

    TyPtr Sema::checkArrayLit(const ast::ArrayLit* n, const TyPtr& expected) {
        if (n->elements.empty()) {
            return tyError();
        }

        TyPtr expectedElem =
            (expected && expected->kind == Ty::Kind::Array)
                ? expected->element
                : nullptr;

        TyPtr elem = checkExpr(n->elements.front().get(), expectedElem);
        for (std::size_t i = 1; i < n->elements.size(); ++i) {
            TyPtr hint = expectedElem ? expectedElem : elem;
            TyPtr t = checkExpr(n->elements[i].get(), hint);
            if (!typeEquals(elem, t)) {
                error(n->elements[i]->loc, "array elements have different types");
            }
        }
        return tyArray(elem, static_cast<std::size_t>(n->elements.size()));
    }

    TyPtr Sema::checkArrayRepeat(const ast::ArrayRepeat* n, const TyPtr& expected) {
        TyPtr expectedElem = (expected && expected->kind == Ty::Kind::Array) ? expected->element : nullptr;
        TyPtr elem = checkExpr(n->element.get(), expectedElem);
        auto len = constEval_.evalUsize(n->count.get(), n->count->loc);
        // 长度 > 1 的重复要求元素 Copy（长度 1 不需要）
        if (len && *len > 1 && !traits_.isCopy(elem)) {
            error(n->loc, "repeating an array more than once requires Copy");
        }
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

    bool Sema::canCoerce(const TyPtr& actual, const TyPtr& expected) {
        if (typeEquals(actual, expected)) return true;
        // 顶层 &mut T -> &T（重借用，不移动）
        if (actual && expected &&
            actual->kind == Ty::Kind::Ref && expected->kind == Ty::Kind::Ref &&
            actual->refMut && !expected->refMut &&
            typeEquals(actual->refTo, expected->refTo)) {
            return true;
        }
        return false;
    }

    TyPtr Sema::unref(const TyPtr& t) {
        TyPtr r = t;
        while (r && r->kind == Ty::Kind::Ref) {
            r = r->refTo;
        }
        return r;
    }

    TyPtr Sema::typeArgOf(const ast::PathSegment& seg) {
        if (!seg.generics) return nullptr;
        for (auto& ga : seg.generics->args) {
            if (auto* ta = dynamic_cast<const ast::TypeArg*>(ga.get())) {
                return resolveType(ta->value.get());
            }
        }
        return nullptr;
    }

    bool Sema::isMutableAccess(const ast::Expr* e, const TyPtr& ty) {
        // 不可变容器里取非 Copy 元素，即使元素类型是 &mut 也不能直接拿来用
        if (isImmutableContainerAccess(e)) return false;
        if (ty && ty->kind == Ty::Kind::Ref) return ty->refMut;
        if (!isPlace(e)) return true;                            // 右值临时
        return isMutablePlace(e);
    }

    // Box::<T>::new / Vec::<T>::new 等内建关联调用
    std::optional<TyPtr> Sema::tryBuiltinCall(const ast::Path* p, const ast::Call* c) {
        if (!p || p->segments.size() != 2) return std::nullopt;
        const std::string& tyName = p->segments[0].ident.text;
        const std::string& fn = p->segments[1].ident.text;
        if (tyName != "Box" && tyName != "Vec") return std::nullopt;

        TyPtr elemTy = typeArgOf(p->segments[0]);
        if (!elemTy) {
            error(p->loc, tyName + " needs a type argument");
            return tyError();
        }

        auto exactArgs = [&](std::size_t n) -> bool {
            if (c->args.size() != n) {
                for (auto& a : c->args) {
                    checkExpr(a.get());
                }
                error(c->loc, "wrong number of arguments");
                return false;
            }
            return true;
        };

        if (tyName == "Box") {
            if (fn == "new") {
                if (!exactArgs(1)) {
                    return tyError();
                }
                TyPtr a = checkExpr(c->args[0].get(), elemTy);
                if (!typeEquals(a, elemTy)) {
                    error(c->loc, "argument type mismatch");
                }
                return tyBox(elemTy);
            }
            if (fn == "clone") {
                if (!exactArgs(1)) {
                    return tyError();
                }
                if (!traits_.isClone(elemTy)) {
                    error(c->loc, "Box<T>::clone requires T: Clone");
                    return tyError();
                }
                TyPtr a = checkExpr(c->args[0].get());
                TyPtr base = unref(a);
                if (!base || base->kind != Ty::Kind::Box || !typeEquals(base->element, elemTy)) {
                    error(c->loc, "Box::clone expects &Box<T>");
                }
                return tyBox(elemTy);
            }
            error(p->loc, "unknown Box function `" + fn + "`");
            return tyError();
        }

        // Vec
        if (fn == "new") {
            if (!exactArgs(0)) {
                return tyError();
            }
            return tyVec(elemTy);
        }
        if (fn == "len") {
            if (!exactArgs(1)) return tyError();
            TyPtr a = checkExpr(c->args[0].get());
            TyPtr base = unref(a);
            if (!base || base->kind != Ty::Kind::Vec ||
                !typeEquals(base->element, elemTy)) {
                error(c->loc, "Vec::len expects &Vec<T>");
            }
            return tyInt("usize");
        }
        if (fn == "push") {
            if (!exactArgs(2)) return tyError();
            TyPtr a0 = checkExpr(c->args[0].get());
            TyPtr base = unref(a0);
            if (!base || base->kind != Ty::Kind::Vec ||
                !typeEquals(base->element, elemTy)) {
                error(c->loc, "Vec::push expects &mut Vec<T>");
            } else if (!a0 || a0->kind != Ty::Kind::Ref || !a0->refMut) {
                error(c->loc, "Vec::push requires mutable access");
            }
            TyPtr a1 = checkExpr(c->args[1].get(), elemTy);
            if (!typeEquals(a1, elemTy)) error(c->loc, "push argument type mismatch");
            return tyUnit();
        }
        if (fn == "remove") {
            if (!exactArgs(2)) return tyError();
            TyPtr a0 = checkExpr(c->args[0].get());
            TyPtr base = unref(a0);
            if (!base || base->kind != Ty::Kind::Vec ||
                !typeEquals(base->element, elemTy)) {
                error(c->loc, "Vec::remove expects &mut Vec<T>");
            } else if (!a0 || a0->kind != Ty::Kind::Ref || !a0->refMut) {
                error(c->loc, "Vec::remove requires mutable access");
            }
            TyPtr idx = checkExpr(c->args[1].get(), tyInt("usize"));
            if (!isError(idx) && (!isInt(idx) || idx->name != "usize")) {
                error(c->args[1]->loc, "remove index must be usize");
            }
            return elemTy;
        }
        if (fn == "clone") {
            if (!exactArgs(1)) return tyError();
            if (!traits_.isClone(elemTy)) {              // ★
                error(c->loc, "Vec<T>::clone requires T: Clone");
                return tyError();
            }
            TyPtr a = checkExpr(c->args[0].get());
            TyPtr base = unref(a);
            if (!base || base->kind != Ty::Kind::Vec ||
                !typeEquals(base->element, elemTy)) {
                error(c->loc, "Vec::clone expects &Vec<T>");
                }
            return tyVec(elemTy);
        }
        error(p->loc, "unknown Vec function `" + fn + "`");
        return tyError();
    }

    // Type::method(args) 显式关联调用（含派生 Clone）
    std::optional<TyPtr> Sema::tryExplicitAssocCall(const ast::Path* p, const ast::Call* c) {
        if (!p || p->segments.size() != 2) {
            return std::nullopt;
        }
        std::string typeName = p->segments[0].ident.text;
        if (typeName == "Self") {
            typeName = curSelfType_;
        }
        auto* info = symbols_.findStruct(typeName);
        if (!info) {
            return std::nullopt;          // Box/Vec 等由 tryBuiltinCall 处理
        }
        const std::string& name = p->segments[1].ident.text;
        auto mit = info->methods.find(name);

        if (mit == info->methods.end()) {
            // 派生 Clone 的显式形式：Pair::clone(&a)
            if (name == "clone" && traits_.isClone(tyStruct(typeName))) {
                if (c->args.size() != 1) {
                    for (auto& a : c->args) checkExpr(a.get());
                    error(c->loc, "wrong number of arguments");
                    return tyStruct(typeName);
                }
                TyPtr expectRecv = tyRef(tyStruct(typeName), false);
                TyPtr t = checkExpr(c->args[0].get(), expectRecv);
                if (!typeEquals(t, expectRecv)) error(c->loc, "argument type mismatch");
                return tyStruct(typeName);
            }
            if (info->assocConsts.count(name)) {
                error(c->loc, "associated constant `" + name + "` is not callable");
                return tyError();
            }
            return std::nullopt;
        }

        TyPtr ft = mit->second;
        if (!ft || ft->kind != Ty::Kind::Fn) return tyError();

        ReceiverKind rk = info->methodReceivers[name];
        std::vector<TyPtr> expected;
        TyPtr selfTy = tyStruct(typeName);
        if (rk == ReceiverKind::Value) {
            expected.push_back(selfTy);
        } else if (rk == ReceiverKind::Ref) {
            expected.push_back(tyRef(selfTy, false));
        } else if (rk == ReceiverKind::RefMut) {
            expected.push_back(tyRef(selfTy, true));
        }
        for (auto& t : ft->params) expected.push_back(t);

        if (c->args.size() != expected.size()) {
            for (auto& a : c->args) checkExpr(a.get());
            error(c->loc, "wrong number of arguments");
            return ft->ret ? ft->ret : tyUnit();
        }
        for (std::size_t i = 0; i < c->args.size(); ++i) {
            TyPtr t = checkExpr(c->args[i].get(), expected[i]);
            if (!typeEquals(t, expected[i])) error(c->loc, "argument type mismatch");
        }
        return ft->ret ? ft->ret : tyUnit();
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

    namespace {
        // 解引用链上是否出现 &mut（Box 透明穿过）
        bool chainHasMutRef(const TyPtr& t) {
            TyPtr x = t;
            while (x) {
                if (x->kind == Ty::Kind::Ref) {
                    if (x->refMut) return true;
                    x = x->refTo;
                } else if (x->kind == Ty::Kind::Box) {
                    x = x->element;
                } else {
                    break;
                }
            }
            return false;
        }
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
            if (isFrozenPlace(e)) return false;
            TyPtr bt = checkExpr(f->target.get());
            if (bt && bt->kind == Ty::Kind::Ref) return bt->refMut;
            if (chainHasMutRef(bt)) return true;
            if (!isPlace(f->target.get())) return true;   // 右值临时
            return isMutablePlace(f->target.get());
        }
        if (auto* i = dynamic_cast<const ast::Index*>(e)) {
            if (isFrozenPlace(e)) return false;
            TyPtr bt = checkExpr(i->target.get());
            if (bt && bt->kind == Ty::Kind::Ref) return bt->refMut;
            if (chainHasMutRef(bt)) return true;
            if (!isPlace(i->target.get())) return true;   // 右值临时
            return isMutablePlace(i->target.get());
        }
        if (auto* u = dynamic_cast<const ast::Unary*>(e)) {
            if (u->op != ast::UnaryOp::DeRef) {
                return false;
            }
            TyPtr t = checkExpr(u->operand.get());
            if (!t) return false;
            if (t->kind == Ty::Kind::Ref) {
                return t->refMut && !isFrozenPlace(u->operand.get());
            }
            if (t->kind == Ty::Kind::Box) {
                // Box 所有者可变，或 Box 本身是右值临时
                return isPlace(u->operand.get())
                           ? isMutablePlace(u->operand.get())
                           : true;
            }
            return false;
        }
        return false;
    }

    // 是否经过共享引用投影（字段/索引/解引用链上出现 &T）
    bool Sema::isFrozenPlace(const ast::Expr* e) {
        if (!e) return false;
        if (dynamic_cast<const ast::Path*>(e)) return false;

        if (auto* f = dynamic_cast<const ast::Field*>(e)) {
            TyPtr bt = checkExpr(f->target.get());
            if (bt && bt->kind == Ty::Kind::Ref && !bt->refMut) return true;
            return isFrozenPlace(f->target.get());
        }
        if (auto* i = dynamic_cast<const ast::Index*>(e)) {
            if (isImmutableContainerAccess(e)) return true;
            TyPtr bt = checkExpr(i->target.get());
            if (bt && bt->kind == Ty::Kind::Ref && !bt->refMut) return true;
            return isFrozenPlace(i->target.get());
        }
        if (auto* u = dynamic_cast<const ast::Unary*>(e)) {
            if (u->op != ast::UnaryOp::DeRef) return false;
            TyPtr t = checkExpr(u->operand.get());
            if (t && t->kind == Ty::Kind::Ref && !t->refMut) return true;
            return isFrozenPlace(u->operand.get());
        }
        return false;
    }

    // 从不可变容器（Vec）里取出非 Copy 元素：等价于“移动”，不允许
    bool Sema::isImmutableContainerAccess(const ast::Expr* e) {
        auto* idx = dynamic_cast<const ast::Index*>(e);
        if (!idx) return false;

        TyPtr bt = checkExpr(idx->target.get());
        TyPtr base = bt;
        while (base && (base->kind == Ty::Kind::Ref || base->kind == Ty::Kind::Box)) {
            base = (base->kind == Ty::Kind::Ref) ? base->refTo : base->element;
        }
        if (!base || base->kind != Ty::Kind::Vec) return false;
        if (isCopyType(base->element)) return false;
        if (chainHasMutRef(bt)) return false;                 // 链上有 &mut
        return !isMutableAccess(idx->target.get(), bt);
    }

    bool Sema::isCopyType(const TyPtr& t, int depth) {
        return traits_.isCopy(t);
    }
}