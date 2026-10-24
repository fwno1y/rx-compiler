#include "Sema.h"

namespace rx::sema {
    bool Sema::run() {
        declareBuiltins();
        collectItems();
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
            if (info.fieldType.count(fld.name)) {
                error(fld.loc, "duplicate field '" + fld.name + "'");
            }
            info.fieldOrder.push_back(fld.name);
            info.fieldType[fld.name] = nullptr;
        }
    }

    void Sema::resolveStruct(const ast::StructDef *s) {
        auto info = symbols_.findStruct(s->name);
        if (!info || info->def != s) {
            return;
        }
        for (auto& fld : s->fields) {
            info->fieldType[fld.name] = resolveType(fld.type.get());
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
        }
        for (auto& it : impl->items) {
            if (auto* m = dynamic_cast<const ast::FunctionDef*>(it.get())) {
                if (info->methods.count(m->name)) {
                    error(m->loc, "duplicate method '" + m->name + "'");
                    continue;
                }
                info->methods[m->name] = resolveFunctionType(m);
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
            return tyArray(resolveType(a->element.get()), 0);
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
        TyPtr res = tyUnit();
        if (createsScope) {
            symbols_.popScope();
        }
        return res;
    }

    void Sema::checkStmt(const ast::Stmt *s) {
        if (auto* l = dynamic_cast<const ast::LetStmt*>(s)) {
            Symbol symbol;
            symbol.kind = Symbol::Kind::Local;
            symbol.isMut = l->binding.isMut;
            symbol.type = l->type ? resolveType(l->type.get()) : tyError();
            symbols_.declareLocal(l->binding.name, std::move(symbol));
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
}